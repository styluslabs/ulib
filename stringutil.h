#ifndef STRINGUTIL_H
#define STRINGUTIL_H

#include <limits.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <random>

#include "stb_sprintf.h"

// Convert define to actual string; the second define is indeed necessary to extract the value!
#define QUOTE(x) #x
#define PPVALUE_TO_STRING(x) QUOTE(x)

// more compiler fixups
#ifdef _MSC_VER
#define strtoull _strtoui64
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

// split std::string on delimiter - http://stackoverflow.com/questions/236129/how-to-split-a-string-in-c

//template<template<class> class T> T<std::string>
template<template<class, class...> class Container, class... Container_Params>
Container<std::string, Container_Params... > splitStr(const char* s, char delim, bool skip_empty = false)
{
  Container<std::string, Container_Params... > elems;
  std::stringstream ss(s);
  std::string item;
  while(std::getline(ss, item, delim))
    if(!skip_empty || !item.empty())
      elems.insert(elems.end(), item);  //elems.push_back(item);  -- insert() works for all container types
  return elems;
}

template<template<class, class...> class Container, class... Container_Params>
Container<std::string, Container_Params... > splitStr(std::string s, const char* delims, bool skip_empty = false)
{
  Container<std::string, Container_Params... > elems;
  std::string item;
  size_t start = 0, end = 0;
  while((end = s.find_first_of(delims, start)) != std::string::npos) {
    if(!skip_empty || end > start)
      elems.insert(elems.end(), s.substr(start, end-start));
    start = end + 1;
  }
  if(start < s.size())
    elems.insert(elems.end(), s.substr(start));
  return elems;
}

template<template<class, class...> class Container, class... Container_Params>
Container<char*, Container_Params... > splitStrInPlace(char* str, const char* sep)
{
  Container<char*, Container_Params... > res;
  int seplen = strlen(sep);
  while(1) {
    res.insert(res.end(), str);  //res.push_back(str);
    str = strstr(str, sep);
    if(!str)
      break;
    *str = '\0';
    str += seplen;
  }
  return res;
}

// accept vector of anything that can be written to stringstream
template<typename T>
std::string joinStr(const std::vector<T>& strs, const char* sep)
{
  std::stringstream ss;
  if(!strs.empty())
    ss << strs[0];
  for(size_t ii = 1; ii < strs.size(); ++ii)
    ss << sep << strs[ii];
  return ss.str();
}

// optimized specialization for std::string
template<>
std::string joinStr(const std::vector<std::string>& strs, const char* sep);

std::string fstring(const char* fmt, ...);
//inline std::string fstring(const char* fmt) { return fmt;  }

inline std::string toLower(std::string str)
{
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  return str;
}

inline bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// LLVM StringRef: http://llvm.org/docs/doxygen/html/StringRef_8h_source.html
class StringRef
{
public:
  // might be better to use end pointer instead of len
  const char* str;
  size_t len;

  StringRef() : str(NULL), len(0) {}
  StringRef(const char* s) : str(s), len(s ? strlen(s) : 0) {}
  StringRef(const char* s, size_t l) : str(s), len(l) {}
  StringRef(const char* s, int offset, size_t l) : str(s + offset), len(l) {}
  StringRef(const std::string& s) : str(s.c_str()), len(s.size()) {}
  const char* data() const { return str; }
  const char* constData() const { return str; }
  const char* begin() const { return str; }
  const char* end() const { return str + len; }
  size_t length() const { return len; }
  size_t size() const { return len; }
  bool isEmpty() const { return len == 0; }
  bool startsWith(const char* prefix) const
    { size_t plen = strlen(prefix); return len >= plen && strncmp(str, prefix, plen) == 0; }
  bool endsWith(const char* suffix) const
    { size_t slen = strlen(suffix); return len >= slen && strncmp(str + (len - slen), suffix, slen) == 0; }
  int find(const char* substr, int start = 0) const
  {
    int slen = strlen(substr);
    for(int ii = start; ii <= (int)len - slen; ++ii) {
      if(str[ii] == substr[0] && strncmp(str + ii, substr, slen) == 0)
        return ii;
    }
    return -1;
  }
  bool contains(const char* substr) const { return find(substr) >= 0; }
  int findFirstOf(const char* chars, int start = 0)
  {
    for(int ii = start; ii <= (int)len; ++ii) {
      for(const char* scanp = chars; *scanp; ++scanp) {
        if(*scanp == str[ii])
          return ii;
      }
    }
    return -1;
  }

