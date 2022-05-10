#include <cassert>
#include "selfdrive/loggerd/encoder.h"

void VideoEncoder::publisher_init() {
  // publish
  service_name = this->type == DriverCam ? "driverEncodeData" :
    (this->type == WideRoadCam ? "wideRoadEncodeData" :
    (this->width == 1928 ? "roadEncodeData" : "qRoadEncodeData"));
  pm.reset(new PubMaster({service_name}));
}

void VideoEncoder::publisher_publish(VideoEncoder *e, int segment_num, uint32_t idx, VisionIpcBufExtra &extra,
                                     unsigned int flags, kj::ArrayPtr<capnp::byte> header, kj::ArrayPtr<capnp::byte> dat) {
  // broadcast packet
  MessageBuilder msg;
  auto event = msg.initEvent(true);
  auto edat = (e->type == DriverCam) ? event.initDriverEncodeData() :
    ((e->type == WideRoadCam) ? event.initWideRoadEncodeData() :
    (e->codec == HEVC ? event.initRoadEncodeData() : event.initQRoadEncodeData()));
  auto edata = edat.initIdx();
  edata.setFrameId(extra.frame_id);
  edata.setTimestampSof(extra.timestamp_sof);
  edata.setTimestampEof(extra.timestamp_eof);
  edata.setType(e->codec == RAW ? cereal::EncodeIndex::Type::BIG_BOX_LOSSLESS :
    (e->codec == HEVC ? cereal::EncodeIndex::Type::FULL_H_E_V_C : cereal::EncodeIndex::Type::QCAMERA_H264));
  edata.setEncodeId(idx);
  edata.setSegmentNum(segment_num);
  edata.setSegmentId(idx);
  edata.setFlags(flags);
  edata.setLen(dat.size());
  edat.setData(dat);
  if (flags & V4L2_BUF_FLAG_KEYFRAME) edat.setHeader(header);

  auto words = new kj::Array<capnp::word>(capnp::messageToFlatArray(msg));
  auto bytes = words->asBytes();
  e->pm->send(e->service_name, bytes.begin(), bytes.size());
  if (e->write) {
    e->to_write.push(words);
  } else {
    delete words;
  }
}

// TODO: writing should be moved to loggerd
void VideoEncoder::write_handler(VideoEncoder *e, const char *path) {
  VideoWriter writer(path, e->filename, e->codec != HEVC, e->width, e->height, e->fps, e->codec);

  bool first = true;
  kj::Array<capnp::word>* out_buf;
  while ((out_buf = e->to_write.pop())) {
    capnp::FlatArrayMessageReader cmsg(*out_buf);
    cereal::Event::Reader event = cmsg.getRoot<cereal::Event>();

    auto edata = (e->type == DriverCam) ? event.getDriverEncodeData() :
      ((e->type == WideRoadCam) ? event.getWideRoadEncodeData() :
      (e->width == 1928 ? event.getRoadEncodeData() : event.getQRoadEncodeData()));
    auto idx = edata.getIdx();
    auto flags = idx.getFlags();

    if (first) {
      assert(flags & V4L2_BUF_FLAG_KEYFRAME);
      auto header = edata.getHeader();
      writer.write((uint8_t *)header.begin(), header.size(), idx.getTimestampEof()/1000, true, false);
      first = false;
    }

    // dangerous cast from const, but should be fine
    auto data = edata.getData();
    if (data.size() > 0) {
      writer.write((uint8_t *)data.begin(), data.size(), idx.getTimestampEof()/1000, false, flags & V4L2_BUF_FLAG_KEYFRAME);
    }

    // free the data
    delete out_buf;
  }

  // VideoWriter is freed on out of scope
}
