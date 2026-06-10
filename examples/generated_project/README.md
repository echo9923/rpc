# Generated Project Example

This example verifies the generator can create simple and full layout projects that build, start a TinyPB server, run a generated client, and shut down.

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

## Source Pointers

- Generator: `generator/tinyrpc_generator.py`
- Templates: `generator/template/`
- Generator checks: `scripts/check_generator.sh`
- Generated project check: `scripts/check_generator_project.sh`

## Full-Completion Path

This example is the completed stage 23 generator path. The generated project flow covers simple/full layouts, `protoc` output, descriptor-set service/method metadata, package-aware generated code, method interface classes, service adapters, a generated test client, and build/start/call/shutdown verification. It is included by `scripts/check_all.sh` and `scripts/check_full_completion.sh`.

## Boundary

The generator uses `protoc` to generate C++ Protobuf files and descriptor-set metadata, then generates method interface classes, a service adapter, a test client, and run/shutdown scripts. It currently supports unary RPC only; streaming RPC, proto2-specific behavior, multi-language generation, and IDE project generation are outside this example.