  StringRef& advance(int inc) { if(inc > 0) { str += inc; len -= inc; } return *this; }
  StringRef& operator+=(int inc) { str += inc; len -= inc; return *this; }
  StringRef& operator++() { str += 1; len -= 1; return *this; }
  const char& at(size_t idx) const { return str[idx]; }
  const char& operator[](size_t idx) const { return str[idx]; }
  const char& operator*() const { return *str; }
  const char& front() const { return str[0]; }
  const char& back() const { return str[len - 1]; }

  StringRef& chop(size_t n) { len -= std::min(len, n); return *this; }
  StringRef& slice(size_t pos, size_t n = SIZE_MAX)  // substr in-place
    { str += std::min(pos, len); len = (pos >= len) ? 0 : std::min(len - pos, n); return *this; }
  StringRef& trimL() { while(len > 0 && isSpace(str[0])) { ++str; --len; } return *this; }
  StringRef& trimR() { while(len > 0 && isSpace(str[len - 1])) { --len; } return *this; }
  StringRef trimmed() const { return StringRef(*this).trimL().trimR(); }
  StringRef substr(size_t pos, size_t n = SIZE_MAX) const
    { return StringRef(str + pos, (pos >= len ? 0 : std::min(len - pos, n))); }
  std::string toString() const { return std::string(str, len); }
  char* toBuff(char* dest) const { dest[len] = '\0'; return strncpy(dest, str, len); }
  // const char* c_str(char* buff) const { if(!str[len]) return str; strncpy(buff, str, len); buff[len] = '\0'; return buff; }

  friend StringRef operator+(const StringRef& ref, int inc) { return StringRef(ref) += inc; }
  friend bool operator==(const StringRef& ref, const char* other)
    { return ref.len == strlen(other) && strncmp(ref.str, other, ref.len) == 0; }
  friend bool operator==(const StringRef& ref, const StringRef& other)
    { return ref.len == other.len && strncmp(ref.str, other.str, ref.len) == 0; }
  friend bool operator!=(const StringRef& ref, const char* other) { return !operator==(ref, other); }
  friend bool operator!=(const StringRef& ref, const StringRef& other) { return !operator==(ref, other); }
};

std::vector<StringRef> splitStringRef(const StringRef& strRef, const char* sep, bool skipEmpty = false);
std::vector<StringRef> splitStringRef(const StringRef& strRef, char sep = ' ', bool skipEmpty = false);
const char* findWord(const char* str, const char* word, char sep = ' ');
inline bool containsWord(const char* str, const char* word, char sep = ' ') { return findWord(str, word, sep) != NULL; }
std::string addWord(std::string s, std::string w, char sep = ' ');
std::string removeWord(std::string s, std::string w, char sep = ' ');
char* strNstr(const char* s, const char* substr, size_t len);
std::string urlEncode(const char* s);

// base64
constexpr size_t base64_enclen(size_t len) { return 4 * ((len + 2) / 3); }
char* base64_encode(const unsigned char* data, size_t len, char* dest);
std::string base64_encode(const unsigned char* data, size_t len);
std::vector<unsigned char> base64_decode(const char* data, size_t len);
inline std::string base64_encode(const std::string& str) { return base64_encode((unsigned char*)str.data(), str.size()); }
inline std::string base64_encode(const std::vector<unsigned char>& str) { return base64_encode(str.data(), str.size()); }
inline std::vector<unsigned char> base64_decode(const std::string& str) { return base64_decode(str.data(), str.size()); }

inline std::string trimStr(const std::string& s)
{
  return StringRef(s).trimmed().toString();
}

template<int N>
static int indexOfStr(const StringRef& value, const char* const (&strs)[N])
{
  int ii = 0;
  for(const char* str : strs) {
    if(str == value)
      return ii;
    ++ii;
  }
  return -1;
}

