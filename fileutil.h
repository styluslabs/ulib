#ifndef FILEUTIL_H
#define FILEUTIL_H

#include <string.h>
#include <limits.h>
#include <string>
#include <vector>
#include <algorithm>
#include "platformutil.h"

// Windows uses UTF-16, but we use UTF-8 internally
#if PLATFORM_WIN
std::wstring utf8_to_wstr(const char* str8);
std::string wstr_to_utf8(wchar_t* wstr);
std::vector<std::string> lsDrives();
// note .data() must be done in macro, not utf8_to_utf16 of course!
#define PLATFORM_STR(s) (utf8_to_wstr(s).data())
#define fopen(filename, mode) _wfopen(PLATFORM_STR(filename), PLATFORM_STR(mode))
#define stat(path, pstat) _wstat(PLATFORM_STR(path), (struct _stat64i32*)pstat)
#else
#define PLATFORM_STR(s) (s)
#endif

// sadly, there is no cross-platform way to use the C file I/O fns w/ a memory stream, so we add our own
//  abstraction (Unix has fmemopen, but not avail on Windows)
// FIFOStream class with separate read and write positions?
struct IOStream
{
  IOStream() {}
  IOStream(const IOStream&) = delete;
  virtual ~IOStream() {}
  virtual bool is_open() const { return true; }
  virtual size_t read(void* dest, size_t len) = 0;  // returns number of bytes read (from stream to dest)
  virtual size_t write(const void* src, size_t len) = 0;  // returns number of bytes written (from src to stream)
  virtual long tell() const = 0;  // get stream position
  virtual bool seek(long offset, int origin = SEEK_SET) = 0;  // set stream position
  virtual bool flush() { return true; }
  virtual bool truncate(size_t len) = 0;  // returns true on success
  virtual const char* name() const { return ""; }
  virtual size_t size() const = 0;
  virtual size_t readp(void** pdest, size_t len) = 0;  // read into buffer owned by stream
  virtual int type() const = 0;
  enum StreamType { MEMSTREAM = 1, FILESTREAM = 2, UIDOCSTREAM = 4 };

  size_t write(const char* s) { return write(s, strlen(s)); }
  IOStream& operator<<(const char* s) { write(s); return *this; }
  IOStream& operator<<(std::string s) { write(s.data(), s.size()); return *this; }

  static size_t readfn(void* dest, size_t len, void* self) { return static_cast<IOStream*>(self)->read(dest, len); }
  static size_t writefn(const void* src, size_t len, void* self) { return static_cast<IOStream*>(self)->write(src, len); }
  static void seekfn(long offset, int origin, void* self) { static_cast<IOStream*>(self)->seek(offset, origin); }
};

struct MemStream : public IOStream
{
  char* buffer = NULL;
  size_t buffsize = 0;
  size_t capacity = 0;
  size_t pos = 0;

  MemStream() {}
  MemStream(size_t _reserve) { reserve(_reserve); }
  MemStream(const void* src, size_t len, size_t _reserve = 0) : MemStream(std::max(len, _reserve))
    { write(src, len); pos = 0; }
  ~MemStream() override { free(buffer); }
  // probably should implement these for FileStream too
  MemStream(MemStream&& other) : MemStream() { *this = std::move(other); }
  MemStream& operator=(MemStream&& other);

  char* data() { return buffer; }
  char* posdata() { return buffer + pos; }
  size_t possize() const { return buffsize - pos; }
  char* enddata() { return buffer + buffsize; }
  size_t endsize() const { return capacity - buffsize; }
  void reserve(size_t n) { if(n > capacity) { buffer = (char*)realloc(buffer, n); capacity = n;  } }
  void shift(size_t n);

  size_t read(void* dest, size_t len) override
    { len = std::min(len, buffsize - pos); memcpy(dest, &buffer[pos], len); pos += len; return len; }
  size_t write(const void* src, size_t len) override;
  long tell() const override { return (long)pos; }
  bool seek(long offset, int origin = SEEK_SET) override
    { if(origin == SEEK_CUR) { offset += pos; } pos = std::min((size_t)offset, buffsize); return true; }
  bool truncate(size_t len) override
    { buffsize = std::min(buffsize, len); pos = std::min(pos, buffsize); return true; }
  size_t size() const override { return buffsize; }
  size_t readp(void** pdest, size_t len) override
    { *pdest = &buffer[pos]; len = std::min(len, buffsize - pos); pos += len; return len;}
  int type() const override { return MEMSTREAM;  }
};

struct FileStream : public IOStream
{
  FILE* file = NULL;
  std::string filename;
  std::vector<char> buffer;

