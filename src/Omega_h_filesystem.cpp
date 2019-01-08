#include <Omega_h_filesystem.hpp>
#include <Omega_h_fail.hpp>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

namespace Omega_h {

namespace filesystem {

bool create_directory(path const& p) {
  ::mode_t const mode = S_IRWXU | S_IRWXG | S_IRWXO;
  errno = 0;
  int err = ::mkdir(p.c_str(), mode);
  if (err != 0) {
    if (errno != EEXIST) {
      throw filesystem_error(errno, "create_directory");
    }
    return false;
  }
  return true;
}

path current_path() {
  char buf[1024];
  errno = 0;
  char* ret = ::getcwd(buf, sizeof(buf));
  if (ret == nullptr) {
    throw filesystem_error(errno, "current_path");
  }
  return ret;
}

bool remove(path const& p) {
  errno = 0;
  int err = ::remove(p.impl.c_str());
  if (err != 0) {
    throw filesystem_error(errno, "remove");
  }
  return true;
}

bool exists(path const& p) {
  return ::access(p.impl.c_str(), F_OK) != -1;
}

struct IteratorImpl {
  IteratorImpl():stream(nullptr),entry(nullptr) {}
  IteratorImpl(path const& p):root(p),entry(nullptr) {
    errno = 0;
    stream = ::opendir(p.impl.c_str());
    if (stream == nullptr) {
      throw filesystem_error(errno, "directory_iterator");
    }
    increment();
  }
  ~IteratorImpl() {
    close();
  }
  void close() {
    if (stream == nullptr) return;
    errno = 0;
    int err = ::closedir(stream);
    stream = nullptr;
    if (err != 0) {
      throw filesystem_error(errno, "directory_iterator");
    }
  }
  void increment() {
    errno = 0;
    entry = ::readdir(stream);
    if (entry == nullptr) {
      if (errno != 0) {
        throw filesystem_error(errno, "directory_iterator");
      }
      // safely reached the end of the directory
      close();
    } else {
      // we just extracted a good entry from the stream
      // skip dot and dot-dot, max 3-call recursion
      if (0 == strcmp(entry->d_name, ".")) increment();
      else if (0 == strcmp(entry->d_name, "..")) increment();
    }
  }
  directory_entry deref() {
    OMEGA_H_CHECK(entry != nullptr);
    return directory_entry(root / entry->d_name);
  }
  bool is_end() { return entry == nullptr; }
  bool equal(IteratorImpl const& other) const {
    if (entry == nullptr && other.entry == nullptr) return true;
    if (root.impl != other.root.impl) return false;
    return 0 == strcmp(entry->d_name, other.entry->d_name);
  }
  path root;
  DIR* stream;
  ::dirent* entry;
};

file_status status(path const& p) {
  errno = 0;
  struct ::stat buf;
  int ret = ::stat(p.c_str(), &buf);
  if (ret != 0) {
    throw filesystem_error(errno, "status");
  }
  file_type type;
  if (buf.st_mode & S_IFREG) type = file_type::regular;
  else if (buf.st_mode & S_IFDIR) type = file_type::directory;
  else if (buf.st_mode & S_IFLNK) type = file_type::symlink;
  else if (buf.st_mode & S_IFBLK) type = file_type::block;
  else if (buf.st_mode & S_IFCHR) type = file_type::character;
  else if (buf.st_mode & S_IFIFO) type = file_type::fifo;
  return file_status(type);
}

// end of OS-specific stuff

std::uintmax_t remove_all(path const& p) {
  directory_iterator end;
  std::uintmax_t count = 0;
  while (true) {
    directory_iterator first(p);
    if (first == end) break;
    if (first->is_directory()) {
      count += remove_all(first->path());
    } else {
      remove(first->path());
      ++count;
    }
  }
  remove(p);
  ++count;
  return count;
}

filesystem_error::~filesystem_error() {}

path::path(char const* source):impl(source) {
}

path::path(std::string const& source):impl(source) {
}

path::value_type const* path::c_str() const noexcept {
  return native().c_str();
}

const path::string_type& path::native() const noexcept {
  return impl;
}

path operator/(path const& a, path const& b) {
  auto str = a.impl;
  str.push_back(path::preferred_separator);
  str += b.impl;
  return str;
}

std::ostream& operator<<(std::ostream& os, path const& p) {
  os << p.c_str();
  return os;
}

filesystem_error::filesystem_error(int ev, const char* what_arg)
  : std::system_error(ev, std::system_category(), what_arg)
{
}

file_status::file_status(file_type const& type_in)
  :type_variable(type_in)
{
}

file_type file_status::type() const noexcept {
  return type_variable;
}

directory_entry::directory_entry(class path const& p):path_variable(p) {}

bool directory_entry::is_regular_file() const {
  return status(path_variable).type() == file_type::regular;
}

bool directory_entry::is_directory() const {
  return status(path_variable).type() == file_type::directory;
}

bool directory_entry::is_symlink() const {
  return status(path_variable).type() == file_type::symlink;
}

directory_iterator::directory_iterator()
 : impl(new IteratorImpl())
{
}

directory_iterator::directory_iterator(directory_iterator&& other) {
  delete impl;
  impl = other.impl;
  other.impl = new IteratorImpl();
}

directory_iterator::directory_iterator(path const& p)
 : impl(new IteratorImpl(p))
{
  if (!impl->is_end()) entry = impl->deref();
}

directory_iterator::~directory_iterator() {
  delete impl;
  impl = nullptr;
}

directory_iterator& directory_iterator::operator++() {
  impl->increment();
  if (!impl->is_end()) entry = impl->deref();
  return *this;
}

const directory_entry& directory_iterator::operator*() const {
  return entry;
}

const directory_entry* directory_iterator::operator->() const {
  return &entry;
}

bool directory_iterator::operator==(directory_iterator const& other) const {
  return impl->equal(*other.impl);
}

}

}