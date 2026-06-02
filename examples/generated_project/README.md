# Generated Project Example

This example verifies the generator can create a project that builds, starts a TinyPB server, runs a generated client, and shuts down.

## Run

```bash
cd /mnt/d/codeproject/cpp/rpc
./scripts/check_generator_project.sh
```

Expected final output:

```text
[generator] PASS
```

The generated project is written under `build/generated_task81_project/`.

## Manual Generator Command

```bash
python3 generator/tinyrpc_generator.py \
  --proto testcases/test_tinypb_server.proto \
  --service QueryService \
  --out build/generated_manual_project

cmake -S build/generated_manual_project \
  -B build/generated_manual_project/build \
  -DMYTINYRPC_ROOT="$(pwd)"
cmake --build build/generated_manual_project/build
bash build/generated_manual_project/run.sh
./build/generated_manual_project/build/QueryService_client --client 39999
bash build/generated_manual_project/shutdown.sh
```

## Source Pointers

- Generator: `generator/tinyrpc_generator.py`
- Templates: `generator/template/`
- Generator checks: `scripts/check_generator.sh`
- Generated project check: `scripts/check_generator_project.sh`

## Boundary

The generator intentionally supports a small proto subset: a service block with unary rpc methods in the same file. It does not implement a complete Protobuf parser or IDE project generation.