// strToReal and realToStr are templates to support both float and double
// from http://www.leapsecond.com/tools/fast_atof.c
template<typename Real>
static Real strToReal(const char *p, char** endptr)
{
  Real sign, value;

  // Skip leading white space, if any.
  while(isSpace(*p) ) {
    p += 1;
  }

  // Get sign, if any.
  sign = 1.0;
  if(*p == '-') {
    sign = -1.0;
    p += 1;
  }
  else if (*p == '+') {
    p += 1;
  }

  // Get digits before decimal point or exponent, if any.
  for (value = 0.0; isDigit(*p); p += 1) {
    value = value * 10.0 + (*p - '0');
  }

  // Get digits after decimal point, if any.
  if(*p == '.') {
    Real pow10 = 0.1;
    p += 1;
    while (isDigit(*p)) {
      value += (*p - '0') * pow10;
      pow10 *= 0.1;
      p += 1;
    }
  }

  // Handle exponent, if any.
  if((*p == 'e') || (*p == 'E')) {
    int frac = 0;
    Real scale = 1.0;
    unsigned int expon;
    // Get sign of exponent, if any.
    p += 1;
    if(*p == '-') {
      frac = 1;
      p += 1;
    }
    else if(*p == '+') {
      p += 1;
    }
    // Get digits of exponent, if any.
    for(expon = 0; isDigit(*p); p += 1) {
      expon = expon * 10 + (*p - '0');
    }
    if (expon > 308) expon = 308;
    // Calculate scaling factor.
    while(expon >= 50) { scale *= 1E50; expon -= 50; }
    while(expon >=  8) { scale *= 1E8;  expon -=  8; }
    while(expon >   0) { scale *= 10.0; expon -=  1; }
    value = frac ? (value / scale) : (value * scale);
  }

  if(endptr)
    *endptr = (char*)p;
  // Return signed and scaled floating point result.
  return sign * value;  // this is actually faster than (negative ? -value : value)
}

// We should probably have intToStr and realToStr add '\0' terminators
// std::tostring calls sprintf ... I think this should be a bit faster
template<typename Int>
int intToStr(char* str, Int x)
{
  bool negative = x < 0;
  if(negative)
    x = -x;

  int ii = 0;
  do {
    str[ii++] = '0' + char(x % 10);
    x /= 10;
  } while(x > 0);
  if(negative)
    str[ii++] = '-';
  std::reverse(str, str+ii);
  return ii;
}

template<typename Real>
int realToStr(char* str, Real f, int prec)
{
  static const Real powers_of_10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

  // we could of course extend range and precision we handle by using 64-bit ints
  if(!(f < INT_MAX && f > -INT_MAX && prec < 10))  // handles NaN, since all comparisons with NaN return false
    return stbsp_sprintf(str, "%.*f", prec, f);  // let the professionals handle this one

  bool negative = f < 0;
  if(negative)
    f = -f;
  // separate integer and decimal part; scale and round decimal part based on requested precision
  int whole = int(f);
  int frac = int((f - whole) * powers_of_10[prec] + Real(0.5));
  int ii = 0;
  if(frac != 0) {
    // skip trailing zeros; separate while()s appears to be faster for random numbers
    while(prec > 0 && frac % 10 == 0) { frac /= 10; --prec; }
    while(ii < prec) {
      str[ii++] = '0' + char(frac % 10);
      frac /= 10;
    }
    if(ii > 0)
      str[ii++] = '.';
  }
  else if(whole == 0) {
    str[ii] = '0';  // main reason for this is to prevent "-0"
    return 1;
  }
  if(frac >= 1)  // this is needed for, e.g., 1.9999999999999
    whole += 1;
  // print at least one digit before decimal
  do {
    str[ii++] = '0' + char(whole % 10);
    whole /= 10;
  } while(whole > 0);
  // print minus sign if negative
  if(negative)
    str[ii++] = '-';
  std::reverse(str, str+ii);
  return ii;
}

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12
unsigned int decode_utf8(unsigned int* state, unsigned int* codep, unsigned char _byte);

