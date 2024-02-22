#ifndef UTRACE_H
#define UTRACE_H

#ifdef UTRACE_ENABLE

// for fancier profiling, we could have this use https://github.com/Celtoys/Remotery
//  or https://github.com/jonasmr/microprofile
struct Tracer
{
  static std::string buff;
  static uint64_t countsPerUs;
  static bool enabled;
  static uint64_t record(uint64_t t0, const char* msg)
  {
    if(!enabled) return 0;
    char temp[64];
    uint64_t t1 = t();
    int n = intToStr(temp, int(t1 - t0));
    buff.append(temp, n).append(" us: ").append(msg).append("\n");
    return t1;
  }

  static void flush()
  {
    if(!enabled) return;
    uint64_t t0 = t();
    PLATFORM_LOG(buff.c_str());
    buff.clear();
    record(t0, "Tracer::flush\n");
  }

  // see github.com/Celtoys/Remotery for a simple impl w/o SDL
#ifdef UTRACE_SDL
  static void init() { enabled = true; countsPerUs = SDL_GetPerformanceFrequency()/1000000; }
  static uint64_t t() { return enabled ? SDL_GetPerformanceCounter()/countsPerUs : 0; }  // in microseconds
#else
  static void init() { enabled = true; }
  static uint64_t t() {
    if(!enabled) return 0;
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
  }
#endif
};

#define TRACE_INIT() Tracer::init()
#define TRACE_T() Tracer::t()
#define TRACE_BEGIN(var) uint64_t var = TRACE_T()
#define TRACE_STEP(t0, msg) Tracer::record(t0, msg)
#define TRACE_END(t0, msg) t0 = Tracer::record(t0, msg)
#define TRACE_FLUSH() Tracer::flush()

#define TRACE(stmt) do { TRACE_BEGIN(t0); stmt; TRACE_END(t0, #stmt); } while(0)

struct ScopedTrace
{
  std::string msg;
  uint64_t t0;
  ScopedTrace(std::string&& _msg) : msg(_msg), t0(TRACE_T()) {}
  ~ScopedTrace() { TRACE_END(t0, msg.c_str()); }
};

#define TRACE_SCOPE(...) ScopedTrace ScopedTrace_inst(fstring(__VA_ARGS__))

#ifdef UTRACE_IMPLEMENTATION
#undef UTRACE_IMPLEMENTATION

std::string Tracer::buff;
uint64_t Tracer::countsPerUs = 1000;
bool Tracer::enabled = false;

#endif  // UTRACE_IMPLEMENTATION

#else

#define TRACE_INIT() do {} while(0)
#define TRACE_BEGIN(var) do {} while(0)
#define TRACE_STEP(t0, msg) do {} while(0)
#define TRACE_END(t0, msg) do {} while(0)
#define TRACE_FLUSH() do {} while(0)
#define TRACE_SCOPE(...) do {} while(0)
#define TRACE(stmt) stmt

#endif  // UTRACE_ENABLE

#endif  // UTRACE_H
