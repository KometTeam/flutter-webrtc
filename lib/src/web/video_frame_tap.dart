import 'dart:typed_data';

import 'package:webrtc_interface/webrtc_interface.dart';

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

class VideoFrameTap {
  Stream<VideoTapFrame> get frames => const Stream.empty();

  static Future<VideoFrameTap> start(
    MediaStreamTrack track, {
    VideoFrameTapFormat format = VideoFrameTapFormat.i420,
    int maxFps = 10,
    int maxWidth = 1920,
    int maxHeight = 1080,
    int bitrateKbps = 2000,
  }) =>
      throw UnsupportedError('VideoFrameTap is not available on the web');

  Future<void> requestKeyFrame() async {}

  Future<void> stop() async {}
}
