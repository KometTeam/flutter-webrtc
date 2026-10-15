#ifndef FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX
#define FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX

#include "flutter_common.h"
#include "flutter_webrtc_base.h"

namespace flutter_webrtc_plugin {

class FlutterMediaStream {
 public:
  FlutterMediaStream(FlutterWebRTCBase* base);

  void GetUserMedia(const EncodableMap& constraints,
                    std::unique_ptr<MethodResultProxy> result);

  void GetUserAudio(const EncodableMap& constraints,
                    scoped_refptr<RTCMediaStream> stream,
                    EncodableMap& params);

  void GetUserVideo(const EncodableMap& constraints,
                    scoped_refptr<RTCMediaStream> stream,
                    EncodableMap& params);

  void GetSources(std::unique_ptr<MethodResultProxy> result);

  void SelectAudioOutput(const std::string& device_id,
                         std::unique_ptr<MethodResultProxy> result);

  void SelectAudioInput(const std::string& device_id,
                        std::unique_ptr<MethodResultProxy> result);

  void MediaStreamGetTracks(const std::string& stream_id,
                            std::unique_ptr<MethodResultProxy> result);

  void MediaStreamDispose(const std::string& stream_id,
                          std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackSetEnable(const std::string& track_id,
                                 std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackSwitchCamera(const std::string& track_id,
                                    std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackDispose(const std::string& track_id,
                               std::unique_ptr<MethodResultProxy> result);

  void CreateLocalMediaStream(std::unique_ptr<MethodResultProxy> result);

  void OnDeviceChange();

 private:
  struct CameraRequest {
    EncodableMap video_constraints;
    std::string source_id;
    int32_t width = 0;
    int32_t height = 0;
    int32_t fps = 0;
  };

  struct OpenedCamera {
    scoped_refptr<RTCVideoCapturer> capturer;
    std::string source_id;
  };

  CameraRequest ParseCameraRequest(const EncodableMap& constraints);

  OpenedCamera OpenCamera(const CameraRequest& request);

  void AttachCamera(const CameraRequest& request,
                    const OpenedCamera& camera,
                    scoped_refptr<RTCMediaStream> stream,
                    EncodableMap& params);

  void StopCapture(scoped_refptr<RTCVideoCapturer> capturer);

  FlutterWebRTCBase* base_;
};

}  // namespace flutter_webrtc_plugin

#endif  // !FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX
