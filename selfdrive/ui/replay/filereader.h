#pragma once

#include <QMultiMap>
#include <unordered_map>
#include <vector>

#include <capnp/serialize.h>
#include "cereal/gen/cpp/log.capnp.h"
#include "selfdrive/camerad/cameras/camera_common.h"

const CameraType ALL_CAMERAS[] = {RoadCam, DriverCam, WideRoadCam};
const int MAX_CAMERAS = std::size(ALL_CAMERAS);
struct EncodeIdx {
  int segmentNum;
  uint32_t frameEncodeId;
};
class Event {
public:
  Event(const kj::ArrayPtr<const capnp::word> &amsg) : reader(amsg) {
    words = kj::ArrayPtr<const capnp::word>(amsg.begin(), reader.getEnd());
    event = reader.getRoot<cereal::Event>();
    which = event.which();
    mono_time = event.getLogMonoTime();
  }
  inline kj::ArrayPtr<const capnp::byte> bytes() const { return words.asBytes(); }

  uint64_t mono_time;
  cereal::Event::Which which;
  cereal::Event::Reader event;
  capnp::FlatArrayMessageReader reader;
  kj::ArrayPtr<const capnp::word> words;
};

class LogReader {
public:
  LogReader() = default;
  ~LogReader();
  bool load(const std::string &file);

  QMultiMap<uint64_t, Event*> events;
  std::unordered_map<uint32_t, EncodeIdx> eidx[MAX_CAMERAS] = {};

private:
  void parseEvents(const QByteArray &dat);
  std::vector<uint8_t> raw_;
};
