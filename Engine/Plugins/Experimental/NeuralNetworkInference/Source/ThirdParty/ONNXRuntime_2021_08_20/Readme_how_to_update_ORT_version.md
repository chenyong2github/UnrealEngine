# README

All changes in ORT have been labeled in the code with `WITH_UE` in some way (e.g., `#ifdef WITH_UE`, `#ifndef WITH_UE`, etc). Search for `WITH_UE` to (hopefully) find all the custom changes we made. When adding new changes to ORT, please, make sure to keep adding this `WITH_UE` flag to help locate changes in the future.

NOTE: Updating from master commit X to master commit Y is relatively "easy" and is detailed below. However, updating from a specific version or branch of ONNX Runtime into its master or another branch will be trickier because the changes from that branch will have to be reverted first. For this case, I highly recommend the following steps:
1. Downgrade from that version (e.g. 1.7.1) into the last common commit between that version and master (e.g. the last commit before v1.7.1 was created).
2. Fix merge conflicts (caused by ONNX Runtime, not by NNI).
3. Update from this master commit to the desired master commit by following the document below.

More ONNX Runtime compiling info in [onnxruntime.ai/docs/how-to/build/inferencing.html](https://www.onnxruntime.ai/docs/how-to/build/inferencing.html).

For questions, ask Gines.Hidalgo.



## Step 0: Compiling Third Parties
Francisco.Vicente understands better this step, in case of questions, ping him.
- MLAS (manually remove the old ORT_MLAS folder first):
```
# D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ORT_MLAS/onnxruntime

cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime
mkdir ORT_MLAS; cd ORT_MLAS
git clone --recursive https://github.com/Microsoft/onnxruntime
cd onnxruntime
# Checkout ORT master. E.g., on Oct 18th
git reset --hard 4028e51e7e6421fdbeca5f4e4ccd8b4f790d0fd5 # This is the desired commit of ORT that will live inside of NNI
.\build.bat --config Release --parallel --use_dml --use_full_protobuf

# Then copy into `Engine/Plugins/Experimental/NeuralNetworkInference/Source/ThirdParty/Deps/MLAS_1_9_1/`:
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ORT_MLAS/onnxruntime/onnxruntime/core/mlas/
# - D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ORT_MLAS/onnxruntime/build/Windows/Release/Release/onnxruntime_mlas.lib
```
- ONNX: Compile desired ORT version to see ONNX flags + https://github.ol.epicgames.net/francisco-vicente/onnx_nni



## Step 1: Prerequisites
1. Create and open the following folders on your explorer:
	- `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI`
	- `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime`
	- `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI`
2. Fork https://github.com/Microsoft/onnxruntime into your GitHub account, e.g., https://github.com/gineshidalgo99/onnxruntime



## Step 2: Create Local ONNX Runtime Fork
(First time only, not needed if you already have your fork of ORT locally) Clone your fork of ORT locally:
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/   # cd D:/Users/gines.hidalgo/Desktop/ONNXRuntime
# Recursive not needed if only planning to merge (but not compile)
# git clone --recursive https://github.com/gineshidalgo99/onnxruntime # git clone --recursive https://github.com/Microsoft/onnxruntime
git clone https://github.com/gineshidalgo99/onnxruntime
cd onnxruntime
```



## Step 3: Add NNI's Changes Locally
Idea (what the commands below will automatically do):
1. Copy NNI's ONNXRuntime locally:
	- From `D:/P4/ue5_main_pitt64/Engine/Plugins/Experimental/NeuralNetworkInference/Source/ThirdParty/ONNXRuntime/`.
	- Into `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/`.
2. To allow changes, right-click on `ONNXRuntime_code_from_NNI`, "Properties", uncheck "Read-only", and "OK".
3. Bump version in `[...]/ONNXRuntime_code_from_NNI/Internal/onnxruntime_config.h` accordingly: `#define ORT_VERSION "1.9.0"` (otherwise minor compiler error on NNI).
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
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime

git pull https://github.com/microsoft/onnxruntime/ master
git reset *; git checkout *; git clean -f -d; git reset --hard 9fc53df33a3dd7ad20e061b610e18ea8c5b795a4 # This is the commit where the current version of ORT inside of NNI is
# git push -f # Optional

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
# Copy include/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Internal/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/include/onnxruntime/core
# Copy contrib_ops/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Private/contrib_ops/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/contrib_ops
# Copy core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Private/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Private_DML/Windows/core/ D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime
# Copy custom_op_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Private/test D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime

################################################## REVERTING ACCIDENTAL DELETES ##################################################
git checkout onnxruntime/core/platform/android/*
git checkout onnxruntime/core/platform/posix/env*
git checkout onnxruntime/core/platform/posix/logging/*
git checkout onnxruntime/core/platform/windows/*

git add .
git status
```

See how many files you have updated and make sure ALL changes reported by `git diff` come from lines saying `WITH_UE` (otherwise you might have messed up and merged the wrong version of ORT, believe me, I've done that mistake before, and it's a nightmare, so CHECK IT!):
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime
# Using git add will let you see renames more easily, just don't commit/push it!
git add .
git status
git diff --cached
# Undo git add
git reset *
```

(OPTIONAL) Reset the local changes
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime
# Note: To remove untracked files
git reset *
git checkout *
git clean -f -d   # https://koukia.ca/how-to-remove-local-untracked-files-from-the-current-git-branch-571c6ce9b6b1
```



## Step 4: Merge ORT Master with NNI's changes
```
# Push code
git add .
git commit -m "NNI"

# Checkout desired version to merged with (e.g., ORT master on Oct 18th)
git pull https://github.com/microsoft/onnxruntime/ 4028e51e7e6421fdbeca5f4e4ccd8b4f790d0fd5 # This is the desired commit of ORT that will live inside of NNI
# NOTE: Manually fix conflicts locally (if any). E.g., if error(s) about untracked files, just run something like this with whatever files you get an error message about:
# rm onnxruntime/python/tools/tensorrt/perf/build/Dockerfile.tensorrt-perf
```

(OPTIONAL and not needed at all) If no conflicts or after solving them, code ready to be moved to NNI. You can optionally commmit/push it:
```
git add .
git commit -m "Master from MONTH DAY-th merged"
git push
```



## Step 5: Push New Code into NNI
```
cd D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/

rm D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/* # It will ask for confirmation, press "A" (Yes to All)

# Copy include/onnxruntime
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/include/onnxruntime/core D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Internal/core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Internal/onnxruntime_config.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Internal/
# Copy ORTModule.h/cpp
mkdir D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Private/ONNXRuntimeModule.* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/
# Copy contrib_ops/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/contrib_ops/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/contrib_ops/cpu
# Copy core
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/common D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/common
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/flatbuffers D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/flatbuffers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/framework D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/framework
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/graph D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/graph
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/optimizer D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/optimizer
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/platform D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/platform/android
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/platform/posix/env*
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/platform/posix/logging
rm -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/platform/windows
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/profile D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/profile
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/cpu D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers/cpu
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/dml D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private_DML/Windows/core/providers/dml
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers/shared
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/shared_library D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers/shared_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/common.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers/common.h
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/get_execution_providers.* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/providers/op_kernel_type_control* D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/providers
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/quantization D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/quantization
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/session D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/session
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/core/util D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/core/util
# Copy custom_op_library
mkdir D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/test/testdata/custom_op_library
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.cc D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/test/testdata/custom_op_library/custom_op_library.cc
cp -r -fo D:/Users/gineshidalgo99/Desktop/ONNXRuntime/onnxruntime/onnxruntime/test/testdata/custom_op_library/custom_op_library.h D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Private/test/testdata/custom_op_library/custom_op_library.h

# Individual files
cp D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/ONNXRuntime.Build.cs D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/ONNXRuntime.Build.cs
cp D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/ONNXRuntime.tps D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/ONNXRuntime.tps
cp D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_from_NNI/Readme_how_to_update_ORT_version.md D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI/Readme_how_to_update_ORT_version.md

rm D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ort_compressed.zip
Compress-Archive -LiteralPath D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ONNXRuntime_code_to_push_to_NNI -DestinationPath D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ort_compressed.zip
```

This new code zipped as `D:/Users/gineshidalgo99/Desktop/ONNXRuntime/ort_compressed.zip` can be copied into NNI and tested in there. To test it properly, do the following tests (in this order):
1. Compile UE and run QA tests to make sure they are successful.
2. Package game for Windows and run tests on the Windows game to make sure they are successful. Trick: If your game is saved on `D:/Users/gines.hidalgo/Desktop/GameTest/`, the logging of the application will be saved on `D:/Users/gines.hidalgo/Desktop/GameTest/Windows/NNIExample/Saved/Logs/NNIExample.log`.
3. Package game for Ubuntu from Windows to make sure it compiles on Ubuntu.
4. Do extensive pre-flights for all targeted platforms.

Once fully working on NNI, pushed code on P4 and you are done!



## Optional Step: Making Pull Request to ORT
```
git checkout -b SOME_BRANCH_NAME # Eg ClangOnWindowsCompiling, AlwaysReturn
git push origin SOME_BRANCH_NAME
# Equivalent to:
# git branch SOME_BRANCH_NAME
# git checkout SOME_BRANCH_NAME
```



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
			- `providers/` (removed non-used providers, dml one moved into `Private_DML/`)
				- `cpu/`
				- `nni_cpu/` (eventually)
				- `nni_hlsl/` (eventually)
				- `shared/`
				- `shared_library/`
			- `quantization/`
			- `session/`
			- `util/`
		- `test/testdata/custom_op_library/`
	- `Private_DML/`
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
