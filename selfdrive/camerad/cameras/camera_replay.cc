#include "selfdrive/camerad/cameras/camera_replay.h"

#include <cassert>
#include <thread>

#include "selfdrive/common/clutil.h"
#include "selfdrive/common/util.h"

extern ExitHandler do_exit;

const char *BASE_URL = "https://commadataci.blob.core.windows.net/openpilotci/";

const std::string road_camera_route = "0c94aa1e1296d7c6|2021-05-05--19-48-37";
const std::string driver_camera_route = "534ccd8a0950a00c|2021-06-08--12-15-37";

std::string get_url(std::string route_name, const std::string &camera, int segment_num) {
  std::replace(route_name.begin(), route_name.end(), '|', '/');
  return util::string_format("%s%s/%d/%s.hevc", BASE_URL, route_name.c_str(), segment_num, camera.c_str());
}

void cameras_open(MultiCameraState *s) {}
void camera_autoexposure(CameraState *s, float grey_frac) {}
void process_road_camera(MultiCameraState *s, CameraState *c, int cnt) {}

void camera_open(CameraState *s, bool rear) {
  // empty
}

void camera_close(CameraState *s) {
  delete s->frame_reader;
}

void camera_init(VisionIpcServer *v, CameraState *s, int camera_id, unsigned int fps, cl_device_id device_id, cl_context ctx, VisionStreamType rgb_type, VisionStreamType yuv_type, const std::string &url) {
  s->frame_reader = new FrameReader(url);
  if (!s->frame_reader->process()) {
    printf("failed to load stream from %s", url.c_str());
    assert(0);
  }

  CameraInfo ci = {
      .frame_width = s->frame_reader->width,
      .frame_height = s->frame_reader->height,
      .frame_stride = s->frame_reader->width * 3};
  s->ci = ci;
  s->camera_num = camera_id;
  s->fps = fps;
  s->buf.init(device_id, ctx, s, v, FRAME_BUF_COUNT, rgb_type, yuv_type);
}

void cameras_close(MultiCameraState *s) {
  camera_close(&s->road_cam);
  camera_close(&s->driver_cam);
}

void cameras_init(VisionIpcServer *v, MultiCameraState *s, cl_device_id device_id, cl_context ctx) {
  camera_init(v, &s->road_cam, CAMERA_ID_LGC920, 20, device_id, ctx,
              VISION_STREAM_RGB_BACK, VISION_STREAM_YUV_BACK, get_url(road_camera_route, "fcamera", 0));
  camera_init(v, &s->driver_cam, CAMERA_ID_LGC615, 10, device_id, ctx,
              VISION_STREAM_RGB_FRONT, VISION_STREAM_YUV_FRONT, get_url(driver_camera_route, "dcamera", 0));
}

static void run_camera(CameraState *s) {
  uint32_t cam_frame_id = 0, frame_id = 0;
  size_t buf_idx = 0;
  while (!do_exit) {
    uint8_t *dat = s->frame_reader->get(cam_frame_id);
    if (dat) {
      s->buf.camera_bufs_metadata[buf_idx] = {.frame_id = frame_id};
      auto &buf = s->buf.camera_bufs[buf_idx];
      CL_CHECK(clEnqueueWriteBuffer(buf.copy_q, buf.buf_cl, CL_TRUE, 0, s->frame_reader->getRGBSize(), dat, 0, NULL, NULL));
      s->buf.queue(buf_idx);
      ++frame_id;
      ++cam_frame_id;
      buf_idx = (buf_idx + 1) % FRAME_BUF_COUNT;
      util::sleep_for(1000 / s->fps);
    } else {
      // loop
      cam_frame_id = 0;
    }
  }
}

static void road_camera_thread(CameraState *s) {
  set_thread_name("pc_road_camera_thread");
  run_camera(s);
}

static void driver_camera_thread(CameraState *s) {
  set_thread_name("pc_driver_camera_thread");
  run_camera(s);
}

void process_driver_camera(MultiCameraState *s, CameraState *c, int cnt) {
}

void cameras_run(MultiCameraState *s) {
  std::vector<std::thread> threads;
  threads.push_back(start_process_thread(s, &s->road_cam, process_road_camera));
  threads.push_back(start_process_thread(s, &s->driver_cam, process_driver_camera));
  std::thread road_cam_thread = std::thread(road_camera_thread, &s->road_cam);
  driver_camera_thread(&s->driver_cam);

  road_cam_thread.join();
  for (auto &t : threads) t.join();

  cameras_close(s);
}