  FileStream(const char* _filename, const char* mode = "rb+") : filename(_filename) { open(mode); }
  ~FileStream() override { if(file) fclose(file); }

  bool open(const char* mode = "rb+") { file = fopen(filename.c_str(), mode); return file != NULL; }
  bool close() { if(file && fclose(file) == 0) { file = NULL; return true; } return false; }

  bool is_open() const override { return file != NULL; }
  size_t read(void* dest, size_t len) override { return fread(dest, 1, len, file); }
  size_t write(const void* src, size_t len) override { return fwrite(src, 1, len, file); }
  long tell() const override { return ftell(file); }
  bool seek(long offset, int origin = SEEK_SET) override { return fseek(file, offset, origin) == 0; }
  bool flush() override { return fflush(file) == 0; }
  bool truncate(size_t len) override;
  const char* name() const override { return filename.c_str(); }
  size_t size() const override;
  size_t readp(void** pdest, size_t len) override;
  int type() const override { return FILESTREAM; }
};

// would actually make more sense for MemStream to derive from ConstMemStream
struct ConstMemStream : public MemStream
{
  ConstMemStream(const char* src) : ConstMemStream(src, strlen(src)) {}
  ConstMemStream(const void* src, size_t len) { buffer = (char*)src;  buffsize = len; }
  ~ConstMemStream() override { buffer = NULL; }  // prevent call to free() in ~MemStream()
  ConstMemStream& operator=(ConstMemStream&& other) { MemStream::operator=(std::move(other)); return *this; }
  ConstMemStream(ConstMemStream&& other) : MemStream(std::move(other)) {}

  size_t write(const void* src, size_t len) override { return 0; }
  bool truncate(size_t len) override { return false; }
  //int type() const override { return CONST_MEMSTREAM;  }
};

// not quite ready to commit to C++17...
class FSPath
{
public:
  std::string path;

  FSPath() {}
  FSPath(const char* s) : path(s) { normalize(); }
  FSPath(const std::string& s) : FSPath(s.c_str()) {}
  FSPath(const std::string& s, const std::string& t) : FSPath(s + "/" + t) {}  // normalize will remove "//"
  void clear() { path.clear(); }
  bool exists(const char* mode = "rb") const;
  bool isDir() const { return !isEmpty() && path.back() == '/'; }
  bool isRoot() const { return path == "/"; }
  bool isEmpty() const { return path.empty(); }
  bool isAbsolute() const;
  const char* c_str() const { return path.c_str(); }
  bool operator==(const FSPath& other) const { return path == other.path; }
  // path components
  // filePath, fileName refer to name sans any trailing "/", while name() retains trailing "/" if present
  std::string name() const { return path.substr(path.find_last_of('/', path.size() - 2) + 1); }
  std::string filePath() const { return isDir() ? path.substr(0, path.size() - 1) : path; }
  std::string fileName() const { std::string n = name(); if(!n.empty() && isDir()) { n.pop_back(); } return n; }
  std::string baseName() const { std::string base = fileName(); return base.substr(0, base.find_last_of('.')); }
  std::string basePath() const { return path.substr(0, path.find_last_of('.')); }
  std::string extension() const;

  // children and parents
  std::string childPath(const std::string& s) const
    { return (path.empty() || path.back() == '/') ? (path + s) : (path + "/" + s); }
  FSPath child(const std::string& s) const { return FSPath(childPath(s)); }
  std::string parentPath() const
    { return isRoot() ? "" : path.substr(0, path.find_last_of('/', path.size() - 2) + 1); }
  FSPath parent() const { return FSPath(parentPath()); }
  FSPath dir() const { return parent(); }
  std::string relativeTo(FSPath base);
private:
  void normalize();
};

// TODO: use string_views (C++17) instead of string for these
Timestamp getFileMTime(const FSPath& filename);
long getFileSize(const FSPath& filename);
std::vector<std::string> lsDirectory(const FSPath& name);
bool createDir(const std::string& dir);
bool createPath(const FSPath& pathname);
bool removeDir(const FSPath& path, bool rmtopdir = true);
bool copyFile(FSPath src, FSPath dest);
bool moveFile(FSPath src, FSPath dest);
bool removeFile(const std::string& name);
bool isDirectory(const char* path);
std::string canonicalPath(const FSPath& path);
std::string getCwd();
bool truncateFile(const char* filename, size_t len);
std::string sysExec(const char* cmd);
std::string toValidFilename(std::string s, char replacement = '_');

