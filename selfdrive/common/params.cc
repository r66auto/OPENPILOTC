#include "common/params.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif  // _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <mutex>
#include <csignal>
#include <string.h>

#include "common/util.h"

#define HANDLE_EINTR(x)                                       \
  ({                                                          \
    decltype(x) ret;                                          \
    int try_cnt = 0;                                          \
    do {                                                      \
      ret = (x);                                              \
    } while (ret == -1 && errno == EINTR && try_cnt++ < 100); \
    ret;                                                      \
  })

#if defined(QCOM) || defined(QCOM2)
const std::string default_params_path = "/data/params";
#else
const std::string default_params_path = util::getenv_default("HOME", "/.comma/params", "/data/params");
#endif

#if defined(QCOM) || defined(QCOM2)
const std::string persistent_params_path = "/persist/comma/params";
#else
const std::string persistent_params_path = default_params_path;
#endif


volatile sig_atomic_t params_do_exit = 0;
void params_sig_handler(int signal) {
  params_do_exit = 1;
}

static int fsync_dir(const char* path){
  int fd = HANDLE_EINTR(open(path, O_RDONLY, 0755));
  if (fd < 0){
    return -1;
  }

  int result = fsync(fd);
  int result_close = close(fd);
  if (result_close < 0) {
    result = result_close;
  }
  return result;
}

// TODO: replace by std::filesystem::create_directories
static int mkdir_p(std::string path) {
  char * _path = (char *)path.c_str();

  mode_t prev_mask = umask(0);
  for (char *p = _path + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0'; // Temporarily truncate
      if (mkdir(_path, 0777) != 0) {
        if (errno != EEXIST) return -1;
      }
      *p = '/';
    }
  }
  if (mkdir(_path, 0777) != 0) {
    if (errno != EEXIST) return -1;
  }
  chmod(_path, 0777);
  umask(prev_mask);
  return 0;
}

static bool ensure_params_path(const std::string &param_path, const std::string &key_path) {
  // Make sure params path exists
  if (!util::file_exists(param_path) && mkdir_p(param_path) != 0) {
    return false;
  }

  // See if the symlink exists, otherwise create it
  if (!util::file_exists(key_path)) {
    // 1) Create temp folder
    // 2) Set permissions
    // 3) Symlink it to temp link
    // 4) Move symlink to <params>/d

    std::string tmp_path = param_path + "/.tmp_XXXXXX";
    // this should be OK since mkdtemp just replaces characters in place
    char *tmp_dir = mkdtemp((char *)tmp_path.c_str());
    if (tmp_dir == NULL) {
      return false;
    }

    if (chmod(tmp_dir, 0777) != 0) {
      return false;
    }

    std::string link_path = std::string(tmp_dir) + ".link";
    if (symlink(tmp_dir, link_path.c_str()) != 0) {
      return false;
    }

    // don't return false if it has been created by other
    if (rename(link_path.c_str(), key_path.c_str()) != 0 && errno != EEXIST) {
      return false;
    }
  }

  // Ensure permissions are correct in case we didn't create the symlink
  return chmod(key_path.c_str(), 0777) == 0;
}

class FileLock {
 public:
  FileLock(const std::string& file_name, int op) : fn_(file_name), op_(op) {}

  void lock() {
    // keep trying if open() gets interrupted by a signal
    fd_ = HANDLE_EINTR(open(fn_.c_str(), O_CREAT, 0775));
    if (fd_ < 0) {
      throw std::runtime_error(util::string_format("Failed to open lock file %s, errno=%d", fn_.c_str(), errno));
    }

    // keep trying if flock() gets interrupted by a signal
    int err = HANDLE_EINTR(flock(fd_, op_));
    if (err < 0) {
      close(fd_);
      throw std::runtime_error(util::string_format("Failed to lock file %s, errno=%d", fn_.c_str(), errno));
    }
  }

  void unlock() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  ~FileLock() {
    unlock();
  }

private:
  int fd_ = -1;
  int op_;
  std::string fn_;
};

