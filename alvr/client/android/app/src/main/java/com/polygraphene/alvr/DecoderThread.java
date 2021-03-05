package com.polygraphene.alvr;

import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.media.MediaFormat;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import androidx.annotation.NonNull;
import android.view.Surface;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.Queue;

public class DecoderThread extends ThreadBase implements Handler.Callback {
    private static final String TAG = "DecoderThread";

    private static final int CODEC_H264 = 0;
    private static final int CODEC_H265 = 1;
    private int mCodec = CODEC_H265;
    private int mPriority = 0;

    private static final String VIDEO_FORMAT_H264 = "video/avc";
    private static final String VIDEO_FORMAT_H265 = "video/hevc";
    private String mFormat = VIDEO_FORMAT_H265;

    private MediaCodec mDecoder = null;
    private final Surface mSurface;

    private boolean mWaitNextIDR = false;

    private final NalQueue mNalQueue = new NalQueue();
    private final OutputFrameQueue mQueue;

    private static final int MESSAGE_PUSH_NAL = 1;
    private static final int MESSAGE_INPUT_BUFFER_AVAILABLE = 2;
    private static final int MESSAGE_OUTPUT_FRAME = 3;

    private Handler mHandler;

    public interface DecoderCallback {
        void onPrepared();
        void onFrameDecoded();
    }

    private final DecoderCallback mDecoderCallback;

    private static final int NAL_TYPE_SPS = 7;
    private static final int NAL_TYPE_IDR = 5;
    private static final int NAL_TYPE_P = 1;

    private static final int H265_NAL_TYPE_IDR_W_RADL = 19;
    private static final int H265_NAL_TYPE_VPS = 32;