// read an entire file into a container - supports vector<char> or string
// using C fns is faster than ifstream!
template<class Container>
bool readFile(Container* buff, const char* filename)
{
  FILE* f = fopen(filename, "rb");
  if(!f)
    return false;
  long bytesread = 0;
  // obtain file size
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  // ftell returns -1 on error (e.g. if file is a directory)
  if(size > 0) {
    rewind(f);
    // insert after any existing contents
    size_t offset = buff->size();
    buff->resize(offset + size);
    // C++11 guarantees this will work for std::string
    bytesread = fread(&(*buff)[offset], 1, size, f);
  }
  fclose(f);
  return bytesread == size;
}

std::string readFile(const char* filename);

#endif

#ifdef FILEUTIL_IMPLEMENTATION
#undef FILEUTIL_IMPLEMENTATION

#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>

MemStream& MemStream::operator=(MemStream&& other)
{
  std::swap(buffer, other.buffer);
  std::swap(buffsize, other.buffsize);
  std::swap(capacity, other.capacity);
  std::swap(pos, other.pos);
  return *this;
}

void MemStream::shift(size_t n)
{
  n = std::min(n, buffsize);
  if(n > 0 && n < buffsize)
    memmove(buffer, buffer + n, buffsize - n);
  buffsize -= n;
  pos -= std::min(n, pos);
}

size_t MemStream::write(const void* src, size_t len)
{
  if(pos + len > capacity)
    reserve(std::max(pos + len, capacity*2));  // exponential growth
  memcpy(&buffer[pos], src, len);
  pos += len;
  buffsize = std::max(buffsize, pos);
  return len;
}

// writable = file && mode && mode[0] && (mode[0] != 'r' || mode[1] == '+' || (mode[1] && mode[2] == '+'));
bool FileStream::truncate(size_t len)
{
  // reopen(NULL, ...) doesn't work on Windows, so just close and open
  if(len == 0)
    return close() && open("wb+");   //freopen(NULL, "wb+", file) != NULL;
  return close() && truncateFile(filename.c_str(), len) && open();
}

size_t FileStream::size() const
{
  if(!file)
    return SIZE_MAX;
  long pos = ftell(file);
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, pos, SEEK_SET);
  return size >= 0 && size < LONG_MAX ? (size_t)size : SIZE_MAX;  // seems we get LONG_MAX for directory on Linux!
}

size_t FileStream::readp(void** pdest, size_t len)
{
  if(len > 0xFFFF)
    len = std::min(len, size() - (size_t)tell());
  buffer.resize(len);
  *pdest = &buffer[0];
  return read(*pdest, len);
}

bool copyFile(FSPath src, FSPath dest)
{
  std::ifstream src_strm(PLATFORM_STR(src.c_str()), std::ios::binary);
  std::ofstream dest_strm(PLATFORM_STR(dest.c_str()), std::ios::binary);
  if(!dest_strm || !src_strm)
    return false;
  dest_strm << src_strm.rdbuf();
  return true;
}

