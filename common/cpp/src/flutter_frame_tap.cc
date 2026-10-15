#include "flutter_frame_tap.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "task_runner.h"

namespace flutter_webrtc_plugin {

namespace {

constexpr int kDefaultMaxFps = 10;
constexpr int kDefaultMaxWidth = 1920;
constexpr int kDefaultMaxHeight = 1080;

int64_t SamplePosition(int dst, int src_size, int dst_size) {
  int64_t pos = ((2 * static_cast<int64_t>(dst) + 1) * src_size << 15) /
                    dst_size -
                (1 << 15);
  return pos < 0 ? 0 : pos;
}

// Bilinear scaling of one plane, sampling at pixel centres. An exact 2x
// reduction degenerates into a 2x2 box filter, which keeps text readable.
void ScalePlane(const uint8_t* src,
                int src_stride,
                int src_width,
                int src_height,
                uint8_t* dst,
                int dst_width,
                int dst_height) {
  if (src_width == dst_width && src_height == dst_height) {
    for (int y = 0; y < dst_height; y++) {
      std::memcpy(dst + y * dst_width, src + y * src_stride, dst_width);
    }
    return;
  }

  std::vector<int> x0(dst_width), x1(dst_width), fx(dst_width);
  for (int x = 0; x < dst_width; x++) {
    int64_t pos = SamplePosition(x, src_width, dst_width);
    x0[x] = std::min(static_cast<int>(pos >> 16), src_width - 1);
    x1[x] = std::min(x0[x] + 1, src_width - 1);
    fx[x] = static_cast<int>(pos & 0xFFFF);
  }

  for (int y = 0; y < dst_height; y++) {
    int64_t pos = SamplePosition(y, src_height, dst_height);
    int y0 = std::min(static_cast<int>(pos >> 16), src_height - 1);
    int y1 = std::min(y0 + 1, src_height - 1);
    int64_t fy = pos & 0xFFFF;
    const uint8_t* row0 = src + y0 * src_stride;
    const uint8_t* row1 = src + y1 * src_stride;
    uint8_t* out = dst + y * dst_width;
    for (int x = 0; x < dst_width; x++) {
      int64_t top = row0[x0[x]] * (65536 - fx[x]) + row0[x1[x]] * fx[x];
      int64_t bottom = row1[x0[x]] * (65536 - fx[x]) + row1[x1[x]] * fx[x];
      int64_t value = top * (65536 - fy) + bottom * fy;
      out[x] = static_cast<uint8_t>((value + (int64_t{1} << 31)) >> 32);
    }
  }
}

}  // namespace

FrameTapSink::FrameTapSink(TaskRunner* task_runner,
                           std::unique_ptr<EventChannelProxy> channel,
                           int max_fps,
                           int max_width,
                           int max_height)
    : task_runner_(task_runner),
      delivery_(std::make_shared<Delivery>()),
      interval_(std::chrono::milliseconds(1000 / std::max(1, max_fps))),
      max_width_(max_width),
      max_height_(max_height) {
  delivery_->channel = std::move(channel);
}

void FrameTapSink::OnFrame(scoped_refptr<RTCVideoFrame> frame) {
  auto now = std::chrono::steady_clock::now();
  if (now - last_ < interval_) {
    return;
  }
  if (delivery_->pending.exchange(true)) {
    return;
  }
  last_ = now;

  int src_width = frame->width() & ~1;
  int src_height = frame->height() & ~1;
  if (src_width <= 0 || src_height <= 0) {
    delivery_->pending = false;
    return;
  }
  double scale = std::min({1.0, static_cast<double>(max_width_) / src_width,
                           static_cast<double>(max_height_) / src_height});
  int width = std::max(2, static_cast<int>(src_width * scale) & ~1);
  int height = std::max(2, static_cast<int>(src_height * scale) & ~1);
  int chroma_width = width / 2;
  int chroma_height = height / 2;

  std::vector<uint8_t> i420(width * height + 2 * chroma_width * chroma_height);
  uint8_t* y_plane = i420.data();
  uint8_t* u_plane = y_plane + width * height;
  uint8_t* v_plane = u_plane + chroma_width * chroma_height;
  ScalePlane(frame->DataY(), frame->StrideY(), src_width, src_height, y_plane,
             width, height);
  ScalePlane(frame->DataU(), frame->StrideU(), src_width / 2, src_height / 2,
             u_plane, chroma_width, chroma_height);
  ScalePlane(frame->DataV(), frame->StrideV(), src_width / 2, src_height / 2,
             v_plane, chroma_width, chroma_height);

  EncodableMap event;
  event[EncodableValue("width")] = EncodableValue(width);
  event[EncodableValue("height")] = EncodableValue(height);
  event[EncodableValue("data")] = EncodableValue(std::move(i420));
  EncodableValue value(std::move(event));

  std::weak_ptr<Delivery> weak_delivery = delivery_;
  task_runner_->EnqueueTask([weak_delivery, value]() {
    auto delivery = weak_delivery.lock();
    if (!delivery) {
      return;
    }
    delivery->channel->Success(value, false);
    delivery->pending = false;
  });
}

void FlutterFrameTap::StartFrameTap(const std::string& track_id,
                                    const std::string& format,
                                    int max_fps,
                                    int max_width,
                                    int max_height,
                                    std::unique_ptr<MethodResultProxy> result) {
  if (format != "i420") {
    result->Error("videoTrackStartFrameTap",
                  "videoTrackStartFrameTap() format " + format +
                      " is not supported on this platform");
    return;
  }
  if (base_->task_runner_ == nullptr) {
    result->Error("videoTrackStartFrameTap",
                  "videoTrackStartFrameTap() needs a task runner");
    return;
  }
  scoped_refptr<RTCMediaTrack> track = base_->MediaTrackForId(track_id);
  if (track == nullptr || track->kind().std_string() != "video") {
    result->Error("videoTrackStartFrameTap",
                  "videoTrackStartFrameTap() video track not found");
    return;
  }

  std::string tap_id = base_->GenerateUUID();
  Tap tap;
  tap.track = static_cast<RTCVideoTrack*>(track.get());
  tap.sink = std::make_unique<FrameTapSink>(
      base_->task_runner_,
      EventChannelProxy::Create(base_->messenger_, base_->task_runner_,
                                "FlutterWebRTC/frameTap" + tap_id),
      max_fps > 0 ? max_fps : kDefaultMaxFps,
      max_width > 0 ? max_width : kDefaultMaxWidth,
      max_height > 0 ? max_height : kDefaultMaxHeight);
  tap.track->AddRenderer(tap.sink.get());
  taps_[tap_id] = std::move(tap);

  EncodableMap params;
  params[EncodableValue("tapId")] = EncodableValue(tap_id);
  result->Success(EncodableValue(params));
}

void FlutterFrameTap::StopFrameTap(const std::string& tap_id,
                                   std::unique_ptr<MethodResultProxy> result) {
  auto it = taps_.find(tap_id);
  if (it != taps_.end()) {
    it->second.track->RemoveRenderer(it->second.sink.get());
    taps_.erase(it);
  }
  result->Success();
}

}  // namespace flutter_webrtc_plugin