// replacement for C rand()
void srandpp(unsigned int s);
unsigned int randpp();
#define RANDPP_MAX UINT_MAX
std::string randomStr(const unsigned int len);

#endif  // header section

#ifdef STRINGUTIL_IMPLEMENTATION
#undef STRINGUTIL_IMPLEMENTATION

#ifndef STRINGUTIL_NO_STB_IMPL
// unaligned access crashes on 32-bit ARM (Android) and seems like a bad idea anyway
#define STB_SPRINTF_NOUNALIGNED
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"
#endif

static char* stb_sprintfcb(const char* buf, void* user, int len)
{
  static_cast<std::string*>(user)->append(buf, len);
  return (char*)buf;  // stb_sprintf always calls this with base of buf passed to it
}

// template<class... Args>  std::string fstring(fmt, Args&&... args) -  fn(fmt, std::forward<Args>(args)...)
std::string fstring(const char* fmt, ...)
{
#ifdef STB_SPRINTF_MIN
  // standard snprintf always returns number of bytes needed to print entire string, whereas stbsp_snprintf
  //  only returns this if passed buf = 0 and count = 0 (otherwise returns actual number written), so we
  //  instead use the callback version, stbsp_vsprintfcb (extra copy, but saves memory)
  char buf[STB_SPRINTF_MIN];
  std::string str;
  va_list va;
  va_start(va, fmt);
  stbsp_vsprintfcb(stb_sprintfcb, &str, buf, fmt, va);
  va_end(va);
  return str;
#else
  char buf[512];
  buf[0] = '\0';
  va_list va, va2;
  va_start(va, fmt);
  va_copy(va2, va);
  int n = vsnprintf(buf, 512, fmt, va);
  std::string str;
  if(n < 512)
    str = buf;
  else {
    str.resize(n);
    vsnprintf(&str[0], n+1, fmt, va2);
  }
  va_end(va2);
  va_end(va);
  return str;
#endif
}

const char* findWord(const char* str, const char* word, char sep)
{
  size_t l = 0;
  const char* s = str;
  for(;;) {
    s = strstr(s, word);
    if(!s || !word[0])  // prevent infinite loop if empty string passed
      return NULL;
    if(!l)
      l = strlen(word);
    if((s == str || s[-1] == sep) && (s[l] == '\0' || s[l] == sep))
      return s;
    s += l;  // we found a subword match at s, thus s+l is not past end of string
  }
}

// TODO: accept const char* instead of std::string?
std::string addWord(std::string s, std::string w, char sep)
{
  if(s.empty())
    return w;
  return containsWord(s.c_str(), w.c_str(), sep) ? s : s.append(1, sep).append(w);
}

std::string removeWord(std::string s, std::string w, char sep)
{
  const char* ss = s.c_str();
  const char* hit = findWord(ss, w.c_str(), sep);
  if(hit) {
    size_t pos = hit - ss;
    s.erase(pos > 0 ? pos - 1 : pos, w.size() + 1);  // +1 to include separator
  }
  return s;
}

char* strNstr(const char* s, const char* substr, size_t len)
{
  size_t sublen = strlen(substr);
  if(!sublen) return (char*)s;
  for(const char* t = s; t <= (s + len) - sublen; ++t) {
    if(*t == *substr && strncmp(t, substr, sublen) == 0)
      return (char*)t;
  }
  return NULL;
}

// don't make this a member of StringRef so that StringRef doesn't require inclusion of std::vector
std::vector<StringRef> splitStringRef(const StringRef& strRef, char sep, bool skipEmpty)
{
  std::vector<StringRef> lst;
  const char* str = strRef.constData();
  const char* end = str + strRef.size();
  while(str < end) {
    const char* start = str;
    while(str < end && *str != sep) ++str;
    if(str - start > 0 || !skipEmpty)
      lst.emplace_back(start, str - start);
    ++str;
  }
  return lst;
}