std::string FSPath::extension() const
{
  std::string base = fileName();
  std::string ext = base.substr(base.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

void FSPath::normalize()
{
#if PLATFORM_WIN
  std::replace(path.begin(), path.end(), '\\', '/');
#endif
  size_t n = 0;
  while((n = path.find("//", n)) != std::string::npos)
    path.replace(n, 2, "/");
}

bool FSPath::isAbsolute() const
{
#if PLATFORM_WIN
  return path.size() > 1 && path[1] == ':';
#else
  return path.size() > 0 && path[0] == '/';
#endif
}

// mode specifies open mode - default of "rb" checks for existence; "r+" can be used to check for writability
bool FSPath::exists(const char* mode) const
{
  if(isEmpty())
    return false;
  // on Android, stat succeeds even if we don't have permission, while fopen fails
#if PLATFORM_WIN
  if(isDir())
    return GetFileAttributes(PLATFORM_STR(path.c_str())) != INVALID_FILE_ATTRIBUTES;
#endif
  // apparently fopen works with directory on Unix as long as mode is read-only
  FILE* f = fopen(path.c_str(), mode);
  if(f != NULL)
    fclose(f);
  return f != NULL;
}

Timestamp getFileMTime(const FSPath& filename)
{
  struct stat result;
  if(stat(filename.c_str(), &result) == 0)
    return result.st_mtime;
  return 0;
}

long getFileSize(const FSPath& filename)
{
  FILE* f = fopen(filename.c_str(), "rb");
  if(!f)
    return -1;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);
  return size;
}

bool createPath(const FSPath& path)
{
  if(path.isEmpty()) {
    ASSERT(0 && "createPath() passed empty path!");
    return false;
  }
  return path.exists() || (createPath(path.parentPath().c_str()) && createDir(path.path));
}

bool isDirectory(const char* path)
{
  struct stat result;
  if(stat(path, &result) == 0)
    return result.st_mode & S_IFDIR;  // S_ISDIR(result.st_mode) -- not avail on Windows
  return false;
}

std::string canonicalPath(const FSPath& path)
{
  if(path.isEmpty()) return "";
  FSPath p(path.isAbsolute() ? path.path : getCwd() + "/" + path.path);
  std::vector<std::string> dirs;
  while(!p.isRoot() && !p.isEmpty()) {
    dirs.emplace_back(p.fileName());  // drop trailing '/'
    p = p.parent();
  }
  // reassemble - p will be empty on Windows and root ('/') on other platforms
  while(!dirs.empty()) {
    if(dirs.back() == "..")
      p = p.parent();
    else if(dirs.back() != ".")
      p = p.child(dirs.back());
    dirs.pop_back();
  }
  // if path passed by caller specifies directory, or path exists and is directory, append '/'
  if(!p.isDir() && (path.isDir() || isDirectory(p.c_str())))
    p.path.push_back('/');
  return p.path;
}

std::string FSPath::relativeTo(FSPath base)
{
  FSPath p(isAbsolute() ? path : getCwd() + "/" + path);
  std::vector<std::string> pdirs;
  while(!p.isRoot() && !p.isEmpty()) {
    pdirs.emplace_back(p.fileName());  // drop trailing '/'
    p = p.parent();
  }

  FSPath b(base.isAbsolute() ? base.path : getCwd() + "/" + base.path);
  std::vector<std::string> bdirs;
  while(!b.isRoot() && !b.isEmpty()) {
    bdirs.emplace_back(b.fileName());  // drop trailing '/'
    b = b.parent();
  }

  while(!bdirs.empty() && !pdirs.empty() && bdirs.back() == pdirs.back()) {
    bdirs.pop_back();
    pdirs.pop_back();
  }
  std::string res;
  for(; !bdirs.empty(); bdirs.pop_back()) res.append("../");
  for(; !pdirs.empty(); pdirs.pop_back()) res.append(pdirs.back()).append("/");
  if(!isDir()) res.pop_back();  // remove trailing '/'
  return res;
}

// for now, just use most restrictive set of disallowed chars (Windows/Android)
std::string toValidFilename(std::string s, char replacement)
{
  size_t pos = 0;
  while((pos = s.find_first_of("<>:|?*\"\\/", pos)) != std::string::npos)
    s[pos++] = replacement;
  return s;
}

std::string readFile(const char* filename)
{
  std::string s;
  readFile(&s, filename);
  return s;
}

#if !PLATFORM_WIN
#include <dirent.h>
#include <unistd.h>

std::vector<std::string> lsDirectory(const FSPath& name)
{
  std::vector<std::string> v;
  DIR* dirp = opendir(name.c_str());
  if(!dirp)
    return v;
  struct dirent* dp;
  while((dp = readdir(dirp)) != NULL) {
    if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
      continue;
    v.emplace_back(dp->d_name);
    if(dp->d_type & DT_DIR)
      v.back().push_back('/');
    else if(dp->d_type == DT_UNKNOWN || dp->d_type == DT_LNK) {
      struct stat s;
      if(stat(name.childPath(v.back()).c_str(), &s) == 0 && S_ISDIR(s.st_mode))
        v.back().push_back('/');
    }
  }
  closedir(dirp);
  return v;
}

bool createDir(const std::string& dir)
{
  return mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != -1;  // mode 775
}

// main motivation for this is to allow actually moving file to be disabled for testing
bool moveFile(FSPath src, FSPath dest)
{
  // rename across filesystems fails on linux w/ errno == EXDEV
  return rename(src.c_str(), dest.c_str()) == 0 ||
      (errno == EXDEV && copyFile(src, dest) && removeFile(src.c_str()));
}

bool removeFile(const std::string& name)
{
  return remove(name.c_str()) == 0;
}

std::string getCwd()
{
  char cwdbuff[1024];
  return std::string(getcwd(cwdbuff, 1024));
}

bool truncateFile(const char* filename, size_t len)
{
  return truncate(filename, len) == 0;
}

#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>  // for SHFileOperation
#include <io.h>  // for _chsize_s
// for exec
#define popen _popen
#define pclose _pclose

// Also see github.com/tronkko/dirent and github.com/vurtun/nuklear/blob/master/example/file_browser.c
std::vector<std::string> lsDirectory(const FSPath& name)
{
  std::vector<std::string> v;
  std::string pattern(name.c_str());
  if(pattern.back() != '/' && pattern.back() != '\\')
    pattern.push_back('/');
  pattern.push_back('*');
  WIN32_FIND_DATA data;
  HANDLE hFind;
  if((hFind = FindFirstFile(utf8_to_wstr(pattern.c_str()).data(), &data)) != INVALID_HANDLE_VALUE) {
    do {
      if(data.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_HIDDEN))
        continue;
      std::string filename = wstr_to_utf8(data.cFileName);
      if(filename == "." || filename == "..")
        continue;
      if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        filename.push_back('/');
      v.push_back(filename);
    } while(FindNextFile(hFind, &data) != 0);
    FindClose(hFind);
  }
  return v;
}

