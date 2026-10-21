#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
# SPDX-License-Identifier: BSL-1.0
"""GitHub Release の本文 (Markdown) を組み立てる。

方針 (2026-09-24):
  - 本文の中心は **そのリリースの主要変更点**。固定の機能一覧は載せない (README にある)。
  - 変更点は docs/releases/<tag>.md (手書き、利用者向けの言葉で) があればそれを使い、
    無ければ直前の tag からのコミット一覧で代用する (merge / ci / chore / build は除く)。
    直前の tag = stable なら直前の stable、pre-release なら (チャンネル問わず) 直前の tag。
  - ADR 番号や内部の設計用語は書かない (利用者は知らない)。
  - 恒常的な案内 (対応ボード / 書き込み方法 / OTA / v0.12 以前からの更新) は短く末尾に。

使い方 (release.yml の release ジョブから):
    release_notes.py --tag v0.15.0-alpha.1 --boards '["cores3","atoms3r"]' \\
        --repo ciniml/stackchan-idf --commit <sha> --build-date "..." > release_notes.md
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
from release_channels import Tag, parse_tag  # noqa: E402

BOARD_LABELS = {
    # [EN-FR fork] notes de version en français
    "cores3": "**CoreS3 + base Stack-chan / Takao** (`firmware-{v}-cores3.zip`, flash 16 Mo / PSRAM Quad)",
    "atoms3r": "**AtomS3R + Atomic ECHO BASE** (`firmware-{v}-atoms3r.zip`, flash 8 Mo / PSRAM Octal)",
    "atoms3": "**AtomS3 + Atomic ECHO BASE**, profil allégé (`firmware-{v}-atoms3.zip`, flash 8 Mo / sans PSRAM)",
    "stopwatch": "**M5 StopWatch (C152)**, AMOLED rond 466×466 (`firmware-{v}-stopwatch.zip`, flash 16 Mo / PSRAM Octal)",
}

# 自動生成の変更点から落とすコミット (利用者に意味が無いもの)。
_SKIP_SUBJECT = re.compile(r"^(Merge |ci\(|ci:|chore|build\(|build:|docs\(|docs:|test\(|test:)")


def previous_tag(tag: Tag, all_tags: list[str]) -> Optional[str]:
    """変更点の起点。stable なら直前の stable、pre-release なら直前の tag (チャンネル問わず)。"""
    parsed = [t for t in (parse_tag(s) for s in all_tags) if t and t.sort_key < tag.sort_key]
    if not tag.prerelease:
        parsed = [t for t in parsed if not t.prerelease]
    if not parsed:
        return None
    return max(parsed, key=lambda t: t.sort_key).text


def git_tags(repo_dir: Path) -> list[str]:
    out = subprocess.run(["git", "tag", "--list", "v*"], cwd=repo_dir, capture_output=True, text=True, check=True)
    return out.stdout.split()


def auto_changes(repo_dir: Path, prev: Optional[str], tag: str) -> list[str]:
    rng = f"{prev}..{tag}" if prev else tag
    out = subprocess.run(["git", "log", "--no-merges", "--format=%s", rng], cwd=repo_dir,
                         capture_output=True, text=True, check=True)
    return [s for s in out.stdout.splitlines() if s.strip() and not _SKIP_SUBJECT.match(s)]


def hand_written(repo_dir: Path, tag: str) -> Optional[str]:
    p = repo_dir / "docs" / "releases" / f"{tag}.md"
    return p.read_text(encoding="utf-8").strip() if p.exists() else None


def build_body(tag_text: str, boards: list[str], repo: str, commit: str, build_date: str, idf: str,
               repo_dir: Path) -> str:
    tag = parse_tag(tag_text)
    if tag is None:
        raise SystemExit(f"bad tag {tag_text}")
    owner, name = repo.split("/", 1)
    pages = f"https://{owner}.github.io/{name}"
    lines: list[str] = [f"# Firmware Stack-chan EN-FR {tag_text}", ""]

    if tag.prerelease:
        lines += [
            f"> **Version {tag.channel} (pré-version)** : build de test. Cartes incluses ci-dessous ; la version "
            f"stable est [Latest](https://github.com/{repo}/releases/latest). Dans le flasheur web, cocher "
            f"l'affichage des préversions (canal **{tag.channel}**) pour la voir.",
            "",
        ]

    prev = previous_tag(tag, git_tags(repo_dir))
    notes = hand_written(repo_dir, tag_text)
    lines += ["## Changements" + (f" (depuis {prev})" if prev else ""), ""]
    if notes:
        lines += [notes, ""]
    else:
        changes = auto_changes(repo_dir, prev, tag_text)
        lines += [f"- {c}" for c in changes] or ["- (aucun changement indiqué)"]
        lines += [""]
    if prev:
        lines += [f"Tous les commits : https://github.com/{repo}/compare/{prev}...{tag_text}", ""]

    lines += ["## Cartes (firmwares inclus dans cette version)", ""]
    lines += [f"- {BOARD_LABELS[b].format(v=tag_text)}" for b in boards]
    lines += ["", "Le mode de PSRAM est fixé par le bootloader : il y a un firmware par carte.", ""]

    lines += [
        "## Installation",
        "",
        "### Flasheur web (Chrome / Edge)",
        f"Sur <{pages}/>, choisir cette version et la carte, puis écrire"
        + (f" (cocher les préversions, canal **{tag.channel}**)." if tag.prerelease else "."),
        "",
        "### Mise à jour par le réseau (OTA)",
        "Page de réglages Wi-Fi du robot (`http://<adresse-du-robot>/`), onglet Maintenance, « Mise à jour du "
        "firmware » : le robot télécharge cette version et redémarre (environ 30 s). Également possible depuis "
        f"<{pages}/settings.html> en Bluetooth si celui-ci est actif.",
        "",
        "### Ligne de commande (esptool)",
        "Décompresser le ZIP de la carte et écrire l'image complète en 0x0 (les réglages sont effacés) :",
        "```bash",
        f"esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash 0x0 firmware-{tag_text}-<board>.bin",
        "```",
        "Pour garder les réglages, écrire les morceaux listés dans `flash_args` : `write_flash @flash_args`",
        "",
        f"Fonctions et réglages : [README.fr.md](https://github.com/{repo}/blob/main/README.fr.md). "
        f"Licences et attributions : <{pages}/licenses.html>.",
        "",
        "## Build",
        f"- ESP-IDF {idf} / ESP32-S3",
        f"- Date : {build_date}",
        f"- Commit : {commit}",
    ]
    return "\n".join(lines) + "\n"


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--boards", required=True, help="JSON 配列")
    ap.add_argument("--repo", default="ciniml/stackchan-idf")
    ap.add_argument("--commit", default="")
    ap.add_argument("--build-date", default="")
    ap.add_argument("--idf", default="v5.5.5")
    ap.add_argument("--repo-dir", type=Path, default=Path(__file__).resolve().parent.parent)
    a = ap.parse_args(argv)
    sys.stdout.write(build_body(a.tag, json.loads(a.boards), a.repo, a.commit, a.build_date, a.idf, a.repo_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
