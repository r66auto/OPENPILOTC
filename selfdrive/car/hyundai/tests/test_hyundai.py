#!/usr/bin/env python3
import unittest
import random

from cereal import car
from selfdrive.car.fw_versions import match_fw_to_car, build_fw_dict
from selfdrive.car.hyundai.values import CAMERA_SCC_CAR, CANFD_CAR, CAN_GEARS, CAR, CHECKSUM, FW_QUERY_CONFIG, \
                                         FW_VERSIONS, LEGACY_SAFETY_MODE_CAR, PART_NUMBER_FW_PATTERN, \
                                         get_platform_codes

Ecu = car.CarParams.Ecu
ECU_NAME = {v: k for k, v in Ecu.schema.enumerants.items()}


class TestHyundaiFingerprint(unittest.TestCase):
  def test_canfd_not_in_can_features(self):
    can_specific_feature_list = set.union(*CAN_GEARS.values(), *CHECKSUM.values(), LEGACY_SAFETY_MODE_CAR, CAMERA_SCC_CAR)
    for car_model in CANFD_CAR:
      self.assertNotIn(car_model, can_specific_feature_list, "CAN FD car unexpectedly found in a CAN feature list")

  def test_auxiliary_request_ecu_whitelist(self):
    # Asserts only auxiliary Ecus can exist in database for CAN-FD cars
    whitelisted_ecus = {ecu for r in FW_QUERY_CONFIG.requests for ecu in r.whitelist_ecus if r.auxiliary}

    for car_model in CANFD_CAR:
      ecus = {fw[0] for fw in FW_VERSIONS[car_model].keys()}
      ecus_not_in_whitelist = ecus - whitelisted_ecus
      ecu_strings = ", ".join([f'Ecu.{ECU_NAME[ecu]}' for ecu in ecus_not_in_whitelist])
      self.assertEqual(len(ecus_not_in_whitelist), 0, f'{car_model}: Car model has ECUs not in auxiliary request whitelists: {ecu_strings}')

  def test_platform_code_ecus_available(self):
    no_eps_platforms = CANFD_CAR | {CAR.KIA_SORENTO, CAR.KIA_OPTIMA_G4, CAR.KIA_OPTIMA_G4_FL,
                                    CAR.SONATA_LF, CAR.TUCSON, CAR.GENESIS_G90, CAR.GENESIS_G80}

    # Asserts ECU keys essential for fuzzy fingerprinting are available on all platforms
    for car_model, ecus in FW_VERSIONS.items():
      with self.subTest(car_model=car_model):
        for fuzzy_ecu in FW_QUERY_CONFIG.platform_code_ecus:
          if fuzzy_ecu in (Ecu.fwdRadar, Ecu.eps) and car_model == CAR.HYUNDAI_GENESIS:
            continue
          if fuzzy_ecu == Ecu.eps and car_model in no_eps_platforms:
            continue
          self.assertIn(fuzzy_ecu, [e[0] for e in ecus])

  # def test_fuzzy_part_numbers(self):
  #   pattern =
  #   match =

  def test_fw_part_number(self):
    # Hyundai places the ECU part number in their FW versions, assert all parsable
    # Some examples of valid formats: '56310-L0010', '56310L0010', '56310/M6300'
    for car_model, ecus in FW_VERSIONS.items():
      with self.subTest(car_model=car_model):
        if car_model == CAR.HYUNDAI_GENESIS:
          raise unittest.SkipTest("No part numbers for car model")

        for ecu, fws in ecus.items():
          if ecu[0] not in FW_QUERY_CONFIG.platform_code_ecus:
            continue

          for fw in fws:
            match = PART_NUMBER_FW_PATTERN.search(fw)
            self.assertIsNotNone(match, fw)

  def test_fuzzy_fw_dates(self):
    # Some newer platforms have date codes in a different format we don't yet parse,
    # for now assert date format is consistent for all FW across each platform
    for car_model, ecus in FW_VERSIONS.items():
      with self.subTest(car_model=car_model):
        for ecu, fws in ecus.items():
          if ecu[0] not in FW_QUERY_CONFIG.platform_code_ecus:
            continue

          codes = set()
          for fw in fws:
            codes |= FW_QUERY_CONFIG.fuzzy_get_platform_codes([fw])

          # Either no parts should be parsed or all parts should be parsed
          self.assertEqual(len({b"-" in code[0] for code in codes}), 1)
          # Same with dates
          self.assertEqual(len({code[1] is not None for code in codes}), 1)

  def test_fuzzy_platform_codes(self):
    # Asserts basic platform code parsing behavior
    results = FW_QUERY_CONFIG.fuzzy_get_platform_codes([b'\xf1\x00DH LKAS 1.1 -150210'])
    self.assertEqual(results, {(b"DH", b"150210")})

    # Some cameras and all radars do not have dates
    results = FW_QUERY_CONFIG.fuzzy_get_platform_codes([b'\xf1\x00AEhe SCC H-CUP      1.01 1.01 96400-G2000         '])
    self.assertEqual(results, {(b'AEhe-G2000', None)})

    results = FW_QUERY_CONFIG.fuzzy_get_platform_codes([b'\xf1\x00CV1_ RDR -----      1.00 1.01 99110-CV000         '])
    self.assertEqual(results, {(b"CV1-CV000", None)})

    results = FW_QUERY_CONFIG.fuzzy_get_platform_codes([
      b'\xf1\x00DH LKAS 1.1 -150210',
      b'\xf1\x00AEhe SCC H-CUP      1.01 1.01 96400-G2000         ',
      b'\xf1\x00CV1_ RDR -----      1.00 1.01 99110-CV000         ',
    ])
    self.assertEqual(results, {(b"DH", b"150210"), (b'AEhe-G2000', None), (b"CV1-CV000", None)})

    results = FW_QUERY_CONFIG.fuzzy_get_platform_codes([
      b'\xf1\x00LX2 MFC  AT USA LHD 1.00 1.07 99211-S8100 220222',
      b'\xf1\x00LX2 MFC  AT USA LHD 1.00 1.08 99211-S8100 211103',
      b'\xf1\x00ON  MFC  AT USA LHD 1.00 1.01 99211-S9100 190405',
      b'\xf1\x00ON  MFC  AT USA LHD 1.00 1.03 99211-S9100 190720',
    ])
    self.assertEqual(results, {(b"LX2-S8100", b"220222"), (b"LX2-S8100", b"211103"),
                               (b"ON-S9100", b"190405"), (b"ON-S9100", b"190720")})

  def test_excluded_platforms_new(self):
    # Asserts a list of platforms that will not fuzzy fingerprint with platform codes due to them being shared.
    # This list can be shrunk as we combine platforms and detect features
    excluded_platforms = {
      CAR.GENESIS_G70,
      CAR.GENESIS_G70_2020,
      CAR.TUCSON_4TH_GEN,
      CAR.TUCSON_HYBRID_4TH_GEN,
    }

    platforms_with_shared_codes = set()
    for platform, fw_by_addr in FW_VERSIONS.items():
      car_fw = []
      for ecu, fw_versions in fw_by_addr.items():
        # Only test fuzzy ECUs so excluded platforms for platforms codes are accurate
        # We can still fuzzy match via exact FW matches
        ecu_name, addr, sub_addr = ecu
        # TODO: if we use match_fw_for_car we need this continue
        # if ecu_name not in FW_QUERY_CONFIG.platform_code_ecus:
        #   continue

        for fw in fw_versions:
          car_fw.append({"ecu": ecu_name, "fwVersion": fw, 'brand': 'hyundai',
                         "address": addr, "subAddress": 0 if sub_addr is None else sub_addr})

      CP = car.CarParams.new_message(carFw=car_fw)
      # _, matches = match_fw_to_car(CP.carFw, allow_exact=False, log=False)
      matches = FW_QUERY_CONFIG.match_fw_to_car_fuzzy(build_fw_dict(CP.carFw, filter_brand='hyundai'))
      if len(matches) == 1:
        self.assertEqual(list(matches)[0], platform)
      else:
        platforms_with_shared_codes.add(platform)

    self.assertEqual(platforms_with_shared_codes, excluded_platforms)


if __name__ == "__main__":
  unittest.main()