// TODO: get rid of the char delimiter version above
std::vector<StringRef> splitStringRef(const StringRef& strRef, const char* sep, bool skipEmpty)
{
  std::vector<StringRef> lst;
  const char* str = strRef.constData();
  int start = 0;
  while(start < int(strRef.size())) {
    int stop = strRef.find(sep, start);
    if(stop < 0)
      stop = strRef.size();
    if(stop - start > 0 || !skipEmpty)
      lst.emplace_back(str + start, stop - start);
    start = stop + strlen(sep);
  }
  return lst;
}

template<>
std::string joinStr(const std::vector<std::string>& strs, const char* sep)
{
  if(strs.size() < 2) { return strs.empty() ? std::string() : strs[0]; }
  size_t tot = (strs.size() - 1)*strlen(sep);
  for(auto& s : strs) { tot += s.size(); }
  std::string res;
  res.reserve(tot);
  res.append(strs[0]);
  for(size_t ii = 1; ii < strs.size(); ++ii) {
    res.append(sep).append(strs[ii]);
  }
  return res;
}

std::string urlEncode(const char* s)
{
  std::string res;
  for(; *s; ++s)
    (isalnum(*s) || *s == '-' || *s == '_' || *s == '.' || *s == '~') ? (res += *s) : (res += fstring("%%%2X", *s));
  return res;
}

// UTF-8 decoder - cut and paste from fontstash.h
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
unsigned int decode_utf8(unsigned int* state, unsigned int* codep, unsigned char _byte)
{
  static const unsigned char utf8d[] = {
    // The first part of the table maps bytes to character classes that
    // to reduce the size of the transition table and create bitmasks.
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // The second part is a transition table that maps a combination
    // of a state of the automaton and a character class to a state.
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
  };

  unsigned int byte = _byte;
  unsigned int type = utf8d[byte];
  *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3fu) | (*codep << 6) : (0xff >> type) & (byte);
  *state = utf8d[256 + *state + type];
  return *state;
}

// rand() isn't thread-safe either, so don't bother with thread_local for now
static /* thread_local */ std::mt19937 randGen;

void srandpp(unsigned int s) { randGen.seed(s); }
// technically, rand()%N is slightly biased, but it's fine for our purposes; std::uniform_int_distribution
//  is implementation dependent, which defeats the whole reason I'm replacing rand()
unsigned int randpp() { return randGen(); }  // or >> 1?

// generate a random string
std::string randomStr(const unsigned int len)
{
  static const char alphanum[] = "0123456789" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz";

  std::string s(len, 'x');
  for(unsigned int ii = 0; ii < len; ++ii)
    s[ii] = alphanum[randpp() % (sizeof(alphanum) - 1)];
  return s;
}

