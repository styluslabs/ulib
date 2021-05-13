#include <mutex>
#include <condition_variable>

class Semaphore
{
private:
  std::mutex mtx;
  std::condition_variable cond;
  unsigned long cnt = 0;

public:
  void post()
  {
    std::lock_guard<decltype(mtx)> lock(mtx);
    ++cnt;
    cond.notify_one();
  }

  void wait()
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    cond.wait(lock, [this](){ return cnt > 0; });
    --cnt;
  }

  // returns true if semaphore was signaled, false if timeout occurred
  bool waitForMsec(unsigned long ms)
  {
    std::unique_lock<decltype(mtx)> lock(mtx);
    bool res = cond.wait_for(lock, std::chrono::milliseconds(ms), [this](){ return cnt > 0; });
    if(res)
      --cnt;
    return res;
  }
};
