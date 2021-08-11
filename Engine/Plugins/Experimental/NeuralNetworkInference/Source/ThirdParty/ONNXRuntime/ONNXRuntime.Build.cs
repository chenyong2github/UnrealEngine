// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ONNXRuntime : ModuleRules
{
	public ONNXRuntime( ReadOnlyTargetRules Target ) : base( Target )
	{
		// TODO #2: Also, revert temporary changes done where the text "TODO - UE Temporary Hack(it avoids the compiling error, but not sure if it works!)" appears

		ShortName = "ORT"; // Could be removed when plugin moves to Experimental, NFL path is too long
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ONNXRuntime includes
				System.IO.Path.Combine(ModuleDirectory, "./Classes"),
				System.IO.Path.Combine(ModuleDirectory, "./Classes/onnxruntime"),
				System.IO.Path.Combine(ModuleDirectory, "./Classes/onnxruntime/core/session"),
				System.IO.Path.Combine(ModuleDirectory, "./Internal/core"),
				// ThirdParty includes
				System.IO.Path.Combine(ModuleDirectory, "../Deps/eigen"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/date/include"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/gsl"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/json"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/mp11/include"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/optional-lite/include"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/SafeInt"),
				System.IO.Path.Combine(ModuleDirectory, "../Deps/wil/include"),
				// System.IO.Path.Combine(ModuleDirectory, "../../../../NeuralNetworkInferenceDeprecated/Source/ThirdParty/Deps/re2"),
			}
		);

		PublicDependencyModuleNames.AddRange
			(
			new string[] {
				"Core",
				"FlatBuffers",
				"ONNX",
				"ONNXRuntimeProto",
				"ONNXRuntimeMlas",
				"Protobuf",
				"Re2", // ONNXRuntimeRE2
				"ThirdPartyHelperAndDLLLoader"
			}
		);

		// Win64-only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDependencyModuleNames.AddRange
				(
				new string[] {
					"DirectML",
					"DX12"
				}
			);
		}
		// Linux
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDependencyModuleNames.AddRange
				(
				new string[] {
					"Nsync"
				}
			);
		}

		PrivateDependencyModuleNames.AddRange
			(
			new string[] {
			}
		);

		bUseRTTI = true;
		bEnableUndefinedIdentifierWarnings = false;
		bEnforceIWYU = false;

		// Editor-only
		if (!Target.bBuildEditor)
		{
			// Disable exceptions (needed by UE Game)
			PublicDefinitions.Add("ORT_NO_EXCEPTIONS");
			PublicDefinitions.Add("WIL_SUPPRESS_EXCEPTIONS");

			// Equivalent to modifying "Macros to throw exceptions on failure" from wil/result_macros.h, i.e.,
			// {NeuralNetworkInference}/Source/ThirdParty/Deps/wil/include/wil/result_macros.h starting around line 804 
			PublicDefinitions.Add("THROW_HR(hr)=(hr)");
			PublicDefinitions.Add("THROW_IF_FAILED(hr)=(hr)");
			PublicDefinitions.Add("THROW_HR_IF(hr, condition)=(condition)");
			PublicDefinitions.Add("THROW_LAST_ERROR_IF(condition)=(condition)");
			PublicDefinitions.Add("THROW_LAST_ERROR_IF_NULL(ptr)=ptr");

			// Equivalent to modifying ONNXRuntime CMake
			PublicDefinitions.Add("ORT_CATCH_RETURN=1;");
			PublicDefinitions.Add("ORT_CATCH_GENERIC=else if (false)");
		}
		else
		{
			// Equivalent to modifying ONNXRuntime CMake
			PublicDefinitions.Add("ORT_CATCH_RETURN=CATCH_RETURN();");
			PublicDefinitions.Add("ORT_CATCH_GENERIC=catch(...)");
		}

		PublicDefinitions.Add("WITH_UE");
		PublicDefinitions.Add("WIN32_LEAN_AND_MEAN");
		PublicDefinitions.Add("EIGEN_HAS_C99_MATH");
		PublicDefinitions.Add("NDEBUG");
		PublicDefinitions.Add("GSL_UNENFORCED_ON_CONTRACT_VIOLATION");
		PublicDefinitions.Add("EIGEN_USE_THREADS");
		PublicDefinitions.Add("ENABLE_ORT_FORMAT_LOAD");
		PublicDefinitions.Add("EIGEN_MPL2_ONLY");
		PublicDefinitions.Add("EIGEN_HAS_CONSTEXPR");
		PublicDefinitions.Add("EIGEN_HAS_VARIADIC_TEMPLATES");
		PublicDefinitions.Add("EIGEN_HAS_CXX11_MATH");
		PublicDefinitions.Add("EIGEN_HAS_CXX11_ATOMIC");
		PublicDefinitions.Add("EIGEN_STRONG_INLINE = inline");
		PublicDefinitions.Add("NOGDI");
		PublicDefinitions.Add("NOMINMAX");
		PublicDefinitions.Add("_USE_MATH_DEFINES");
		

		PublicDefinitions.Add("ONNX_NAMESPACE = onnx");
		PublicDefinitions.Add("ONNX_ML = 1");
		//PublicDefinitions.Add("ONNX_USE_LITE_PROTO = 1");
		PublicDefinitions.Add("__ONNX_NO_DOC_STRINGS");

		PublicDefinitions.Add("ORT_API_MANUAL_INIT");
		PublicDefinitions.Add("LOTUS_LOG_THRESHOLD = 2");
		PublicDefinitions.Add("LOTUS_ENABLE_STDERR_LOGGING");
		PublicDefinitions.Add("UNICODE");
		PublicDefinitions.Add("_UNICODE");
		PublicDefinitions.Add("_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING");

		// Win64-only
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("PLATFORM_WIN64");
			PublicDefinitions.Add("PLATFORM_WINDOWS");
			PublicDefinitions.Add("DML_TARGET_VERSION_USE_LATEST"); // @todo-for-Paco: Repeated
			PublicDefinitions.Add("DML_TARGET_VERSION_USE_LATEST = 1"); // @todo-for-Paco: Repeated
			PublicDefinitions.Add("USE_DML = 1");
		}
	}
}