// base64 encode/decode:
// based on https://github.com/gaspardpetit/base64/blob/master/src/ManuelMartinez/ManuelMartinez.h
// modified to skip whitespace (and other invalid chars) when decoding
static const char base64enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64_encode(const unsigned char* data, size_t len, char* dest)
{
  char* out = dest;
  unsigned int val = 0;
  int valb = -6;
  for (const unsigned char *cptr = data; cptr < data + len; ++cptr) {
    val = (val << 8) + *cptr;  // we could do val & 0x00FFFFFF to shut up ubsan - would be optimized out
    valb += 8;
    while (valb >= 0) {
      *out++ = base64enc[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }
  if (valb > -6) *out++ = base64enc[((val << 8) >> (valb + 8)) & 0x3F];
  while ((out - dest) % 4) *out++ = '=';
  //ASSERT(out - dest == enclen);
  return dest;
}

std::string base64_encode(const unsigned char* data, size_t len)
{
  std::string outstr(base64_enclen(len), '\0');
  base64_encode(data, len, &outstr[0]);
  return outstr;
}

std::vector<unsigned char> base64_decode(const char* data, size_t len)
{
  static std::vector<int> base64dec(256, -1);
  if(base64dec['A'] == -1) { for (int ii = 0; ii < 64; ++ii) base64dec[base64enc[ii]] = ii; }

  std::vector<unsigned char> strout(((len + 2)/4)*3, '\0');
  unsigned char* out = &strout[0];
  unsigned int val = 0;
  int valb = -8;
  for (size_t ii = 0; ii < len; ++ii) {
    char c = data[ii];
    int d = base64dec[c];
    if (d == -1) continue;  // skip invalid chars
    val = (val << 6) + (unsigned int)d;
    valb += 6;
    if (valb >= 0) {
      *out++ = (val >> valb) & 0xFF;
      valb -= 8;
    }
  }
  //ASSERT(out - &strout[0] <= strout.size());
  strout.resize(out - &strout[0]);
  return strout;
}

// sprintf uses bignum library for printing large floats - I don't believe there is any way to get the same
//  result more simply.  However, for numbers |x| < 2^53 (for version using fmod), realToStr seems to
//  match sprintf except for cases involving 0.499.... vs. 0.5
#if defined(STRINGUTIL_TEST_REALTOSTR) || defined(STRINGUTIL_PERF_REALTOSTR)
#define PLATFORMUTIL_IMPLEMENTATION
#include "platformutil.h"
#endif

// g++ -x c++ -O2 -I../stb -DSTRINGUTIL_TEST_REALTOSTR -DSTRINGUTIL_IMPLEMENTATION -o run_stringutil stringutil.h
#ifdef STRINGUTIL_TEST_REALTOSTR
int main(int argc, char* argv[])
{
  char s1[1024];
  char s2[1024];

  srand(time(NULL));
  PLATFORM_LOG("Running realToStr test\n");
  for(int ii = 0; ii < 100000000; ++ii) {
    double f = (ii % 2 ? 1 : -1) * double(rand())/double(rand()); //pow(rand(), Dim(rand())/Dim(rand()));
    sprintf(s1, "%.9f", f);
    int len1 = strlen(s1);
    int len2 = realToStr(s2, f, 9);
    // account for trailing zeros
    while(s1[--len1] == '0') s1[len1] = '\0';
    if(s1[len1] == '.') s1[len1] = '\0';
    //while(len2 < len1) s2[len2++] = '0';
    s2[len2] = '\0';
    if(strcmp(s1, s2) != 0) {
      PLATFORM_LOG("Mismatch for %.32f: sprintf = %s, dimToStr = %s\n", f, s1, s2);
    }
  }
  PLATFORM_LOG("realToStr test completed");
}
#elif defined(STRINGUTIL_PERF_REALTOSTR)
int main(int argc, char* argv[])
{
  char s1[1024];
  PLATFORM_LOG("Running realToStr perf test\n");
  Timestamp t0 = mSecSinceEpoch();
  for(double f = -10000.0; f < 10000.0; f += 0.0001) {
    realToStr(s1, f, 3);
  }
  PLATFORM_LOG("realToStr: %d ms", mSecSinceEpoch() - t0);
}
#endif

// g++ -x c++ -I../stb -DSTRINGUTIL_TEST_BASE64 -DSTRINGUTIL_IMPLEMENTATION -o base64test stringutil.h
#ifdef STRINGUTIL_TEST_BASE64

#define PLATFORMUTIL_IMPLEMENTATION
#include "platformutil.h"

std::string randomData(size_t len)
{
  std::string s(len, '\0');
  for(size_t ii = 0; ii < len; ++ii)
    s[ii] = (char)(randpp() % 256);
  return s;
}

int main(int argc, char* argv[])
{
  srandpp(89);

  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhcw==") == "any simple pleas");
  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhc3U=") == "any simple pleasu");
  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhc3Vy") == "any simple pleasur");

  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhcw") == "any simple pleas");
  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhc3U") == "any simple pleasu");
  ASSERT(base64_decode("YW55IHNpbXBsZSBwbGVhc3Vy") == "any simple pleasur");

  for(int n = 10; n < 1000; ++n) {
    std::string s = randomData(n);
    std::string senc = base64_encode(s);
    ASSERT(base64_decode(senc) == s);
    senc.insert(randpp()%n, randpp()%6, ' ');
    senc.insert(senc.size() - randpp()%6, randpp()%6, ' ');
    senc.insert(randpp()%6, randpp()%6, ' ');
    //PLATFORM_LOG(senc.c_str());
    //PLATFORM_LOG("<end>\n\n");
    ASSERT(base64_decode(senc) == s);
  }
  return 0;
}

#endif

#endif
