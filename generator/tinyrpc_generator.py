#!/usr/bin/env python3
"""
TinyRPC project generator.

The generator intentionally supports only a small, learning-friendly proto
subset: service blocks with unary rpc methods in the same file.
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path


TEMPLATE_SUFFIX = ".template"
DEFAULT_SERVER_PORT = "39999"


@dataclass(frozen=True)
class RpcMethod:
    name: str
    request_type: str
    response_type: str


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


def parse_proto_methods(proto_path: Path, service_name: str) -> list[RpcMethod]:
    content = proto_path.read_text(encoding="utf-8")
    service_pattern = re.compile(r"\bservice\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(?P<body>.*?)\}", re.DOTALL)
    rpc_pattern = re.compile(
        r"\brpc\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
        r"\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*"
        r"returns\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*;"
    )

    for match in service_pattern.finditer(content):
        if match.group(1) != service_name:
            continue

        methods = [
            RpcMethod(name=item.group(1), request_type=item.group(2), response_type=item.group(3))
            for item in rpc_pattern.finditer(match.group("body"))
        ]
        if not methods:
            raise ValueError(f"service has no rpc methods: {service_name}")
        return methods

    raise ValueError(f"service not found in proto: {service_name}")


def render_method_declarations(methods: list[RpcMethod]) -> str:
    lines: list[str] = []
    for method in methods:
        lines.extend(
            [
                f"    void {method.name}(",
                "        google::protobuf::RpcController *controller,",
                f"        const {method.request_type} *request,",
                f"        {method.response_type} *response,",
                "        google::protobuf::Closure *done) override;",
                "",
            ]
        )
    return "\n".join(lines).rstrip()


def render_method_definitions(service_name: str, methods: list[RpcMethod]) -> str:
    blocks: list[str] = []
    for method in methods:
        blocks.append(
            "\n".join(
                [
                    f"void {service_name}Impl::{method.name}(",
                    "    google::protobuf::RpcController * /*controller*/,",
                    f"    const {method.request_type} * /*request*/,",
                    f"    {method.response_type} * /*response*/,",
                    "    google::protobuf::Closure *done)",
                    "{",
                    f"    std::cout << \"[{service_name}] handle {method.name}\" << std::endl;",
                    "    if (done != nullptr) {",
                    "        done->Run();",
                    "    }",
                    "}",
                ]
            )
        )
    return "\n\n".join(blocks)


def render_client_calls(methods: list[RpcMethod]) -> str:
    lines: list[str] = []
    for method in methods:
        lines.extend(
            [
                f"    {method.request_type} {method.name}Request;",
                f"    {method.response_type} {method.name}Response;",
                f"    stub->{method.name}(controller, &{method.name}Request, &{method.name}Response, nullptr);",
                "    if (controller->Failed()) {",
                f"        std::cerr << \"[{method.name}] rpc failed: \"",
                "                  << controller->ErrorText() << std::endl;",
                "        return false;",
                "    }",
                f"    std::cout << \"[{method.name}] response: \"",
                f"              << {method.name}Response.ShortDebugString() << std::endl;",
            ]
        )
    return "\n".join(lines)


def make_replacements(proto_path: Path, service_name: str, methods: list[RpcMethod]) -> dict[str, str]:
    return {
        "{{SERVICE_NAME}}": service_name,
        "{{PROTO_FILE}}": proto_path.name,
        "{{PROTO_HEADER}}": f"{proto_path.stem}.pb.h",
        "{{SERVER_PORT}}": DEFAULT_SERVER_PORT,
        "{{RPC_METHOD_DECLARATIONS}}": render_method_declarations(methods),
        "{{RPC_METHOD_DEFINITIONS}}": render_method_definitions(service_name, methods),
        "{{CLIENT_CALLS}}": render_client_calls(methods),
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
        methods = parse_proto_methods(proto_path, service_name)
        replacements = make_replacements(proto_path, service_name, methods)

        copy_template_tree(template_dir, out_dir, replacements)
        copy_proto(proto_path, out_dir)
    except Exception as exc:  # noqa: BLE001 - CLI should report any validation/copy failure clearly.
        print(f"[generator] FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"[generator] generated {service_name} project at {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
