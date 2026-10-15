#ifndef FLUTTER_WEBRTC_FRAME_TAP_HXX
#define FLUTTER_WEBRTC_FRAME_TAP_HXX

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>

#include "flutter_common.h"
#include "flutter_webrtc_base.h"
#include "rtc_video_frame.h"
#include "rtc_video_renderer.h"
#include "rtc_video_track.h"

namespace flutter_webrtc_plugin {

// Delivers frames of a video track to Dart as contiguous I420 buffers, at most
// `max_fps` per second and scaled down to fit `max_width` x `max_height`. A
// frame is dropped while the previous one has not reached the platform thread
// yet, so a slow consumer never makes frames pile up in memory.
class FrameTapSink : public RTCVideoRenderer<scoped_refptr<RTCVideoFrame>> {
 public:
  FrameTapSink(TaskRunner* task_runner,
               std::unique_ptr<EventChannelProxy> channel,
               int max_fps,
               int max_width,
               int max_height);

  void OnFrame(scoped_refptr<RTCVideoFrame> frame) override;

 private:
  struct Delivery {
    std::unique_ptr<EventChannelProxy> channel;
    std::atomic<bool> pending{false};
  };

  TaskRunner* task_runner_;
  std::shared_ptr<Delivery> delivery_;
  std::chrono::steady_clock::duration interval_;
  std::chrono::steady_clock::time_point last_;
  int max_width_;
  int max_height_;
};

class FlutterFrameTap {
 public:
  FlutterFrameTap(FlutterWebRTCBase* base) : base_(base) {}

  void StartFrameTap(const std::string& track_id,
                     const std::string& format,
                     int max_fps,
                     int max_width,
                     int max_height,
                     std::unique_ptr<MethodResultProxy> result);

  void StopFrameTap(const std::string& tap_id,
                    std::unique_ptr<MethodResultProxy> result);

 private:
  struct Tap {
    scoped_refptr<RTCVideoTrack> track;
    std::unique_ptr<FrameTapSink> sink;
  };

  FlutterWebRTCBase* base_;
  std::map<std::string, Tap> taps_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_FRAME_TAP_HXX
