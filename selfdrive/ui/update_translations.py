#!/usr/bin/env python3
import argparse
import json
import os
from typing import Dict

from common.basedir import BASEDIR

UI_DIR = os.path.join(BASEDIR, "selfdrive", "ui")
TRANSLATIONS_DIR = os.path.join(UI_DIR, "translations")
LANGUAGES_FILE = os.path.join(TRANSLATIONS_DIR, "languages.json")


def update_translations(release: bool = False) -> Dict[str, str]:
  with open(LANGUAGES_FILE, "r") as f:
    translation_files = json.load(f)

  prev_translations = {}
  for name, file in translation_files.items():
    if not len(file):
      print(f"{name} has no translation file, skipping...")
      continue

    tr_file = os.path.join(TRANSLATIONS_DIR, f"{file}.ts")
    with open(tr_file, "r") as f:
      prev_translations[name] = f.read()

    ret = os.system(f"lupdate -recursive {UI_DIR} -ts {tr_file}")
    assert ret == 0

    if release:
      ret = os.system(f"lrelease {tr_file}")
      assert ret == 0

  return prev_translations


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description="Update translation files for UI",
                                   formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("--release", action="store_true", help="Create compiled QM translation files used by UI")
  args = parser.parse_args()

  update_translations(args.release)
