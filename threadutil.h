#pragma once

#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <climits>

class Semaphore
{
private:
  std::mutex mtx;
  std::condition_variable cond;
  unsigned long cnt = 0;
  const unsigned long max;

public:
  Semaphore(unsigned long _max = ULONG_MAX) : max(_max) {}

  void post() {
    std::lock_guard<decltype(mtx)> lock(mtx);
    cnt = std::min(max, cnt+1);
    cond.notify_one();
  }

  void wait() {
    std::unique_lock<decltype(mtx)> lock(mtx);
    cond.wait(lock, [this](){ return cnt > 0; });
    --cnt;
  }

  // returns true if semaphore was signaled, false if timeout occurred
  bool waitForMsec(unsigned long ms) {
    std::unique_lock<decltype(mtx)> lock(mtx);
    bool res = cond.wait_for(lock, std::chrono::milliseconds(ms), [this](){ return cnt > 0; });
    if(res)
      --cnt;
    return res;
  }
};

// thread pool based on github.com/progschj/ThreadPool

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <future>
#include <functional>

class ThreadPool
{
public:
  ThreadPool(size_t nthreads);
  template<class F, class... Args>
  auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
  ~ThreadPool();

private:
  std::vector< std::thread > workers;
  std::queue< std::function<void()> > tasks;

  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t nthreads) : stop(false)
{
  if(nthreads == 0)
    nthreads = std::thread::hardware_concurrency();
  for(size_t ii = 0; ii < nthreads; ++ii)
    workers.emplace_back( [this](){
      for(;;) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(queue_mutex);
          condition.wait(lock, [this](){ return stop || !tasks.empty(); });
          if(stop && tasks.empty())
            return;
          task = std::move(tasks.front());
          tasks.pop();
        }
        task();
      }
    }
  );
}

// add new work item to the pool - returns a std::future, which has a wait() method
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>
{
  using return_type = typename std::result_of<F(Args...)>::type;

  auto task = std::make_shared< std::packaged_task<return_type()> >(
    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
  );

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    if(!stop)
      tasks.emplace([task](){ (*task)(); });
  }
  condition.notify_one();
  return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for(std::thread &worker: workers)
    worker.join();
}

// Thread safe deque - optionally use a std::list so references remain valid even when not locked (also,
//  note that std::deque may have large memory overhead)

template < class T, template<class, class...> class Container = std::deque >
class ThreadSafeQueue
{
public:
  std::mutex mutex;
  std::condition_variable cond_var;
  Container<T> queue;

  void push_back(T&& item) {
    { std::lock_guard<std::mutex> lock(mutex); queue.push_back(std::move(item)); }
    cond_var.notify_one();
  }

  template <class ...Params>
  void emplace_back(Params&&... params) {
    { std::lock_guard<std::mutex> lock(mutex); queue.emplace_back(std::forward<Params>(params)...); }
    cond_var.notify_one();
  }

  void push_front(T&& item) {
    { std::lock_guard<std::mutex> lock(mutex); queue.push_front(std::move(item)); }
    cond_var.notify_one();
  }

  // these should only be used for std::list container or by the unique thread that modifies container
  size_t size() { std::lock_guard<std::mutex> lock(mutex); return queue.size(); }
  bool empty() { std::lock_guard<std::mutex> lock(mutex); return queue.empty(); }
  T& front() { std::lock_guard<std::mutex> lock(mutex); return queue.front(); }
  T& back() { std::lock_guard<std::mutex> lock(mutex); return queue.back(); }

  void pop_front() { std::lock_guard<std::mutex> lock(mutex); queue.pop_front(); }
  void pop_back() { std::lock_guard<std::mutex> lock(mutex); queue.pop_back(); }

  bool pop_front(T& dest) {
    std::lock_guard<std::mutex> lock(mutex);
    if(queue.empty()) return false;
    dest = std::move(queue.front());
    queue.pop_front();
    return true;
  }

  bool pop_back(T& dest) {
    std::lock_guard<std::mutex> lock(mutex);
    if(queue.empty()) return false;
    dest = std::move(queue.back());
    queue.pop_back();
    return true;
  }

  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    cond_var.wait(lock, [&]{ return !queue.empty(); });
  }
};
