#!/usr/bin/env python3
"""
TinyRPC project generator.

Task 79 only copies a fixed project template and substitutes a few basic
variables. Proto service/method parsing is intentionally left for the next task.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


TEMPLATE_SUFFIX = ".template"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a minimal TinyRPC service project from templates.",
    )
    parser.add_argument(
        "--proto",
        required=True,
        help="Path to the proto file used by the generated project.",
    )
    parser.add_argument(
        "--service",
        required=True,
        help="Service name used in generated placeholders.",
    )
    parser.add_argument(
        "--out",
        required=True,
        help="Output directory. It is created automatically when missing.",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> tuple[Path, str, Path]:
    proto_path = Path(args.proto).expanduser().resolve()
    service_name = args.service.strip()
    out_dir = Path(args.out).expanduser().resolve()

    if not proto_path.is_file():
        raise ValueError(f"proto file not found: {proto_path}")
    if not service_name:
        raise ValueError("service name must not be empty")
    if any(ch.isspace() for ch in service_name):
        raise ValueError("service name must not contain whitespace")

    return proto_path, service_name, out_dir


def make_replacements(proto_path: Path, service_name: str) -> dict[str, str]:
    return {
        "{{SERVICE_NAME}}": service_name,
        "{{PROTO_FILE}}": proto_path.name,
    }


def render_text(text: str, replacements: dict[str, str]) -> str:
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def copy_template_tree(template_dir: Path, out_dir: Path, replacements: dict[str, str]) -> None:
    if not template_dir.is_dir():
        raise ValueError(f"template directory not found: {template_dir}")

    out_dir.mkdir(parents=True, exist_ok=True)

    for source in sorted(template_dir.rglob("*")):
        relative = source.relative_to(template_dir)
        target_name = relative.name
        if target_name.endswith(TEMPLATE_SUFFIX):
            target_name = target_name[: -len(TEMPLATE_SUFFIX)]
        target = out_dir / relative.parent / target_name

        if source.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue

        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            content = source.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            shutil.copyfile(source, target)
            continue

        target.write_text(render_text(content, replacements), encoding="utf-8")


def copy_proto(proto_path: Path, out_dir: Path) -> None:
    target = out_dir / proto_path.name
    shutil.copyfile(proto_path, target)


def main() -> int:
    args = parse_args()
    try:
        proto_path, service_name, out_dir = validate_args(args)
        generator_dir = Path(__file__).resolve().parent
        template_dir = generator_dir / "template"
        replacements = make_replacements(proto_path, service_name)

        copy_template_tree(template_dir, out_dir, replacements)
        copy_proto(proto_path, out_dir)
    except Exception as exc:  # noqa: BLE001 - CLI should report any validation/copy failure clearly.
        print(f"[generator] FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"[generator] generated {service_name} project at {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
