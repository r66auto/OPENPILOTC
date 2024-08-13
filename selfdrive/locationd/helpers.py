import numpy as np
import capnp
from typing import Any
from enum import Enum

from cereal import log
from openpilot.common.transformations.orientation import rot_from_euler, euler_from_rot


def rotate_cov(rot_matrix, cov_in):
  return rot_matrix @ cov_in @ rot_matrix.T


def rotate_std(rot_matrix, std_in):
  return np.sqrt(np.diag(rotate_cov(rot_matrix, np.diag(std_in**2))))


class NPQueue:
  def __init__(self, maxlen: int, rowsize: int) -> None:
    self.maxlen = maxlen
    self.arr = np.empty((0, rowsize))

  def __len__(self) -> int:
    return len(self.arr)

  def append(self, pt: list[float]) -> None:
    if len(self.arr) < self.maxlen:
      self.arr = np.append(self.arr, [pt], axis=0)
    else:
      self.arr[:-1] = self.arr[1:]
      self.arr[-1] = pt


class PointBuckets:
  def __init__(self, x_bounds: list[tuple[float, float]], min_points: list[float], min_points_total: int, points_per_bucket: int, rowsize: int) -> None:
    self.x_bounds = x_bounds
    self.buckets = {bounds: NPQueue(maxlen=points_per_bucket, rowsize=rowsize) for bounds in x_bounds}
    self.buckets_min_points = dict(zip(x_bounds, min_points, strict=True))
    self.min_points_total = min_points_total

  def __len__(self) -> int:
    return sum([len(v) for v in self.buckets.values()])

  def is_valid(self) -> bool:
    individual_buckets_valid = all(len(v) >= min_pts for v, min_pts in zip(self.buckets.values(), self.buckets_min_points.values(), strict=True))
    total_points_valid = self.__len__() >= self.min_points_total
    return individual_buckets_valid and total_points_valid

  def is_calculable(self) -> bool:
    return all(len(v) > 0 for v in self.buckets.values())

  def add_point(self, x: float, y: float) -> None:
    raise NotImplementedError

  def get_points(self, num_points: int = None) -> Any:
    points = np.vstack([x.arr for x in self.buckets.values()])
    if num_points is None:
      return points
    return points[np.random.choice(np.arange(len(points)), min(len(points), num_points), replace=False)]

  def load_points(self, points: list[list[float]]) -> None:
    for point in points:
      self.add_point(*point)


class ParameterEstimator:
  """ Base class for parameter estimators """
  def reset(self) -> None:
    raise NotImplementedError

  def handle_log(self, t: int, which: str, msg: log.Event) -> None:
    raise NotImplementedError

  def get_msg(self, valid: bool, with_points: bool) -> log.Event:
    raise NotImplementedError


class Measurement:
  def __init__(self, xyz: np.ndarray, xyz_std: np.ndarray):
    self.xyz: np.ndarray = xyz
    self.xyz_std: np.ndarray = xyz_std

    # properties for convenient access
    xyz_props = [property(lambda self, i=i: float(self.xyz[i])) for i in range(3)]
    xyz_std_props = [property(lambda self, i=i: float(self.xyz_std[i])) for i in range(3)]
    Measurement.x, Measurement.y, Measurement.z = xyz_props
    Measurement.x_std, Measurement.y_std, Measurement.z_std = xyz_std_props
    Measurement.roll, Measurement.pitch, Measurement.yaw = xyz_props
    Measurement.roll_std, Measurement.pitch_std, Measurement.yaw_std = xyz_std_props

  @classmethod
  def from_measurement_xyz(cls, measurement: log.LivePose.Measurement) -> 'Measurement':
    return cls(
      xyz=np.array([measurement.x, measurement.y, measurement.z]),
      xyz_std=np.array([measurement.xStd, measurement.yStd, measurement.zStd])
    )


class Pose:
  def __init__(self, orientation: Measurement, velocity: Measurement, acceleration: Measurement, angular_velocity: Measurement):
    self.orientation = orientation
    self.velocity = velocity
    self.acceleration = acceleration
    self.angular_velocity = angular_velocity

  @classmethod
  def from_live_pose(cls, live_pose: log.LivePose) -> 'Pose':
    return Pose(
      orientation=Measurement.from_measurement_xyz(live_pose.orientationNED),
      velocity=Measurement.from_measurement_xyz(live_pose.velocityDevice),
      acceleration=Measurement.from_measurement_xyz(live_pose.accelerationDevice),
      angular_velocity=Measurement.from_measurement_xyz(live_pose.angularVelocityDevice)
    )


class PoseCalibrator:
  def __init__(self):
    self.calib_valid = False
    self.calib_from_device = np.eye(3)

  def _transform_calib_from_device(self, meas: Measurement):
    new_xyz = self.calib_from_device @ meas.xyz
    new_xyz_std = rotate_std(self.calib_from_device, meas.xyz_std)
    return Measurement(new_xyz, new_xyz_std)

  def _ned_from_calib(self, orientation: Measurement):
    ned_from_device = rot_from_euler(orientation.xyz)
    ned_from_calib = ned_from_device * self.calib_from_device.T
    ned_from_calib_euler_meas = Measurement(euler_from_rot(ned_from_calib), np.full(3, np.nan))
    return ned_from_calib_euler_meas

  def build_calibrated_pose(self, pose: Pose) -> Pose:
    ned_from_calib_euler = self._ned_from_calib(pose.orientation)
    angular_velocity_calib = self._transform_calib_from_device(pose.angular_velocity)
    acceleration_calib = self._transform_calib_from_device(pose.acceleration)
    velocity_calib = self._transform_calib_from_device(pose.angular_velocity)

    return Pose(ned_from_calib_euler, velocity_calib, acceleration_calib, angular_velocity_calib)

  def feed_live_calib(self, live_calib: log.LiveCalibrationData):
    calib_rpy = np.array(live_calib.rpyCalib)
    device_from_calib = rot_from_euler(calib_rpy)
    self.calib_from_device = device_from_calib.T
    self.calib_valid = live_calib.calStatus == log.LiveCalibrationData.Status.calibrated
