### README - How to update ONNX Runtime version

1. Bump version in `Classes/onnxruntime/onnxruntime_config.h`: #define ORT_VERSION "1.7.1".
	- Additional info: In the original ORT, `onnxruntime_config.h` is created by `{onnxruntime_path}/cmake/CMakeLists.txt` in line 1151.
		- `configure_file(onnxruntime_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime_config.h)`

2. Copy subset of `{onnxruntime_path}/onnxruntime/core` into `ONNXRuntime/Public/core/`.

3. Copy subset of `{onnxruntime_path}/include/onnxruntime/core` into `ONNXRuntime/Classes/onnxruntime/core/`.



Final architecture:
- `ONNXRuntime/`
	- `Classes/`
		- `onnxruntime/`
			- `core/`
				- `common/`
				- `framework/`
				- `graph/`
				- `optimizer/`
				- `platform/`
				- `providers/`
				- `session/`
			- `onnxruntime_config.h`
	- `Public/`
		- `core/`
			- `common/`
			- `contrib_ops/`
			- `custom_ops/`
			- `flatbuffers/`
			- `framework/`
			- `graph/`
			- `optimizer/`
			- `platform/`
				- `UE/`
			- `profile/`
			- `providers/` (removed non-used providers)
				- `cpu/`
				- `dml/`
				- `nni_cpu/`
				- `nni_hlsl/`
				- `shared/`
				- `shared_library/`
			- `session/`
			- `util/`
