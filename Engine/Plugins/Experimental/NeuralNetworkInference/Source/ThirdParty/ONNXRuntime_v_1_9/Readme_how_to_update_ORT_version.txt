# README

All changes in ORT have been labeled in the code with `WITH_UE` in some way (e.g., `#ifdef WITH_UE`, `#ifndef WITH_UE`, etc). Search for `WITH_UE` to (hopefully) find all the custom changes we made. When adding new changes to ORT, please, make sure to keep adding this `WITH_UE` flag to help locate changes in the future.

More ONNX Runtime compiling info in [onnxruntime.ai/docs/how-to/build/inferencing.html](https://www.onnxruntime.ai/docs/how-to/build/inferencing.html).



## Step 1: Create Local ONNX Runtime v1.7.1 Fork
1. Fork https://github.com/Microsoft/onnxruntime into your GitHub account, e.g., https://github.com/gineshidalgo99/onnxruntime
2. Checkout to the last version used by NNI (v1.7.1)

```
# Clone ONNX Runtime
cd D:\Users\gineshidalgo99\Desktop\ONNXRuntime
# cd D:\Users\gines.hidalgo\Desktop\ONNXRuntime
git clone --recursive https://github.com/gineshidalgo99/onnxruntime
# git clone --recursive https://github.com/Microsoft/onnxruntime
cd onnxruntime

# Checkout v1.7.1
git reset --hard 711a31e
# Alternatively push code to your fork so the online version is also on v1.7.1
git push origin master

# Alternatives:
# git clone https://github.com/gineshidalgo99/onnxruntime
# git submodule update --init --recursive
# git checkout 711a31e
# git checkout tags/v1.7.1
# git checkout master
```



## Step 2: Add NNI's Changes
1. Copy NNI's ONNXRuntime locally, e.g.,
	- From `D:/P4/ue5_main_pitt64/Engine/Plugins/Experimental/NeuralNetworkInference/Source/ThirdParty/ONNXRuntime/`.
	- Into `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/`.
2. To allow changes, right-click on `ONNXRuntime_src_code_from_NNI`, "Properties", uncheck "Read-only", and "OK".
3. Bump version in `[...]/ONNXRuntime_src_code_from_NNI/Classes/onnxruntime/onnxruntime_config.h`: #define ORT_VERSION "1.7.1".
	- Additional info: In the original ORT, `onnxruntime_config.h` is created by `{onnxruntime_path}/cmake/CMakeLists.txt` in line 1151.
		- `configure_file(onnxruntime_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime_config.h)`
4. Copy subset of `{onnxruntime_path}/include/onnxruntime/core` into `ONNXRuntime/Classes/onnxruntime/core/`.
5. Copy subset of `{onnxruntime_path}/onnxruntime/core` into `ONNXRuntime/Internal/core/`.



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


## Test and Debug ONNX Runtime on GitHub
# Me
.\build.bat --config Release --use_dml --build_shared_lib --parallel --cmake_generator "Visual Studio 16 2019" --build_wheel

# Paco's
.\build.bat --config Release --use_dml --build_shared_lib --parallel --skip_tests --cmake_generator "Visual Studio 16 2019" --build_wheel



## Step 3: How to update ONNX Runtime version
