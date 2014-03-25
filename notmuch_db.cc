
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "infinibuf.h"
#include "notmuch_db.h"

using namespace std;

static unordered_set<string>
lines(const string &s)
{
  istringstream is (s);
  string line;
  unordered_set<string> ret;
  while (getline(is, line))
    ret.insert(line);
  return ret;
}

static string
chomp(string s)
{
  while (s.length() && (s.back() == '\n' || s.back() == '\r'))
    s.resize(s.length() - 1);
  return s;
}

static bool
conf_to_bool(string s)
{
  s = chomp(s);
  if (s.empty() || s == "false" || s == "0")
    return false;
  return true;
}

string
notmuch_db::default_notmuch_config()
{
  char *p = getenv("NOTMUCH_CONFIG");
  if (p && *p)
    return p;
  p = getenv("HOME");
  if (p && *p)
    return string(p) + "/.notmuch-config";
  throw runtime_error ("Cannot find HOME directory\n");
}

string
notmuch_db::config_get(const char *config)
{
  const char *av[] { "notmuch", "config", "get", config, nullptr };
  return run_notmuch(av);
}

notmuch_db::notmuch_db(string config)
  : notmuch_config (config),
    maildir (chomp(config_get("database.path"))),
    new_tags (lines(config_get("new.tags"))),
    sync_flags (conf_to_bool(config_get("maildir.synchronize_flags")))
{
  if (maildir.empty())
    throw runtime_error(notmuch_config + ": no database.path in config file");
  struct stat sb;
  if (stat(maildir.c_str(), &sb) || !S_ISDIR(sb.st_mode))
    throw runtime_error(maildir + ": cannot access maildir");
}

notmuch_db::~notmuch_db()
{
  close();
}

string
notmuch_db::run_notmuch(const char **av)
{
  int fds[2];
  if (pipe(fds) != 0)
    throw runtime_error (string("pipe: ") + strerror(errno));
  pid_t pid = fork();
  switch (pid) {
  case -1:
    {
      string err = string("fork: ") + strerror(errno);
      ::close(fds[0]);
      ::close(fds[1]);
      throw runtime_error (err);
    }
  case 0:
    ::close(fds[0]);
    if (fds[1] != 1) {
      dup2(fds[1], 1);
      ::close(fds[1]);
    }
    setenv("NOTMUCH_CONFIG", notmuch_config.c_str(), 1);
    execvp("notmuch", const_cast<char *const*> (av));
    cerr << "notmuch: " << strerror(errno) << endl;
    raise(SIGINT);
    _exit(127);
  }

  ::close(fds[1]);
  ifdstream in (fds[0]);
  ostringstream os;
  os << in.rdbuf();

  int status;
  if (waitpid(pid, &status, 0) == pid
      && WIFSIGNALED(pid) && WTERMSIG(status) == SIGINT)
    throw runtime_error ("could not run notmuch");
  return os.str();
}


notmuch_database_t *
notmuch_db::notmuch ()
{
  if (!notmuch_) {
    notmuch_status_t err =
      notmuch_database_open (maildir.c_str(),
			     NOTMUCH_DATABASE_MODE_READ_WRITE,
			     &notmuch_);
    if (err)
      throw runtime_error (maildir + ": "
			   + notmuch_status_to_string(err));
  }
  return notmuch_;
}

void
notmuch_db::close()
{
  if (notmuch_)
    notmuch_database_destroy (notmuch_);
  notmuch_ = nullptr;
}

int
main(int argc, char **argv)
{
  notmuch_db nm (notmuch_db::default_notmuch_config());
  for (auto x : nm.new_tags)
    cout << x << '\n';
  for (int i = 1; i < argc; i++)
    cout << "**** " << argv[i] << '\n' << nm.config_get (argv[i]) << '\n';
  return 0;
}