#ifndef FLUTTER_WEBRTC_SERIAL_THREAD_HXX
#define FLUTTER_WEBRTC_SERIAL_THREAD_HXX

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace flutter_webrtc_plugin {

// A background thread that runs posted tasks one after another.
//
// libwebrtc does camera open/close and stats collection through
// Thread::BlockingCall on its own threads; doing either from the platform
// thread stalls the window for as long as the camera takes to start (a second
// or more with V4L2). Work like that goes to one of these threads instead.
// The threads live for the whole process and are intentionally never joined.
class SerialThread {
 public:
  static SerialThread& Camera() {
    static SerialThread* instance = new SerialThread();
    return *instance;
  }

  static SerialThread& Stats() {
    static SerialThread* instance = new SerialThread();
    return *instance;
  }

  void Post(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
  }

 private:
  SerialThread() { std::thread([this] { Run(); }).detach(); }

  void Run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return !tasks_.empty(); });
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> tasks_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_SERIAL_THREAD_HXX
