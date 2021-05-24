#pragma once

#include <unistd.h>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <list>
#include <condition_variable>

#include <QString>

// independent of QT, needs ffmpeg
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}


class FrameReader {
public:
  FrameReader(const QString &fn);
  ~FrameReader();
  uint8_t *get(int idx);
  AVFrame *toRGB(AVFrame *);
  int getRGBSize() { return width*height*3; }
  void process();

private:
  void decodeThread();

  struct Frame{
    AVPacket *pkt;
    AVFrame *picture;
  };
  std::vector<Frame*> frames;

  AVFormatContext *pFormatCtx = NULL;
  AVCodecContext *pCodecCtx = NULL;

	struct SwsContext *sws_ctx = NULL;

  std::mutex mutex;
  std::condition_variable cv_decode;
  std::condition_variable cv_frame;
  int decode_idx = -1;
  std::atomic<bool> exit_;
  std::thread thread;

  bool valid = true;
  QString url;

  int width, height;
};

