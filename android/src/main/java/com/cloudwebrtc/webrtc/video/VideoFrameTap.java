package com.cloudwebrtc.webrtc.video;

import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.util.Log;

import com.cloudwebrtc.webrtc.utils.AnyThreadSink;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

import io.flutter.plugin.common.BinaryMessenger;
import io.flutter.plugin.common.EventChannel;

import org.webrtc.VideoFrame;
import org.webrtc.VideoSink;
import org.webrtc.VideoTrack;

/**
 * Encodes the frames of a video track to VP8 with MediaCodec and hands them to
 * Dart on the "FlutterWebRTC/frameTap{id}" event channel, at most maxFps per
 * second and scaled down to fit maxWidth x maxHeight. The software encoders of
 * org.webrtc cannot be driven from Java (their encode() is native-only), and
 * every Android device ships a VP8 MediaCodec encoder (c2.android.vp8.encoder,
 * OMX.google.vp8.encoder on older releases).
 */
public class VideoFrameTap implements VideoSink, EventChannel.StreamHandler {
  private static final String TAG = "VideoFrameTap";
  private static final String MIME = MediaFormat.MIMETYPE_VIDEO_VP8;
  private static final int KEY_FRAME_INTERVAL_SECONDS = 10;
  private static final long INPUT_TIMEOUT_US = 20_000;
  private static final long OUTPUT_TIMEOUT_US = 50_000;

  private final VideoTrack track;
  private final EventChannel eventChannel;
  private final HandlerThread thread;
  private final Handler handler;
  private final long intervalMs;
  private final int maxWidth;
  private final int maxHeight;
  private final int bitrateBps;
  private final int maxFps;
  private final AtomicBoolean busy = new AtomicBoolean(false);

  private volatile AnyThreadSink sink;
  private volatile boolean disposed;
  private long lastFrameMs;
  private MediaCodec codec;
  private int codecWidth;
  private int codecHeight;
  private boolean keyFrameWanted;

  public VideoFrameTap(
      BinaryMessenger messenger,
      String tapId,
      VideoTrack track,
      int maxFps,
      int maxWidth,
      int maxHeight,
      int bitrateKbps) {
    this.track = track;
    this.maxFps = Math.max(1, maxFps);
    this.intervalMs = 1000 / this.maxFps;
    this.maxWidth = maxWidth;
    this.maxHeight = maxHeight;
    this.bitrateBps = bitrateKbps * 1000;
    thread = new HandlerThread("FrameTapEncoder");
    thread.start();
    handler = new Handler(thread.getLooper());
    eventChannel = new EventChannel(messenger, "FlutterWebRTC/frameTap" + tapId);
    eventChannel.setStreamHandler(this);
    track.addSink(this);
  }

  @Override
  public void onListen(Object arguments, EventChannel.EventSink events) {
    sink = new AnyThreadSink(events);
    requestKeyFrame();
  }

  @Override
  public void onCancel(Object arguments) {
    sink = null;
  }

  @Override
  public void onFrame(VideoFrame frame) {
    long now = SystemClock.elapsedRealtime();
    if (disposed || sink == null || now - lastFrameMs < intervalMs) {
      return;
    }
    if (!busy.compareAndSet(false, true)) {
      return;
    }
    lastFrameMs = now;

    VideoFrame.Buffer buffer = frame.getBuffer();
    int srcWidth = buffer.getWidth() & ~1;
    int srcHeight = buffer.getHeight() & ~1;
    double scale =
        Math.min(1.0, Math.min((double) maxWidth / srcWidth, (double) maxHeight / srcHeight));
    int width = Math.max(2, (int) (srcWidth * scale) & ~1);
    int height = Math.max(2, (int) (srcHeight * scale) & ~1);

    VideoFrame.I420Buffer i420;
    VideoFrame.Buffer scaled = buffer.cropAndScale(0, 0, srcWidth, srcHeight, width, height);
    try {
      i420 = scaled.toI420();
    } finally {
      scaled.release();
    }
    if (i420 == null) {
      busy.set(false);
      return;
    }
    long ptsUs = frame.getTimestampNs() / 1000;
    handler.post(() -> {
      try {
        encode(i420, ptsUs);
      } catch (Exception e) {
        Log.w(TAG, "encode failed", e);
        releaseCodec();
      } finally {
        i420.release();
        busy.set(false);
      }
    });
  }

  public void requestKeyFrame() {
    handler.post(() -> keyFrameWanted = true);
  }

