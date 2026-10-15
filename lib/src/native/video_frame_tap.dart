import 'dart:async';

import 'package:flutter/services.dart';

import 'package:webrtc_interface/webrtc_interface.dart';

import 'utils.dart';

/// Format of the frames a [VideoFrameTap] delivers.
///
/// [i420] hands out raw contiguous I420 buffers (Linux, Windows). [vp8]
/// encodes on the native side and hands out VP8 frames (Android), where no
/// libvpx is reachable from Dart.
enum VideoFrameTapFormat { i420, vp8 }

class VideoTapFrame {
  const VideoTapFrame({
    required this.width,
    required this.height,
    required this.data,
    this.keyFrame = false,
  });

  final int width;
  final int height;
  final Uint8List data;
  final bool keyFrame;
}

/// Receives the frames of a local or remote video track in Dart, at most
/// `maxFps` per second and scaled down to fit `maxWidth` x `maxHeight`.
class VideoFrameTap {
  VideoFrameTap._(this._tapId, Stream<dynamic> events) {
    _subscription = events.listen(
      (event) {
        final map = event as Map<dynamic, dynamic>;
        _frames.add(VideoTapFrame(
          width: map['width'] as int,
          height: map['height'] as int,
          data: map['data'] as Uint8List,
          keyFrame: map['keyFrame'] == true,
        ));
      },
      onError: _frames.addError,
    );
  }

  final String _tapId;
  final StreamController<VideoTapFrame> _frames =
      StreamController<VideoTapFrame>.broadcast();
  late final StreamSubscription<dynamic> _subscription;
  bool _stopped = false;

  Stream<VideoTapFrame> get frames => _frames.stream;

  static Future<VideoFrameTap> start(
    MediaStreamTrack track, {
    VideoFrameTapFormat format = VideoFrameTapFormat.i420,
    int maxFps = 10,
    int maxWidth = 1920,
    int maxHeight = 1080,
    int bitrateKbps = 2000,
  }) async {
    final response = await WebRTC.invokeMethod(
      'videoTrackStartFrameTap',
      <String, dynamic>{
        'trackId': track.id,
        'format': format.name,
        'maxFps': maxFps,
        'maxWidth': maxWidth,
        'maxHeight': maxHeight,
        'bitrateKbps': bitrateKbps,
      },
    );
    final tapId = (response as Map<dynamic, dynamic>)['tapId'] as String;
    return VideoFrameTap._(
      tapId,
      EventChannel('FlutterWebRTC/frameTap$tapId').receiveBroadcastStream(),
    );
  }

  /// Asks a [VideoFrameTapFormat.vp8] tap to make the next frame a key frame.
  Future<void> requestKeyFrame() async {
    if (_stopped) return;
    await WebRTC.invokeMethod(
      'videoTrackFrameTapRequestKeyFrame',
      <String, dynamic>{'tapId': _tapId},
    );
  }

  Future<void> stop() async {
    if (_stopped) return;
    _stopped = true;
    await _subscription.cancel();
    try {
      await WebRTC.invokeMethod(
        'videoTrackStopFrameTap',
        <String, dynamic>{'tapId': _tapId},
      );
    } finally {
      await _frames.close();
    }
  }
}