    // Dummy SPS/PPS for some decoders which crashes on not set csd-0/csd-1. (e.g. Galaxy S6 Exynos decoder)
    private final byte[] DummySPS = new byte[]{(byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x67, (byte) 0x64, (byte) 0x00, (byte) 0x20, (byte) 0xac, (byte) 0x2b, (byte) 0x40, (byte) 0x20,
            0x02, (byte) 0x0d, (byte) 0x80, (byte) 0x88, (byte) 0x00, (byte) 0x00, (byte) 0x1f, (byte) 0x40, (byte) 0x00, (byte) 0x0e, (byte) 0xa6, (byte) 0x04,
            0x7a, (byte) 0x55};
    private final byte[] DummyPPS = new byte[]{(byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x68, (byte) 0xee, (byte) 0x3c, (byte) 0xb0};

    private final byte[] DummyCSD_H265 = new byte[]{
            (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x40, (byte) 0x01, (byte) 0x0c, (byte) 0x01, (byte) 0xff, (byte) 0xff, (byte) 0x21, (byte) 0x40,
            (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03,
            (byte) 0x00, (byte) 0x78, (byte) 0xac, (byte) 0x09, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x42, (byte) 0x01, (byte) 0x01, (byte) 0x21,
            (byte) 0x40, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x00,
            (byte) 0x03, (byte) 0x00, (byte) 0x78, (byte) 0xa0, (byte) 0x02, (byte) 0x00, (byte) 0x80, (byte) 0x20, (byte) 0x16, (byte) 0x5a, (byte) 0xd2, (byte) 0x90,
            (byte) 0x96, (byte) 0x4b, (byte) 0x8c, (byte) 0x04, (byte) 0x04, (byte) 0x00, (byte) 0x00, (byte) 0x03, (byte) 0x00, (byte) 0x04, (byte) 0x00, (byte) 0x00,
            (byte) 0x03, (byte) 0x00, (byte) 0xf0, (byte) 0x20, (byte) 0x00, (byte) 0x00, (byte) 0x00, (byte) 0x01, (byte) 0x44, (byte) 0x01, (byte) 0xc0, (byte) 0xf7,
            (byte) 0xc0, (byte) 0xcc, (byte) 0x90
    };

    private final Queue<Integer> mAvailableInputs = new LinkedList<>();

    public DecoderThread(Surface surface, DecoderCallback callback) {
        mSurface = surface;
        mQueue = new OutputFrameQueue();
        mDecoderCallback = callback;
    }

    public void start() {
        super.startBase();
    }

    public void interrupt() {
        super.interrupt();

        mHandler.getLooper().quitSafely();

        mQueue.stop();
    }

    @Override
    public boolean handleMessage(Message msg) {
        switch (msg.what) {
            case MESSAGE_PUSH_NAL:
                Utils.log(TAG, () -> "MESSAGE_PUSH_NAL");
                NAL nal = (NAL) msg.obj;

                detectNALType(nal);
                mNalQueue.add(nal);
                pushNALInternal();
                return true;
            case MESSAGE_INPUT_BUFFER_AVAILABLE:
                Utils.log(TAG, () -> "MESSAGE_INPUT_BUFFER_AVAILABLE");
                int index = msg.arg1;
                mAvailableInputs.add(index);
                pushNALInternal();
                return true;
            case MESSAGE_OUTPUT_FRAME:
                Utils.log(TAG, () -> "MESSAGE_OUTPUT_FRAME");
                int index2 = msg.arg1;
                MediaCodec.BufferInfo info = (MediaCodec.BufferInfo) msg.obj;

                mQueue.pushOutputBuffer(index2, info);
                mDecoderCallback.onFrameDecoded();
                return true;
        }
        return false;
    }

    protected void run() {
        try {
            decodeLoop();
        } catch (IOException | IllegalStateException e) {
            e.printStackTrace();
            Utils.loge(TAG, () -> "DecoderThread stopped by Exception.");
        } finally {
            Utils.logi(TAG, () -> "Stopping decoder.");
            mQueue.stop();

            if (mDecoder != null) {
                try {
                    mDecoder.stop();
                    mDecoder.release();
                } catch (IllegalStateException e) {
                    e.printStackTrace();
                }
                mDecoder = null;
            }
        }
        Utils.logi(TAG, () -> "DecoderThread stopped.");
    }

    private void decodeLoop() throws IOException {
        mAvailableInputs.clear();
        mNalQueue.clear();

        Looper.prepare();
        mHandler = new Handler(this);

        int dummyHeight = 512;
        int dummyWidth = 1024;
        MediaFormat format = MediaFormat.createVideoFormat(mFormat, dummyWidth, dummyHeight);
        format.setString("KEY_MIME", mFormat);

        format.setInteger(MediaFormat.KEY_OPERATING_RATE, Short.MAX_VALUE);
        format.setInteger(MediaFormat.KEY_PRIORITY, mPriority);

        if (mCodec == CODEC_H264) {
            format.setByteBuffer("csd-0", ByteBuffer.wrap(DummySPS, 0, DummySPS.length));
            format.setByteBuffer("csd-1", ByteBuffer.wrap(DummyPPS, 0, DummyPPS.length));
        } else {
            format.setByteBuffer("csd-0", ByteBuffer.wrap(DummyCSD_H265, 0, DummyCSD_H265.length));
        }
        mDecoder = MediaCodec.createDecoderByType(mFormat);

        mQueue.setCodec(mDecoder);



        mDecoder.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT);
        mDecoder.setCallback(new Callback());
        mDecoder.configure(format, mSurface, null, 0);
        mDecoder.start();

        Utils.logi(TAG, () -> "Codec created. Type=" + mFormat + " Name=" + mDecoder.getCodecInfo().getName());

        mDecoderCallback.onPrepared();

        mWaitNextIDR = true;
        setWaitingNextIDR(true);

        Looper.loop();
    }

    private boolean pushInputBuffer(NAL nal, long presentationTimeUs, int flags) {
        if (presentationTimeUs != 0) {
            mQueue.pushInputBuffer(presentationTimeUs, nal.frameIndex);
        }

        while (nal.length > 0) {
            Integer bufferIndex = mAvailableInputs.poll();
            if (bufferIndex == null) {
                // Insufficient buffer
                return false;
            }
            ByteBuffer buffer = mDecoder.getInputBuffer(bufferIndex);

            int copyLength = Math.min(nal.length, buffer.remaining());
            buffer.put(nal.buf, 0, copyLength);

            mDecoder.queueInputBuffer(bufferIndex, 0, buffer.position(), presentationTimeUs, flags);
            nal.length -= copyLength;

            if (nal.length > 0) {
                String name = mDecoder.getCodecInfo().getName();
                Utils.frameLog(nal.frameIndex, () -> "Splitting input buffer for codec. NAL Size="
                        + nal.length + " copyLength=" + copyLength + " codec=" + name);
            }
        }
        return true;
    }

    // Called from Main thread.
    class Callback extends MediaCodec.Callback {
        @Override
        public void onInputBufferAvailable(@NonNull MediaCodec codec, final int index) {
            Utils.log(TAG, () -> "mHandler.sendMessage MESSAGE_INPUT_BUFFER_AVAILABLE");
            Message message = mHandler.obtainMessage(MESSAGE_INPUT_BUFFER_AVAILABLE);
            message.arg1 = index;
            mHandler.sendMessage(message);
        }

        @Override
        public void onOutputBufferAvailable(@NonNull MediaCodec codec, int index, @NonNull MediaCodec.BufferInfo info) {
            Utils.log(TAG, () -> "mHandler.sendMessage MESSAGE_OUTPUT_FRAME");
            Message message = mHandler.obtainMessage(MESSAGE_OUTPUT_FRAME);
            message.arg1 = index;
            message.obj = info;
            mHandler.sendMessage(message);
        }

        @Override
        public void onError(@NonNull MediaCodec codec, @NonNull MediaCodec.CodecException e) {
            Utils.loge(TAG, () -> "Codec Error: " + e.getMessage() + "\n" + e.getDiagnosticInfo());
        }

        @Override
        public void onOutputFormatChanged(@NonNull MediaCodec codec, @NonNull MediaFormat format) {
            Utils.logi(TAG, () -> "New format " + mDecoder.getOutputFormat());
        }
    }

    public void onConnect(int codec, boolean realtime) {
        Utils.logi(TAG, () -> "onConnect()");
        mQueue.reset();
        notifyCodecChange(codec, realtime);
    }

    public void onDisconnect() {
        mQueue.stop();
    }

    private void notifyCodecChange(int codec, boolean realtime) {
        final int priority = realtime ? 0 : 1;
        if (codec != mCodec || priority != mPriority) {
            Utils.logi(TAG, () -> "notifyCodecChange: Codec was changed. New Codec=" + codec);
            stopAndWait();
            mCodec = codec;
            mPriority = priority;
            if (mCodec == CODEC_H264) {
                mFormat = VIDEO_FORMAT_H264;
            } else {
                mFormat = VIDEO_FORMAT_H265;
            }
            mQueue.reset();
            start();
        } else {
            Utils.logi(TAG, () -> "notifyCodecChange: Codec was not changed. Codec=" + codec);
            mDecoderCallback.onPrepared();
            //mWaitNextIDR = true;
        }
    }

    private void pushNALInternal() {
        if (isStopped()) {
            Utils.logi(TAG, () ->"decodeLoop Stopped. mStopped==true.");
            return;
        }
        if (mAvailableInputs.size() == 0) {
            return;
        }
        NAL nal = mNalQueue.peek();
        if (nal == null) {
            return;
        }

        long presentationTime = System.nanoTime() / 1000;

        boolean consumed;

        if (nal.type == NAL_TYPE_SPS) {
            // (VPS + )SPS + PPS
            Utils.frameLog(nal.frameIndex, () -> "Feed codec config. Size=" + nal.length);

            mWaitNextIDR = false;

            consumed = pushInputBuffer(nal, 0, MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
        } else if (nal.type == NAL_TYPE_IDR) {
            // IDR-Frame
            Utils.frameLog(nal.frameIndex, () -> "Feed IDR-Frame. Size=" + nal.length + " PresentationTime=" + presentationTime);
            setWaitingNextIDR(false);

            DecoderInput(nal.frameIndex);

            consumed = pushInputBuffer(nal, presentationTime, 0);
        } else {
            // PFrame
            DecoderInput(nal.frameIndex);

            if (mWaitNextIDR) {
                // Ignore P-Frame until next I-Frame
                Utils.frameLog(nal.frameIndex, () -> "Ignoring P-Frame");

                consumed = true;
            } else {
                // P-Frame
                Utils.frameLog(nal.frameIndex, () -> "Feed P-Frame. Size=" + nal.length + " PresentationTime=" + presentationTime);

                consumed = pushInputBuffer(nal, presentationTime, 0);
            }
        }
        if (consumed) {
            mNalQueue.remove();
        }
    }

    private void detectNALType(NAL nal) {
        int NALType;

        if (mCodec == CODEC_H264) {
            NALType = nal.buf[4] & 0x1F;
        } else {
            NALType = (nal.buf[4] >> 1) & 0x3F;
        }
        Utils.frameLog(nal.frameIndex, () -> "Got NAL Type=" + NALType + " Length=" + nal.length + " QueueSize=" + mNalQueue.size());

        if ((mCodec == CODEC_H264 && NALType == NAL_TYPE_SPS) ||
                (mCodec == CODEC_H265 && NALType == H265_NAL_TYPE_VPS)) {
            // (VPS + )SPS + PPS
            nal.type = NAL_TYPE_SPS;
        } else if ((mCodec == CODEC_H264 && NALType == NAL_TYPE_IDR) ||
                (mCodec == CODEC_H265 && NALType == H265_NAL_TYPE_IDR_W_RADL)) {
            // IDR-Frame
            nal.type = NAL_TYPE_IDR;
        } else {
            // PFrame
            nal.type = NAL_TYPE_P;
        }
    }

    public NAL obtainNAL(int length) {
        return mNalQueue.obtain(length);
    }

    public void pushNAL(NAL nal) {
        Message message = mHandler.obtainMessage(MESSAGE_PUSH_NAL, nal);
        mHandler.sendMessage(message);
    }

    public void releaseBuffer() {
        mQueue.render();
    }

    public void onFrameAvailable() {
        mQueue.onFrameAvailable();
    }

    public long clearAvailable(SurfaceTexture surfaceTexture) {
        return mQueue.clearAvailable(surfaceTexture);
    }

    public static native void DecoderInput(long frameIndex);
    public static native void DecoderOutput(long frameIndex);
    public static native void setWaitingNextIDR(boolean waiting);
}
