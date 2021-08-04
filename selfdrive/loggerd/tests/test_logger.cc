#define CATCH_CONFIG_MAIN
#include <kj/array.h>

#include <climits>

#include "catch2/catch.hpp"
#include "cereal/messaging/messaging.h"
#include "selfdrive/common/util.h"
#include "selfdrive/loggerd/logger.h"

const int LOG_COUNT = 1000;
const int THREAD_COUNT = 5;
typedef cereal::Sentinel::SentinelType SentinelType;
namespace {

bool decompressBZ2(std::vector<uint8_t> &dest, const char srcData[], size_t srcSize,
                   size_t outputSizeIncrement = 0x100000U) {
  bz_stream strm = {};
  int ret = BZ2_bzDecompressInit(&strm, 0, 0);
  assert(ret == BZ_OK);
  dest.resize(1024 * 1024);
  strm.next_in = const_cast<char *>(srcData);
  strm.avail_in = srcSize;
  do {
    strm.next_out = (char *)&dest[strm.total_out_lo32];
    strm.avail_out = dest.size() - strm.total_out_lo32;
    ret = BZ2_bzDecompress(&strm);
    if (ret == BZ_OK && strm.avail_in > 0 && strm.avail_out == 0) {
      dest.resize(dest.size() + outputSizeIncrement);
    }
  } while (ret == BZ_OK && strm.avail_in > 0);

  BZ2_bzDecompressEnd(&strm);
  dest.resize(strm.total_out_lo32);
  return ret == BZ_STREAM_END;
}

void verify_logfile(const std::string &fn, uint64_t boottime, uint64_t monotonic, SentinelType begin_type, SentinelType end_type) {
  std::vector<uint8_t> log;
  std::string log_bz2 = util::read_file(fn);
  // if fn is still opened by LoggerHandle afater logger_close, log_bz2.size() is zero
  REQUIRE(log_bz2.size() > 0);
  bool ret = decompressBZ2(log, log_bz2.data(), log_bz2.size());
  REQUIRE(ret);

  uint64_t sum = 0, required_sum = 0;
  for (int i = 0; i < THREAD_COUNT; ++i) {
    required_sum += i * LOG_COUNT;
  }

  int i = 0;
  kj::ArrayPtr<const capnp::word> words((capnp::word *)log.data(), log.size() / sizeof(capnp::word));
  while (words.size() > 0) {
    try {
      capnp::FlatArrayMessageReader reader(words);
      auto event = reader.getRoot<cereal::Event>();
      if (i == 0) {
        REQUIRE(event.which() == cereal::Event::INIT_DATA);
      } else if (i == 1) {
        REQUIRE(event.which() == cereal::Event::SENTINEL);
        auto sentinel = event.getSentinel();
        REQUIRE(sentinel.getType() == begin_type);
      }
      if (event.which() == cereal::Event::CLOCKS) {
        auto clocks = event.getClocks();
        REQUIRE(clocks.getBootTimeNanos() == boottime);
        REQUIRE(clocks.getMonotonicNanos() == monotonic);
        sum += clocks.getModemUptimeMillis();
      }
      words = kj::arrayPtr(reader.getEnd(), words.end());
      if (words == 0) {
        // the last event should be SENTINEL
        REQUIRE(event.which() == cereal::Event::SENTINEL);
        auto sentinel = event.getSentinel();
        REQUIRE(sentinel.getType() == end_type);

      }
      ++i;
    } catch (...) {
      REQUIRE(0);
      break;
    }
  }
  REQUIRE(sum == required_sum);
}

}  // namespace

TEST_CASE("logger") {
  LoggerState logger = {};
  logger_init(&logger, "rlog", true);

  std::string log_root = util::getenv("TMPDIR" "/tmp/") + "log_root";
  char segment_path[PATH_MAX];
  int part = -1;
  logger_next(&logger, log_root.c_str(), segment_path, std::size(segment_path), &part);

  SECTION("multiple threads writing to log") {
    uint64_t boottime = nanos_since_boot();
    uint64_t monotonic = nanos_monotonic();
    std::vector<std::thread> threads;
    for (uint8_t i = 0; i < THREAD_COUNT; ++i) {
      threads.push_back(std::thread([=, log = &logger, thread_number = i]() {
        LoggerHandle *lh = logger_get_handle(log);
        for (int i = 0; i < LOG_COUNT; ++i) {
          MessageBuilder msg;
          auto clocks = msg.initEvent().initClocks();
          clocks.setBootTimeNanos(boottime);
          clocks.setMonotonicNanos(monotonic);
          clocks.setModemUptimeMillis(thread_number);
          auto bytes = msg.toBytes();
          lh_log(lh, bytes.begin(), bytes.size(), true);
        }
        lh_close(lh);
      }));
    }
    for (auto &t : threads) {
      t.join();
    }
    logger_close(&logger);
    verify_logfile(segment_path + std::string("/rlog.bz2"), boottime, monotonic, SentinelType::START_OF_ROUTE, SentinelType::END_OF_SEGMENT);
    verify_logfile(segment_path + std::string("/qlog.bz2"), boottime, monotonic, SentinelType::START_OF_ROUTE, SentinelType::END_OF_SEGMENT);
  }
}