Params::Params(bool persistent_param) : Params(persistent_param ? persistent_params_path : default_params_path) {}

Params::Params(const std::string &path) : params_path(path) {
  if (!ensure_params_path(params_path, params_path + "/d")) {
    throw std::runtime_error(util::string_format("Failed to ensure params path, errno=%d", errno));
  }
}

int Params::put(const char* key, const char* value, size_t value_size) {
  // Information about safely and atomically writing a file: https://lwn.net/Articles/457667/
  // 1) Create temp file
  // 2) Write data to temp file
  // 3) fsync() the temp file
  // 4) rename the temp file to the real name
  // 5) fsync() the containing directory

  int tmp_fd = -1;
  int result;
  std::string path;
  std::string tmp_path;
  ssize_t bytes_written;

  FileLock file_lock(params_path + "/.lock", LOCK_EX);
  std::lock_guard<FileLock> lk(file_lock);

  // Write value to temp.
  tmp_path = params_path + "/.tmp_value_XXXXXX";
  tmp_fd = mkstemp((char*)tmp_path.c_str());
  bytes_written = HANDLE_EINTR(write(tmp_fd, value, value_size));
  if (bytes_written < 0 || (size_t)bytes_written != value_size) {
    result = -20;
    goto cleanup;
  }

  // Build key path
  path = params_path + "/d/" + std::string(key);

  // change permissions to 0666 for apks
  result = fchmod(tmp_fd, 0666);
  if (result < 0) {
    goto cleanup;
  }

  // fsync to force persist the changes.
  result = fsync(tmp_fd);
  if (result < 0) {
    goto cleanup;
  }

  // Move temp into place.
  result = rename(tmp_path.c_str(), path.c_str());
  if (result < 0) {
    goto cleanup;
  }

  // fsync parent directory
  path = params_path + "/d";
  result = fsync_dir(path.c_str());
  if (result < 0) {
    goto cleanup;
  }

cleanup:
  if (tmp_fd >= 0) {
    if (result < 0) {
      remove(tmp_path.c_str());
    }
    close(tmp_fd);
  }
  return result;
}

int Params::remove(const char *key) {
  FileLock file_lock(params_path + "/.lock", LOCK_EX);
  std::lock_guard<FileLock> lk(file_lock);
  // Delete value.
  std::string path = params_path + "/d/" + key;
  int result = ::remove(path.c_str());
  if (result != 0) {
    result = ERR_NO_VALUE;
    return result;
  }
  // fsync parent directory
  path = params_path + "/d";
  return fsync_dir(path.c_str());
}

std::string Params::get(const char *key, bool block) {
  std::string path = params_path + "/d/" + key;
  if (!block) {
    return util::read_file(path);
  } else {
    // blocking read until successful
    params_do_exit = 0;
    void (*prev_handler_sigint)(int) = std::signal(SIGINT, params_sig_handler);
    void (*prev_handler_sigterm)(int) = std::signal(SIGTERM, params_sig_handler);

    std::string value;
    while (!params_do_exit) {
      if (value = util::read_file(path); !value.empty()) {
        break;
      }
      util::sleep_for(100);  // 0.1 s
    }

    std::signal(SIGINT, prev_handler_sigint);
    std::signal(SIGTERM, prev_handler_sigterm);
    return value;
  }
}

int Params::read_db_all(std::map<std::string, std::string> *params) {
  FileLock file_lock(params_path + "/.lock", LOCK_SH);
  std::lock_guard<FileLock> lk(file_lock);

  std::string key_path = params_path + "/d";
  DIR *d = opendir(key_path.c_str());
  if (!d) {
    return -1;
  }

  struct dirent *de = NULL;
  while ((de = readdir(d))) {
    if (!isalnum(de->d_name[0])) continue;
    std::string key = std::string(de->d_name);
    std::string value = util::read_file(key_path + "/" + key);

    (*params)[key] = value;
  }

  closedir(d);
  return 0;
}
