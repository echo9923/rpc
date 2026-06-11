# Generated Project Example

This example verifies the generator can create simple and full layout projects that build, start a TinyPB server, run a generated client, and shut down. Stage 31 also verifies release package mode, where the generated project bundles the required MyTinyRPC source subset and builds without `MYTINYRPC_ROOT`.

## Run

```bash
cd /mnt/d/codeproject/cpp/rpc
./scripts/check_generator_project.sh
```

Expected final output:

```text
[generator] PASS
```

The script writes generated projects under `build/generated_task115_simple_project/` and `build/generated_task115_full_project/`.

To verify the release package path:

```bash
./scripts/check_generator_release_package.sh
```

Expected final output:

```text
[generator-release] PASS
```

## Manual Generator Command

```bash
python3 generator/tinyrpc_generator.py \
  --proto testcases/test_tinypb_server.proto \
  --service QueryService \
  --layout full \
  --out build/generated_manual_project

cmake -S build/generated_manual_project \
  -B build/generated_manual_project/build \
  -DMYTINYRPC_ROOT="$(pwd)"
cmake --build build/generated_manual_project/build
bash build/generated_manual_project/run.sh
./build/generated_manual_project/bin/QueryService_client --client 39999
bash build/generated_manual_project/shutdown.sh
```

Omit `--layout full` to keep the backward-compatible simple layout. The simple layout stores generated files in the output root and places binaries under `build/`.

## Release Package Command

```bash
python3 generator/tinyrpc_generator.py \
  --proto testcases/test_tinypb_server.proto \
  --service QueryService \
  --layout full \
  --package release \
  --out build/generated_release_project

cmake -S build/generated_release_project \
  -B build/generated_release_project/build
cmake --build build/generated_release_project/build
bash build/generated_release_project/run.sh
./build/generated_release_project/bin/QueryService_client --client 39999
bash build/generated_release_project/shutdown.sh
```

The release package writes `third_party/mytinyrpc/` and `third_party/MYTINYRPC_MANIFEST.md` into the generated output directory.

## Source Pointers

- Generator: `generator/tinyrpc_generator.py`
- Templates: `generator/template/`
- Generator checks: `scripts/check_generator.sh`
- Generated project check: `scripts/check_generator_project.sh`
- Release package check: `scripts/check_generator_release_package.sh`

## Full-Completion Path

This example is the completed stage 23 generator path plus the stage 31 release package path. The generated project flow covers simple/full layouts, `protoc` output, descriptor-set service/method metadata, package-aware generated code, method interface classes, service adapters, a generated test client, source/release package modes, and build/start/call/shutdown verification. It is included by `scripts/check_all.sh` and `scripts/check_full_completion.sh`.

## Boundary

The generator uses `protoc` to generate C++ Protobuf files and descriptor-set metadata, then generates method interface classes, a service adapter, a test client, and run/shutdown scripts. The generated client uses `TinyPbRpcChannel::Ptr`, a shared `IPAddress`, and Channel-level connection reuse so the generated code exercises the public stage 27 RPC Channel API. It currently supports unary RPC only; streaming RPC, proto2-specific behavior, multi-language generation, binary installers, and IDE project generation are outside this example.
