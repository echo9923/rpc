#!/usr/bin/env python3
"""
TinyRPC project generator.

The generator supports two layouts:
  - simple: keeps the original flat learning project layout.
  - full: creates a closer-to-original project skeleton with pb, service,
    interface, comm, test_client, bin, conf, log, lib and obj directories.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


TEMPLATE_SUFFIX = ".template"
DEFAULT_SERVER_PORT = "39999"
DESCRIPTOR_SUFFIX = ".descriptor.pb"


@dataclass(frozen=True)
class RpcMethod:
    name: str
    request_proto_type: str
    response_proto_type: str
    request_local_type: str
    response_local_type: str
    request_cpp_type: str
    response_cpp_type: str
    interface_class_name: str
    interface_member_name: str
    interface_file_stem: str
    variable_prefix: str


@dataclass(frozen=True)
class ServiceSpec:
    package: str
    name: str
    methods: list[RpcMethod]

    @property
    def full_name(self) -> str:
        if not self.package:
            return self.name
        return f"{self.package}.{self.name}"

    @property
    def namespace_parts(self) -> list[str]:
        if not self.package:
            return []
        return self.package.split(".")

    @property
    def cpp_prefix(self) -> str:
        if not self.namespace_parts:
            return ""
        return "::".join(self.namespace_parts) + "::"

    @property
    def qualified_impl_name(self) -> str:
        return f"{self.cpp_prefix}{self.name}Impl"

    @property
    def qualified_stub_name(self) -> str:
        return f"{self.cpp_prefix}{self.name}_Stub"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a TinyRPC service project from templates.",
    )
    parser.add_argument(
        "--proto",
        required=True,
        help="Path to the proto file used by the generated project.",
    )
    parser.add_argument(
        "--service",
        default="",
        help=(
            "Target service name. It may be either ServiceName or package.ServiceName. "
            "When omitted, the proto must contain exactly one service."
        ),
    )
    parser.add_argument(
        "--out",
        required=True,
        help="Output directory. It is created automatically when missing.",
    )
    parser.add_argument(
        "--layout",
        choices=("simple", "full"),
        default="simple",
        help="Generated project layout. Defaults to simple for backward compatibility.",
    )
    parser.add_argument(
        "--project",
        default="",
        help="Project directory name used by --layout full. Defaults to a snake_case service name.",
    )
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> tuple[Path, str, Path, str, str]:
    proto_path = Path(args.proto).expanduser().resolve()
    service_name = args.service.strip()
    out_dir = Path(args.out).expanduser().resolve()
    layout = args.layout
    project_name = args.project.strip()

    if not proto_path.is_file():
        raise ValueError(f"proto file not found: {proto_path}")
    if any(ch.isspace() for ch in service_name):
        raise ValueError("service name must not contain whitespace")
    if project_name and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", project_name):
        raise ValueError("project name must use only letters, digits and underscores")

    return proto_path, service_name, out_dir, layout, project_name


def to_snake_case(name: str) -> str:
    value = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"[^A-Za-z0-9]+", "_", value)
    return value.strip("_").lower()


def to_pascal_case(name: str) -> str:
    words = re.split(r"[^A-Za-z0-9]+", name)
    return "".join(word[:1].upper() + word[1:] for word in words if word)


def to_lower_camel_case(name: str) -> str:
    pascal = to_pascal_case(name)
    if not pascal:
        return "value"
    return pascal[:1].lower() + pascal[1:]


def normalize_proto_type(proto_type: str) -> str:
    return proto_type[1:] if proto_type.startswith(".") else proto_type


def cpp_type_from_proto(proto_type: str) -> str:
    return normalize_proto_type(proto_type).replace(".", "::")


def local_type_from_proto(proto_type: str, package: str) -> str:
    normalized = normalize_proto_type(proto_type)
    if package and normalized.startswith(package + "."):
        normalized = normalized[len(package) + 1:]
    return normalized.replace(".", "::")


def make_method(package: str, name: str, request_type: str, response_type: str) -> RpcMethod:
    interface_class_name = f"{to_pascal_case(name)}Interface"
    interface_file_stem = f"{to_snake_case(name)}_interface"
    return RpcMethod(
        name=name,
        request_proto_type=request_type,
        response_proto_type=response_type,
        request_local_type=local_type_from_proto(request_type, package),
        response_local_type=local_type_from_proto(response_type, package),
        request_cpp_type=cpp_type_from_proto(request_type),
        response_cpp_type=cpp_type_from_proto(response_type),
        interface_class_name=interface_class_name,
        interface_member_name=f"m_{to_lower_camel_case(name)}Interface",
        interface_file_stem=interface_file_stem,
        variable_prefix=to_lower_camel_case(name),
    )


def read_varint(data: bytes, offset: int) -> tuple[int, int]:
    shift = 0
    value = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, offset
        shift += 7
        if shift > 63:
            break
    raise ValueError("invalid descriptor varint")


def iter_descriptor_fields(data: bytes):
    offset = 0
    while offset < len(data):
        key, offset = read_varint(data, offset)
        field_number = key >> 3
        wire_type = key & 0x07

        if wire_type == 0:
            value, offset = read_varint(data, offset)
            yield field_number, wire_type, value
            continue
        if wire_type == 1:
            value = data[offset:offset + 8]
            offset += 8
            yield field_number, wire_type, value
            continue
        if wire_type == 2:
            length, offset = read_varint(data, offset)
            value = data[offset:offset + length]
            offset += length
            yield field_number, wire_type, value
            continue
        if wire_type == 5:
            value = data[offset:offset + 4]
            offset += 4
            yield field_number, wire_type, value
            continue

        raise ValueError(f"unsupported descriptor wire type: {wire_type}")


def decode_descriptor_string(value: bytes) -> str:
    return value.decode("utf-8")


def parse_method_descriptor(data: bytes, package: str) -> RpcMethod:
    name = ""
    request_type = ""
    response_type = ""
    for field_number, wire_type, value in iter_descriptor_fields(data):
        if wire_type != 2:
            continue
        if field_number == 1:
            name = decode_descriptor_string(value)
        elif field_number == 2:
            request_type = decode_descriptor_string(value)
        elif field_number == 3:
            response_type = decode_descriptor_string(value)

    if not name or not request_type or not response_type:
        raise ValueError("descriptor method is incomplete")
    return make_method(package, name, request_type, response_type)


def parse_service_descriptor(data: bytes, package: str) -> ServiceSpec:
    name = ""
    methods: list[RpcMethod] = []
    for field_number, wire_type, value in iter_descriptor_fields(data):
        if wire_type != 2:
            continue
        if field_number == 1:
            name = decode_descriptor_string(value)
        elif field_number == 2:
            methods.append(parse_method_descriptor(value, package))

    if not name:
        raise ValueError("descriptor service name is empty")
    if not methods:
        raise ValueError(f"service has no rpc methods: {name}")
    return ServiceSpec(package=package, name=name, methods=methods)


def parse_file_descriptor(data: bytes) -> tuple[str, list[ServiceSpec], str]:
    file_name = ""
    package = ""
    service_blobs: list[bytes] = []

    for field_number, wire_type, value in iter_descriptor_fields(data):
        if wire_type != 2:
            continue
        if field_number == 1:
            file_name = decode_descriptor_string(value)
        elif field_number == 2:
            package = decode_descriptor_string(value)
        elif field_number == 6:
            service_blobs.append(value)

    services = [parse_service_descriptor(blob, package) for blob in service_blobs]
    return file_name, services, package


def parse_descriptor_services(descriptor_path: Path, proto_path: Path) -> list[ServiceSpec]:
    descriptor_data = descriptor_path.read_bytes()
    services: list[ServiceSpec] = []
    target_proto_name = proto_path.name

    for field_number, wire_type, value in iter_descriptor_fields(descriptor_data):
        if field_number != 1 or wire_type != 2:
            continue
        file_name, file_services, _ = parse_file_descriptor(value)
        if Path(file_name).name == target_proto_name:
            services.extend(file_services)

    if not services:
        raise ValueError(f"descriptor-set has no service for proto: {target_proto_name}")
    return services


def parse_proto_services(proto_path: Path) -> list[ServiceSpec]:
    content = proto_path.read_text(encoding="utf-8")
    package_match = re.search(
        r"^\s*package\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*;",
        content,
        re.MULTILINE,
    )
    package = package_match.group(1) if package_match else ""
    type_pattern = r"\.?[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*"
    service_pattern = re.compile(
        r"\bservice\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{(?P<body>.*?)\}",
        re.DOTALL,
    )
    rpc_pattern = re.compile(
        r"\brpc\s+([A-Za-z_][A-Za-z0-9_]*)\s*"
        rf"\(\s*({type_pattern})\s*\)\s*"
        r"returns\s*"
        rf"\(\s*({type_pattern})\s*\)\s*;",
    )

    services: list[ServiceSpec] = []
    for service_match in service_pattern.finditer(content):
        service_name = service_match.group(1)
        methods = [
            make_method(package, item.group(1), item.group(2), item.group(3))
            for item in rpc_pattern.finditer(service_match.group("body"))
        ]
        if not methods:
            raise ValueError(f"service has no rpc methods: {service_name}")
        services.append(ServiceSpec(package=package, name=service_name, methods=methods))

    if not services:
        raise ValueError(f"service not found in proto: {proto_path}")
    return services


def select_service(services: list[ServiceSpec], requested_name: str) -> ServiceSpec:
    if not requested_name:
        if len(services) == 1:
            return services[0]
        names = ", ".join(service.full_name for service in services)
        raise ValueError(f"--service is required when proto has multiple services: {names}")

    normalized = requested_name[1:] if requested_name.startswith(".") else requested_name
    for service in services:
        if normalized == service.name or normalized == service.full_name:
            return service

    raise ValueError(f"service not found in proto: {requested_name}")


def run_protoc(proto_path: Path, pb_dir: Path, descriptor_path: Path) -> None:
    protoc = shutil.which("protoc")
    if protoc is None:
        raise ValueError(
            "protoc is required but not found. Install protobuf-compiler in the Linux build environment."
        )

    pb_dir.mkdir(parents=True, exist_ok=True)
    descriptor_path.parent.mkdir(parents=True, exist_ok=True)

    command = [
        protoc,
        f"--proto_path={proto_path.parent}",
        f"--cpp_out={pb_dir}",
        f"--descriptor_set_out={descriptor_path}",
        "--include_imports",
        proto_path.name,
    ]
    # subprocess.run 调用外部 protoc：cwd 限定为 proto 所在目录，参数分别指定
    # proto 搜索路径、C++ 输出目录、descriptor-set 输出路径和输入 proto 文件名。
    result = subprocess.run(
        command,
        cwd=proto_path.parent,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        if not detail:
            detail = f"exit code {result.returncode}"
        raise ValueError(f"protoc failed: {detail}")


def namespace_open(package: str) -> str:
    if not package:
        return ""
    lines = [f"namespace {part} {{" for part in package.split(".")]
    return "\n".join(lines) + "\n\n"


def namespace_close(package: str) -> str:
    if not package:
        return ""
    lines = [f"}}  // namespace {part}" for part in reversed(package.split("."))]
    return "\n".join(lines) + "\n"


def render_method_declarations(methods: list[RpcMethod]) -> str:
    lines: list[str] = []
    for method in methods:
        lines.extend(
            [
                f"    void {method.name}(",
                "        google::protobuf::RpcController *controller,",
                f"        const {method.request_local_type} *request,",
                f"        {method.response_local_type} *response,",
                "        google::protobuf::Closure *done) override;",
                "",
            ]
        )
    return "\n".join(lines).rstrip()


def render_simple_method_definitions(service: ServiceSpec) -> str:
    blocks: list[str] = []
    for method in service.methods:
        blocks.append(
            "\n".join(
                [
                    f"void {service.name}Impl::{method.name}(",
                    "    google::protobuf::RpcController * /*controller*/,",
                    f"    const {method.request_local_type} *request,",
                    f"    {method.response_local_type} *response,",
                    "    google::protobuf::Closure *done)",
                    "{",
                    "    // request 是 protoc 生成的请求消息，response 是当前方法需要填充的响应消息。",
                    "    // 默认实现只保留空响应，业务方可以在这里读取 request 并写入 response。",
                    "    (void)request;",
                    "    if (response != nullptr) {",
                    "        response->Clear();",
                    "    }",
                    f"    std::cout << \"[{service.name}] handle {method.name}\" << std::endl;",
                    "    if (done != nullptr) {",
                    "        done->Run();",
                    "    }",
                    "}",
                ]
            )
        )
    return "\n\n".join(blocks)


def render_full_service_method_definitions(service: ServiceSpec) -> str:
    blocks: list[str] = []
    for method in service.methods:
        blocks.append(
            "\n".join(
                [
                    f"void {service.name}Impl::{method.name}(",
                    "    google::protobuf::RpcController *controller,",
                    f"    const {method.request_local_type} *request,",
                    f"    {method.response_local_type} *response,",
                    "    google::protobuf::Closure *done)",
                    "{",
                    "    // request 是 protoc 生成的请求消息，只读传入 interface 层。",
                    "    // response 是 protoc 生成的响应消息，由 interface 层负责填充。",
                    "    try {",
                    f"        {method.interface_member_name}.handle(request, response);",
                    "    } catch (const BusinessException& exc) {",
                    "        if (controller != nullptr) {",
                    "            controller->SetFailed(exc.what());",
                    "        }",
                    "    } catch (const std::exception& exc) {",
                    "        if (controller != nullptr) {",
                    "            controller->SetFailed(exc.what());",
                    "        }",
                    "    }",
                    "",
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
                f"    {method.request_cpp_type} {method.variable_prefix}Request;",
                f"    {method.response_cpp_type} {method.variable_prefix}Response;",
                "    controller->Reset();",
                f"    stub->{method.name}(",
                "        controller,",
                f"        &{method.variable_prefix}Request,",
                f"        &{method.variable_prefix}Response,",
                "        nullptr);",
                "    if (controller->Failed()) {",
                f"        std::cerr << \"[{method.name}] rpc failed: \"",
                "                  << controller->ErrorText() << std::endl;",
                "        return false;",
                "    }",
                f"    std::cout << \"[{method.name}] response: \"",
                f"              << {method.variable_prefix}Response.ShortDebugString() << std::endl;",
            ]
        )
    return "\n".join(lines)


def render_simple_interface_header_body(service: ServiceSpec) -> str:
    return "\n".join(
        [
            f"class {service.name}Impl : public {service.name} {{",
            " public:",
            render_method_declarations(service.methods),
            "};",
        ]
    )


def render_simple_server_header_body(service: ServiceSpec) -> str:
    return "\n".join(
        [
            f"class {service.name}Server {{",
            " public:",
            "    void start();",
            "",
            " private:",
            f"    {service.qualified_impl_name} m_service;",
            "};",
        ]
    )


def render_simple_server_source_body(service: ServiceSpec) -> str:
    return "\n".join(
        [
            f"void {service.name}Server::start()",
            "{",
            f"    std::cout << \"[{service.name}] start placeholder from generated server\" << std::endl;",
            "}",
        ]
    )


def render_interface_member_includes(project_name: str, methods: list[RpcMethod]) -> str:
    return "\n".join(
        f'#include "{project_name}/interface/{method.interface_file_stem}.h"'
        for method in methods
    )


def render_interface_members(methods: list[RpcMethod]) -> str:
    return "\n".join(
        f"    {method.interface_class_name} {method.interface_member_name};"
        for method in methods
    )


def render_full_service_header_body(service: ServiceSpec) -> str:
    body: list[str] = [
        f"class {service.name}Impl : public {service.name} {{",
        " public:",
        render_method_declarations(service.methods),
        "",
        " private:",
        render_interface_members(service.methods),
        "};",
    ]
    return "\n".join(body)


def render_full_interface_header_body(method: RpcMethod) -> str:
    return "\n".join(
        [
            f"class {method.interface_class_name} : public InterfaceBase {{",
            " public:",
            "    // request 是 protoc 生成的请求消息，response 是待填充的响应消息。",
            f"    void handle(const {method.request_local_type} *request, {method.response_local_type} *response);",
            "};",
        ]
    )


def render_full_interface_source_body(method: RpcMethod) -> str:
    return "\n".join(
        [
            f"void {method.interface_class_name}::handle(",
            f"    const {method.request_local_type} *request,",
            f"    {method.response_local_type} *response)",
            "{",
            "    if (request == nullptr || response == nullptr) {",
            "        throw BusinessException(-1, \"request or response is null\");",
            "    }",
            "",
            "    // 默认业务实现返回空响应，业务方可以根据 request 填充 response。",
            "    (void)request;",
            "    response->Clear();",
            "}",
        ]
    )


def render_app_source_list(items: list[str]) -> str:
    return "\n".join(f"    {item}" for item in items)


def build_common_replacements(
    service: ServiceSpec,
    proto_path: Path,
    project_name: str,
    proto_include: str,
    proto_source_rel: str,
    descriptor_rel: str,
    layout: str,
) -> dict[str, str]:
    return {
        "{{SERVICE_NAME}}": service.name,
        "{{SERVICE_FULL_NAME}}": service.full_name,
        "{{PROJECT_NAME}}": project_name,
        "{{PROTO_FILE}}": proto_path.name,
        "{{PROTO_STEM}}": proto_path.stem,
        "{{PROTO_HEADER}}": f"{proto_path.stem}.pb.h",
        "{{PROTO_INCLUDE}}": proto_include,
        "{{PROTO_SOURCE_REL}}": proto_source_rel,
        "{{DESCRIPTOR_REL}}": descriptor_rel,
        "{{SERVER_PORT}}": DEFAULT_SERVER_PORT,
        "{{LAYOUT_NAME}}": layout,
        "{{QUALIFIED_SERVICE_IMPL}}": service.qualified_impl_name,
        "{{QUALIFIED_STUB_TYPE}}": service.qualified_stub_name,
        "{{CLIENT_CALLS}}": render_client_calls(service.methods),
        "{{RPC_METHOD_DECLARATIONS}}": render_method_declarations(service.methods),
        "{{PACKAGE_NAMESPACE_OPEN}}": namespace_open(service.package),
        "{{PACKAGE_NAMESPACE_CLOSE}}": namespace_close(service.package),
    }


def render_text(text: str, replacements: dict[str, str]) -> str:
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def render_template(template_dir: Path, template_name: str, replacements: dict[str, str]) -> str:
    template_path = template_dir / template_name
    if not template_path.is_file():
        raise ValueError(f"template file not found: {template_path}")
    return render_text(template_path.read_text(encoding="utf-8"), replacements)


def write_template(
    template_dir: Path,
    template_name: str,
    target: Path,
    replacements: dict[str, str],
) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(render_template(template_dir, template_name, replacements), encoding="utf-8")


def copy_proto_and_generate(
    proto_path: Path,
    target_proto: Path,
    pb_dir: Path,
    descriptor_path: Path,
) -> None:
    target_proto.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(proto_path, target_proto)
    run_protoc(proto_path, pb_dir, descriptor_path)


def chmod_script(path: Path) -> None:
    current_mode = path.stat().st_mode
    path.chmod(current_mode | 0o755)


def discover_services(proto_path: Path, descriptor_path: Path) -> list[ServiceSpec]:
    try:
        return parse_descriptor_services(descriptor_path, proto_path)
    except Exception as exc:  # noqa: BLE001 - fallback keeps descriptor parsing optional.
        print(f"[generator] WARN: descriptor parse failed, fallback to proto parser: {exc}", file=sys.stderr)
        return parse_proto_services(proto_path)


def generate_simple_project(
    template_dir: Path,
    proto_path: Path,
    service: ServiceSpec,
    out_dir: Path,
) -> None:
    project_name = to_snake_case(service.name)
    target_proto = out_dir / proto_path.name
    descriptor_path = out_dir / f"{proto_path.stem}{DESCRIPTOR_SUFFIX}"
    proto_source_rel = f"{proto_path.stem}.pb.cc"

    replacements = build_common_replacements(
        service=service,
        proto_path=proto_path,
        project_name=project_name,
        proto_include=f"{proto_path.stem}.pb.h",
        proto_source_rel=proto_source_rel,
        descriptor_rel=descriptor_path.name,
        layout="simple",
    )
    replacements.update(
        {
            "{{SERVICE_HEADER_INCLUDE}}": "server.h",
            "{{INTERFACE_HEADER_INCLUDE}}": "interface.h",
            "{{INTERFACE_HEADER_INCLUDES}}": (
                f'#include "{proto_path.stem}.pb.h"\n\n'
                "#include <google/protobuf/service.h>"
            ),
            "{{INTERFACE_HEADER_BODY}}": render_simple_interface_header_body(service),
            "{{INTERFACE_SOURCE_INCLUDES}}": "#include <iostream>",
            "{{INTERFACE_SOURCE_BODY}}": render_simple_method_definitions(service),
            "{{SERVER_HEADER_INCLUDES}}": '#include "interface.h"',
            "{{SERVER_HEADER_BODY}}": render_simple_server_header_body(service),
            "{{SERVER_SOURCE_INCLUDES}}": "#include <iostream>",
            "{{SERVER_SOURCE_BODY}}": render_simple_server_source_body(service),
            "{{GENERATED_APP_SOURCES}}": render_app_source_list(
                [
                    "main.cc",
                    "server.cc",
                    "interface.cc",
                ]
            ),
            "{{CLIENT_SOURCE_REL}}": "client.cc",
            "{{SERVER_BIN_REL}}": f"build/{service.name}_server",
            "{{CLIENT_BIN_REL}}": f"build/{service.name}_client",
            "{{CONFIG_FILE_REL}}": "conf.xml",
            "{{LOG_FILE_REL}}": f"{service.name}.log",
            "{{LOG_DIR}}": "logs",
            "{{CMAKE_OUTPUT_PROPERTIES}}": "",
            "{{README_LAYOUT_NOTE}}": (
                "The simple layout keeps source files in the project root for the staged learning workflow."
            ),
        }
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    copy_proto_and_generate(proto_path, target_proto, out_dir, descriptor_path)

    for template_name in [
        "CMakeLists.txt.template",
        "README.md.template",
        "conf.xml.template",
        "interface.h.template",
        "interface.cc.template",
        "main.cc.template",
        "server.h.template",
        "server.cc.template",
        "client.cc.template",
        "run.sh.template",
        "shutdown.sh.template",
    ]:
        target_name = template_name[: -len(TEMPLATE_SUFFIX)]
        write_template(template_dir, template_name, out_dir / target_name, replacements)

    chmod_script(out_dir / "run.sh")
    chmod_script(out_dir / "shutdown.sh")


def generate_full_project(
    template_dir: Path,
    proto_path: Path,
    service: ServiceSpec,
    out_dir: Path,
    requested_project_name: str,
) -> None:
    project_name = requested_project_name or to_snake_case(service.name)
    project_dir = out_dir / project_name
    pb_dir = project_dir / "pb"
    target_proto = pb_dir / proto_path.name
    descriptor_path = pb_dir / f"{proto_path.stem}{DESCRIPTOR_SUFFIX}"
    proto_include = f"{project_name}/pb/{proto_path.stem}.pb.h"
    proto_source_rel = f"{project_name}/pb/{proto_path.stem}.pb.cc"

    replacements = build_common_replacements(
        service=service,
        proto_path=proto_path,
        project_name=project_name,
        proto_include=proto_include,
        proto_source_rel=proto_source_rel,
        descriptor_rel=f"{project_name}/pb/{descriptor_path.name}",
        layout="full",
    )

    interface_sources = [
        f"{project_name}/interface/interface_base.cc",
        *[
            f"{project_name}/interface/{method.interface_file_stem}.cc"
            for method in service.methods
        ],
    ]
    replacements.update(
        {
            "{{SERVICE_HEADER_INCLUDE}}": f"{project_name}/service/server.h",
            "{{SERVER_HEADER_INCLUDES}}": (
                f'#include "{proto_include}"\n'
                f'{render_interface_member_includes(project_name, service.methods)}\n\n'
                "#include <google/protobuf/service.h>"
            ),
            "{{SERVER_HEADER_BODY}}": render_full_service_header_body(service),
            "{{SERVER_SOURCE_INCLUDES}}": (
                f'#include "{project_name}/comm/business_exception.h"\n\n'
                "#include <exception>"
            ),
            "{{SERVER_SOURCE_BODY}}": render_full_service_method_definitions(service),
            "{{GENERATED_APP_SOURCES}}": render_app_source_list(
                [
                    f"{project_name}/service/main.cc",
                    f"{project_name}/service/server.cc",
                    *interface_sources,
                ]
            ),
            "{{CLIENT_SOURCE_REL}}": "test_client/test_tinyrpc_client.cc",
            "{{SERVER_BIN_REL}}": f"bin/{service.name}_server",
            "{{CLIENT_BIN_REL}}": f"bin/{service.name}_client",
            "{{CONFIG_FILE_REL}}": "conf/conf.xml",
            "{{LOG_FILE_REL}}": f"log/{service.name}.log",
            "{{LOG_DIR}}": "log",
            "{{CMAKE_OUTPUT_PROPERTIES}}": "\n".join(
                [
                    f'set_target_properties({service.name}_server PROPERTIES',
                    '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"',
                    ")",
                    f'set_target_properties({service.name}_client PROPERTIES',
                    '    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"',
                    ")",
                    "set_target_properties(mytinyrpc_generated_core PROPERTIES",
                    '    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"',
                    ")",
                ]
            ),
            "{{README_LAYOUT_NOTE}}": (
                f"The full layout stores framework-facing service code under `{project_name}/service`, "
                f"business interfaces under `{project_name}/interface`, generated protobuf files under "
                f"`{project_name}/pb`, and the runnable client under `test_client`."
            ),
        }
    )

    for directory in [
        out_dir / "bin",
        out_dir / "conf",
        out_dir / "log",
        out_dir / "lib",
        out_dir / "obj",
        project_dir / "service",
        project_dir / "interface",
        project_dir / "pb",
        project_dir / "comm",
        out_dir / "test_client",
    ]:
        directory.mkdir(parents=True, exist_ok=True)

    copy_proto_and_generate(proto_path, target_proto, pb_dir, descriptor_path)

    write_template(template_dir, "CMakeLists.txt.template", out_dir / "CMakeLists.txt", replacements)
    write_template(template_dir, "README.md.template", out_dir / "README.md", replacements)
    write_template(template_dir, "conf.xml.template", out_dir / "conf" / "conf.xml", replacements)
    write_template(template_dir, "main.cc.template", project_dir / "service" / "main.cc", replacements)
    write_template(template_dir, "server.h.template", project_dir / "service" / "server.h", replacements)
    write_template(template_dir, "server.cc.template", project_dir / "service" / "server.cc", replacements)
    write_template(template_dir, "client.cc.template", out_dir / "test_client" / "test_tinyrpc_client.cc", replacements)
    write_template(
        template_dir,
        "business_exception.h.template",
        project_dir / "comm" / "business_exception.h",
        replacements,
    )
    write_template(
        template_dir,
        "interface_base.h.template",
        project_dir / "interface" / "interface_base.h",
        replacements,
    )
    write_template(
        template_dir,
        "interface_base.cc.template",
        project_dir / "interface" / "interface_base.cc",
        replacements,
    )

    for method in service.methods:
        method_replacements = dict(replacements)
        method_replacements.update(
            {
                "{{INTERFACE_HEADER_INCLUDE}}": f"{project_name}/interface/{method.interface_file_stem}.h",
                "{{INTERFACE_HEADER_INCLUDES}}": (
                    f'#include "{project_name}/interface/interface_base.h"\n'
                    f'#include "{proto_include}"'
                ),
                "{{INTERFACE_HEADER_BODY}}": render_full_interface_header_body(method),
                "{{INTERFACE_SOURCE_INCLUDES}}": (
                    f'#include "{project_name}/comm/business_exception.h"'
                ),
                "{{INTERFACE_SOURCE_BODY}}": render_full_interface_source_body(method),
            }
        )
        write_template(
            template_dir,
            "interface.h.template",
            project_dir / "interface" / f"{method.interface_file_stem}.h",
            method_replacements,
        )
        write_template(
            template_dir,
            "interface.cc.template",
            project_dir / "interface" / f"{method.interface_file_stem}.cc",
            method_replacements,
        )

    write_template(template_dir, "run.sh.template", out_dir / "run.sh", replacements)
    write_template(template_dir, "shutdown.sh.template", out_dir / "shutdown.sh", replacements)
    chmod_script(out_dir / "run.sh")
    chmod_script(out_dir / "shutdown.sh")


def main() -> int:
    args = parse_args()
    try:
        proto_path, service_name, out_dir, layout, project_name = validate_args(args)
        generator_dir = Path(__file__).resolve().parent
        template_dir = generator_dir / "template"

        probe_pb_dir = out_dir / ".generator_probe"
        probe_descriptor = probe_pb_dir / f"{proto_path.stem}{DESCRIPTOR_SUFFIX}"
        run_protoc(proto_path, probe_pb_dir, probe_descriptor)
        services = discover_services(proto_path, probe_descriptor)
        service = select_service(services, service_name)
        shutil.rmtree(probe_pb_dir, ignore_errors=True)

        if layout == "simple":
            generate_simple_project(template_dir, proto_path, service, out_dir)
        else:
            generate_full_project(template_dir, proto_path, service, out_dir, project_name)
    except Exception as exc:  # noqa: BLE001 - CLI should report any validation/copy failure clearly.
        print(f"[generator] FAIL: {exc}", file=sys.stderr)
        return 1

    print(f"[generator] generated {service.name} project at {out_dir} using {layout} layout")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