  public void dispose() {
    disposed = true;
    try {
      track.removeSink(this);
    } catch (IllegalStateException ignored) {
      // The track was disposed first, it has no sinks left.
    }
    eventChannel.setStreamHandler(null);
    sink = null;
    handler.post(() -> {
      releaseCodec();
      thread.quitSafely();
    });
  }

  private void encode(VideoFrame.I420Buffer frame, long ptsUs) throws Exception {
    if (disposed) {
      return;
    }
    int width = frame.getWidth();
    int height = frame.getHeight();
    if (codec == null || codecWidth != width || codecHeight != height) {
      releaseCodec();
      startCodec(width, height);
    } else if (keyFrameWanted) {
      Bundle parameters = new Bundle();
      parameters.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
      codec.setParameters(parameters);
    }
    keyFrameWanted = false;

    int index = codec.dequeueInputBuffer(INPUT_TIMEOUT_US);
    if (index < 0) {
      return;
    }
    Image image = codec.getInputImage(index);
    if (image == null) {
      codec.queueInputBuffer(index, 0, 0, ptsUs, 0);
      return;
    }
    copyPlane(frame.getDataY(), frame.getStrideY(), width, height, image.getPlanes()[0]);
    copyPlane(frame.getDataU(), frame.getStrideU(), width / 2, height / 2, image.getPlanes()[1]);
    copyPlane(frame.getDataV(), frame.getStrideV(), width / 2, height / 2, image.getPlanes()[2]);
    codec.queueInputBuffer(index, 0, width * height * 3 / 2, ptsUs, 0);
    drain();
  }

  private void startCodec(int width, int height) throws Exception {
    MediaFormat format = MediaFormat.createVideoFormat(MIME, width, height);
    format.setInteger(
        MediaFormat.KEY_COLOR_FORMAT,
        MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
    format.setInteger(MediaFormat.KEY_BIT_RATE, bitrateBps);
    format.setInteger(MediaFormat.KEY_FRAME_RATE, maxFps);
    format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, KEY_FRAME_INTERVAL_SECONDS);
    codec = MediaCodec.createEncoderByType(MIME);
    codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
    codec.start();
    codecWidth = width;
    codecHeight = height;
    Log.i(TAG, "VP8 encoder " + codec.getName() + " " + width + "x" + height);
  }

  private void drain() {
    MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
    long timeoutUs = OUTPUT_TIMEOUT_US;
    while (true) {
      int index = codec.dequeueOutputBuffer(info, timeoutUs);
      if (index < 0) {
        if (index == MediaCodec.INFO_TRY_AGAIN_LATER) {
          return;
        }
        continue;
      }
      timeoutUs = 0;
      ByteBuffer output = codec.getOutputBuffer(index);
      boolean config = (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
      if (output != null && info.size > 0 && !config) {
        byte[] data = new byte[info.size];
        output.position(info.offset);
        output.get(data, 0, info.size);
        emit(data, (info.flags & MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0);
      }
      codec.releaseOutputBuffer(index, false);
    }
  }

  private void emit(byte[] data, boolean keyFrame) {
    AnyThreadSink events = sink;
    if (events == null) {
      return;
    }
    Map<String, Object> event = new HashMap<>();
    event.put("width", codecWidth);
    event.put("height", codecHeight);
    event.put("data", data);
    event.put("keyFrame", keyFrame);
    events.success(event);
  }

  private static void copyPlane(
      ByteBuffer src, int srcStride, int width, int height, Image.Plane dst) {
    ByteBuffer out = dst.getBuffer();
    int rowStride = dst.getRowStride();
    int pixelStride = dst.getPixelStride();
    if (pixelStride == 1) {
      for (int y = 0; y < height; y++) {
        src.limit(y * srcStride + width);
        src.position(y * srcStride);
        out.position(y * rowStride);
        out.put(src);
      }
      src.clear();
      return;
    }
    byte[] row = new byte[width];
    for (int y = 0; y < height; y++) {
      src.position(y * srcStride);
      src.get(row, 0, width);
      int base = y * rowStride;
      for (int x = 0; x < width; x++) {
        out.put(base + x * pixelStride, row[x]);
      }
    }
    src.clear();
  }

  private void releaseCodec() {
    if (codec == null) {
      return;
    }
    try {
      codec.stop();
    } catch (IllegalStateException ignored) {
      // Already stopped after an encoder error.
    }
    codec.release();
    codec = null;
  }
}