std::vector<std::string> lsDrives()
{
  std::vector<std::string> names;
  WCHAR volname[MAX_PATH];
  char drivename[] = "_:\\";
  DWORD drivebits = GetLogicalDrives();
  for(int ii = 0; ii < 26; ++ii, drivebits >>= 1) {
    if(!(drivebits & 1))
      continue;
    drivename[0] = 'A' + char(ii);
    bool ok = GetVolumeInformation(PLATFORM_STR(drivename), volname, MAX_PATH, NULL, NULL, NULL, NULL, 0);
    if(ok && volname[0])
      names.push_back(std::string(drivename, 2) + " " + wstr_to_utf8(volname));
    else
      names.push_back(std::string(drivename, 2));
  }
  return names;
}

bool createDir(const std::string& dir)
{
  return CreateDirectory(PLATFORM_STR(dir.c_str()), NULL);
}

bool moveFile(FSPath src, FSPath dest)
{
  // moveFileEx (nor _wrename) cannot move folders between drives
  if(src.isDir() && src.path[0] != dest.path[0]) {
    // SHFileOperation fails w/ error 0x7B if forward slashes are used
    std::replace(src.path.begin(), src.path.end(), '/', '\\');
    std::replace(dest.path.begin(), dest.path.end(), '/', '\\');
    // SHFileOperation requires double \0 terminated strings
    std::wstring ssrc = utf8_to_wstr(src.c_str());
    std::wstring sdest = utf8_to_wstr(dest.c_str());
    ssrc.push_back(0);
    sdest.push_back(0);
    SHFILEOPSTRUCT s = { 0 };
    s.hwnd = NULL;
    s.wFunc = FO_MOVE;
    s.fFlags = FOF_NO_UI;
    s.pTo = sdest.c_str();
    s.pFrom = ssrc.c_str();
    int res = SHFileOperation(&s);
    return res == 0;
  }
  return MoveFileEx(PLATFORM_STR(src.c_str()), PLATFORM_STR(dest.c_str()), MOVEFILE_COPY_ALLOWED) != 0;
}

bool removeFile(const std::string& name)
{
  return _wremove(PLATFORM_STR(name.c_str())) == 0;
}

std::string getCwd()
{
  wchar_t cwdbuff[1024];
  GetCurrentDirectory(1024, cwdbuff);
  return wstr_to_utf8(cwdbuff);
}

bool truncateFile(const char* filename, size_t len)
{
  bool ok = false;
  FILE* f = fopen(filename, "rb+");
  if(f) {
    ok = _chsize_s(fileno(f), len) == 0;
    fclose(f);
  }
  return ok;
}

#include <locale>         // std::wstring_convert
#include <codecvt>        // std::codecvt_utf8

std::wstring utf8_to_wstr(const char* str8)
{
  static std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;
  return cv.from_bytes(str8);
}

std::string wstr_to_utf8(wchar_t* wstr)
{
  static std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;
  return cv.to_bytes(wstr);
}
#endif

// `rm -r <path>`
bool removeDir(const FSPath& path, bool rmtopdir)
{
  ASSERT(path.path.size() > 1 && path.path != "..");
  auto contents = lsDirectory(path);
  bool ok = true;

  for(const std::string& child : contents) {
    FSPath childpath = path.child(child);
    ok = (childpath.isDir() ? removeDir(childpath) : removeFile(childpath.path)) && ok;
  }
#if PLATFORM_WIN
  return rmtopdir ? (RemoveDirectory(PLATFORM_STR(path.c_str())) != 0 && ok) : ok;
#else
  return rmtopdir ? (removeFile(path.path) && ok) : ok;
#endif
}

// run command and get stdout
std::string sysExec(const char* cmd)
{
  char buffer[1024];
  std::string result = "";
  FILE* pipe = popen(cmd, "r");
  if(pipe) {
    while(fgets(buffer, sizeof(buffer), pipe))
      result += buffer;
    pclose(pipe);
  }
  return result;
}

#endif
