# README

All changes in ORT have been labeled in the code with `WITH_UE` in some way (e.g., `#ifdef WITH_UE`, `#ifndef WITH_UE`, etc). Search for `WITH_UE` to (hopefully) find all the custom changes we made. When adding new changes to ORT, please, make sure to keep adding this `WITH_UE` flag to help locate changes in the future.

More ONNX Runtime compiling info in [onnxruntime.ai/docs/how-to/build/inferencing.html](https://www.onnxruntime.ai/docs/how-to/build/inferencing.html).



## Step 0: Third Parties
- MLAS:
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime
mkdir ORT_MLAS; cd ORT_MLAS
git clone --recursive https://github.com/Microsoft/onnxruntime
cd onnxruntime
# Go to v1.9.1
git reset --hard 2a96b73a1afa9aaafb510749627e267c4e8dee63
.\build.bat --config Release --parallel --use_dml --use_full_protobuf

# Then copy into `Engine/Plugins/Experimental/NeuralNetworkInference/Source/ThirdParty/Deps/MLAS_1_9_1/`:
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ORT_MLAS/onnxruntime/onnxruntime/core/mlas/
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ORT_MLAS/onnxruntime/build/Windows/Release/Release/onnxruntime_mlas.lib
```
- ONNX: Compile desired ORT version to see ONNX flags + https://github.ol.epicgames.net/francisco-vicente/onnx_nni



## Step 1: Create Local ONNX Runtime v1.7.1 Fork
1. Fork https://github.com/Microsoft/onnxruntime into your GitHub account, e.g., https://github.com/gineshidalgo99/onnxruntime
2. Checkout to the last version used by NNI (v1.7.1)

```
# Clone ONNX Runtime
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/
mkdir MLAS/
# cd D:/Users/gines.hidalgo/Desktop/ONNXRuntime
git clone --recursive https://github.com/gineshidalgo99/onnxruntime
# git clone --recursive https://github.com/Microsoft/onnxruntime
cd onnxruntime

# Checkout v1.7.1
git reset --hard 711a31e
# Alternatively push code to your fork so the online version is also on v1.7.1
git push origin master

# Checkout right before v1.9.0
git reset --hard 6fbd0a823350615c613662f60cb27f18b4cbab24

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
3. Bump version in `[...]/ONNXRuntime_src_code_from_NNI/Internal/onnxruntime_config.h`: #define ORT_VERSION "1.7.1".
	- Additional info: In the original ORT, `onnxruntime_config.h` is created by `{onnxruntime_path}/cmake/CMakeLists.txt` in line 1151.
		- `configure_file(onnxruntime_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/onnxruntime_config.h)`
4. `ONNXRuntime/Internal/`:
	- Copy subset of `{onnxruntime_path}/include/onnxruntime/core/` into `ONNXRuntime/Internal/core/`.
5. `ONNXRuntime/Private/`:
	- Copy subset of `{onnxruntime_path}/onnxruntime/contrib_ops/cpu/` into `ONNXRuntime/Private/contrib_ops/cpu/`.
	- Copy subset of `{onnxruntime_path}/onnxruntime/core/` into `ONNXRuntime/Private/core/`.
	- Copy subset of `{onnxruntime_path}/onnxruntime/test/testdata/custom_op_library/` into `ONNXRuntime/Private/test/testdata/custom_op_library/`.

With commands:
```
################################################## REMOVING ##################################################
# Remove include/onnxruntime
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/include/onnxruntime/core
# Remove contrib_ops/cpu
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/contrib_ops/cpu/
# Remove core
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/common
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/flatbuffers/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/framework/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/graph/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/optimizer/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/platform/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/profile/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/cpu/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/dml/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared_library/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/common.h
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/get_execution_providers.*
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/op_kernel_type_control*
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/quantization/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/session/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/util/
# Remove custom_op_library
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.cc
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.h

################################################## ADDING NEW FILES ##################################################
# Alternatives:
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/
# Copy include/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Internal/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/include/onnxruntime/core
# Copy contrib_ops/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/contrib_ops/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/contrib_ops
# Copy core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private_DML_EP/Windows/core/ D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime
# Copy custom_op_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/test D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime

# Equivalent to manually dragging:
# - The 3 folders of {ONNXRuntime_from_NNI}/ONNXRuntime/Private into {ONNXRuntime_repo}/onnxruntime/onnxruntime.
# - {ONNXRuntime_from_NNI}/Private_DML_EP/Windows/core into {ONNXRuntime_repo}/onnxruntime/onnxruntime.
# - {ONNXRuntime_from_NNI}/Internal/core into {ONNXRuntime_repo}/include/onnxruntime/core.

################################################## REVERTING ACCIDENTAL DELETES ##################################################
git checkout onnxruntime/core/platform/android/*
git checkout onnxruntime/core/platform/posix/env*
git checkout onnxruntime/core/platform/posix/logging/*
git checkout onnxruntime/core/platform/windows/*
```

See how many files you have updated:
```
# Using git add will let you see renames more easily, just don't commit/push it!
git add .
git status
# Undo git add
git reset *
```

```
# Note: To remove untracked files
# git checkout *
# git clean -f -d   # https://koukia.ca/how-to-remove-local-untracked-files-from-the-current-git-branch-571c6ce9b6b1
```



## Step 3: Upgrade ONNX Runtime
```
# git reset --hard 711a31e
# git reset --hard 6fbd0a823350615c613662f60cb27f18b4cbab24
# git push -f

# Push code
git add .
git commit -m "NNI"
git push

# When doing `git pull https://github.com/microsoft/onnxruntime/ [SOME_FILE]`, error about untracked files, just run something like this with whatever files you get an error message about:
rm onnxruntime/python/tools/tensorrt/perf/build/Dockerfile.tensorrt-perf
rm onnxruntime/python/tools/tensorrt/perf/build/build_images.sh
rm onnxruntime/test/testdata/foo_bar_1.onnx
rm onnxruntime/test/testdata/foo_bar_2.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format3_no_cast_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format3_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format5_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format6_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format7_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format8_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_format9_opset13.onnx
rm onnxruntime/test/testdata/transform/fusion/embed_layer_norm_multiple_opset13.onnx


# Commits on Mar 4, 2021
# https://github.com/microsoft/onnxruntime/commits/master?before=e2b1852eecc82b92daeae27ec0692d4197b1bd73+1279&branch=master
git pull https://github.com/microsoft/onnxruntime/ fa8d1b44b832ffafddee0c38ac1fe1d09d8344ee

# Commits on Mar 23, 2021
# https://github.com/microsoft/onnxruntime/commits/master?before=e2b1852eecc82b92daeae27ec0692d4197b1bd73+1174&branch=master
git pull https://github.com/microsoft/onnxruntime/ b07e168a2b358e10423a29501bf49634281e6139

# Commits on Apr 23, 2021
# https://github.com/microsoft/onnxruntime/commits/master?before=e2b1852eecc82b92daeae27ec0692d4197b1bd73+1004&branch=master
git pull https://github.com/microsoft/onnxruntime/ f1c3f3fcc1f7b0ef2be9291ebe1c5e3f8389bf7e

# Commits on May 26, 2021
# https://github.com/microsoft/onnxruntime/commits/master?before=e2b1852eecc82b92daeae27ec0692d4197b1bd73+699&branch=master
git pull https://github.com/microsoft/onnxruntime/ fa093d8e45c5686f438c93146ab31840dd31f779

# v1.9.0 (Commits on Sep 8, 2021)
# https://github.com/microsoft/onnxruntime/commits/master?after=e2b1852eecc82b92daeae27ec0692d4197b1bd73+99&branch=master
git pull https://github.com/microsoft/onnxruntime/ 6fbd0a823350615c613662f60cb27f18b4cbab24

git add .
git commit -m "Mar 4th pulled"
```



## Step 4: Compare ONNX Runtime
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime
mkdir ORTMoreUpdated/
cd ORTMoreUpdated
git clone --recursive https://github.com/gineshidalgo99/onnxruntime
cd onnxruntime
git reset --hard 6fbd0a823350615c613662f60cb27f18b4cbab24

# Manually copy the whole repository from `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime` into it
# Hopefully, `git status` should give ONLY the WITH_UE changes. Revert other changes

git status
```

## Step 5: Push New Code into NNI
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/
mkdir 0_NewCodeToPushToNNI/
cd 0_NewCodeToPushToNNI/

# Copy include/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/include/onnxruntime/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Internal/core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/Internal/onnxruntime_config.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Internal/
# Copy ORTModule.h/cpp
mkdir D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/Private/ONNXRuntimeModule.* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/
# Copy contrib_ops/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/contrib_ops/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/contrib_ops/cpu
# Copy core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/common D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/common
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/flatbuffers D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/flatbuffers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/framework D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/framework
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/graph D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/graph
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/optimizer D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/optimizer
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/platform D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/platform/android
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/platform/posix/env*
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/platform/posix/logging
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/platform/windows
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/profile D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/profile
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/dml D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private_DML_EP/Windows/core/providers/dml
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers/shared
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared_library D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers/shared_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/common.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers/common.h
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/get_execution_providers.* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/op_kernel_type_control* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/providers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/quantization D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/quantization
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/session D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/session
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/util D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/core/util
# Copy custom_op_library
mkdir D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/test/testdata/custom_op_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.cc D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/test/testdata/custom_op_library/custom_op_library.cc
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_src_code_from_NNI/0_NewCodeToPushToNNI/Private/test/testdata/custom_op_library/custom_op_library.h
```

```
# Revert changes and re-copy changes to double check we did not mess anything here
git reset --hard 6fbd0a823350615c613662f60cb27f18b4cbab24
# Copy new changes
```

This new code can be pushed into NNI and heavily tested in there. Once fully working on NNI, repeat steps 2-5 until upgraded to the desired CL.
NOTE: The first one (from 1.7.1 to master) will be trickier, as there will be merge conflicts between 1.7.1 and master unrelated to the changes made for NNI. But after being in master, it's way easier to upgrade the code.



## Final Architecture of ONNXRuntime in NNI/Source/ThirdParty/:
- `ONNXRuntime/`
	- `Internal/`
		- `core/`
			- `common/`
			- `framework/`
			- `graph/`
			- `optimizer/`
			- `platform/`
			- `providers/`
			- `session/`
		- `onnxruntime_config.h`
	- `Private/`
		- `contrib_ops/cpu/`
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
			- `providers/` (removed non-used providers, dml one moved into `Private_DML_EP/`)
				- `cpu/`
				- `nni_cpu/` (eventually)
				- `nni_hlsl/` (eventually)
				- `shared/`
				- `shared_library/`
			- `quantization/`
			- `session/`
			- `util/`
		- `test/testdata/custom_op_library/`
	- `Private_DML_EP/`
		- `Windows/`
			- `core/`
				- `providers/`
					- `dml/`


## Test and Debug ONNX Runtime on GitHub
# Paco's 1.9.1
.\build.bat --config Release --parallel --use_dml --use_full_protobuf

# Me 1.7.1
.\build.bat --config Release --use_dml --build_shared_lib --parallel --cmake_generator "Visual Studio 16 2019" --build_wheel

# Paco's 1.7.1
.\build.bat --config Release --use_dml --build_shared_lib --parallel --skip_tests --cmake_generator "Visual Studio 16 2019" --build_wheel
