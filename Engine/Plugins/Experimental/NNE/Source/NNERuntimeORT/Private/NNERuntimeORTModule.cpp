// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORTModule.h"
#include "NNECore.h"
#include "NNERuntimeORT.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "UObject/WeakInterfacePtr.h"

#include "NNEThirdPartyWarningDisabler.h"
NNE_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/onnxruntime_cxx_api.h"
NNE_THIRD_PARTY_INCLUDES_END

void FNNERuntimeORTModule::StartupModule()
{
	TArray<FString> DLLFileNames
	{
		"onnxruntime",
	};

	const FString ORTDefaultRuntimeBinPath = TEXT(PREPROCESSOR_TO_STRING(ORTDEFAULT_PLATFORM_BIN_PATH));

	FPlatformProcess::PushDllDirectory(*ORTDefaultRuntimeBinPath);

	TArray<void*> DLLHandles;

	for (FString DLLFileName : DLLFileNames)
	{
#if PLATFORM_WINDOWS
		const FString DLLFilePath = ORTDefaultRuntimeBinPath / DLLFileName + ".dll";
#elif PLATFORM_LINUX
		const FString DLLFilePath = ORTDefaultRuntimeBinPath / "lib" + DLLFileName + ".so";
#elif PLATFORM_MAC
		const FString DLLFilePath = ORTDefaultRuntimeBinPath / "lib" + DLLFileName + ".dylib";
#endif

		// Sanity check
		if (!FPaths::FileExists(DLLFilePath))
		{
			const FString ErrorMessage = FString::Format(TEXT("DLL file not found in \"{0}\"."),
				{ IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*DLLFilePath) });
			//UE_LOG(LogNNE, Error, TEXT("ORT StartupModule(): %s"), *ErrorMessage);
			//checkf(false, TEXT("%s"), *ErrorMessage);
		}
		DLLHandles.Add(FPlatformProcess::GetDllHandle(*DLLFilePath));

	}
	FPlatformProcess::PopDllDirectory(*ORTDefaultRuntimeBinPath);

	Ort::InitApi();
	
#if PLATFORM_WINDOWS
	// NNE runtime ORT Dml startup
	NNERuntimeORTDml = NewObject<UNNERuntimeORTDmlImpl>();
	if (NNERuntimeORTDml.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeDmlInterface(NNERuntimeORTDml.Get());

		NNERuntimeORTDml->Init();
		NNERuntimeORTDml->AddToRoot();
		UE::NNECore::RegisterRuntime(RuntimeDmlInterface);
	}
#endif
}

void FNNERuntimeORTModule::ShutdownModule()
{
#if PLATFORM_WINDOWS
	// NNE runtime ORT Dml shutdown
	if (NNERuntimeORTDml.IsValid())
	{
		TWeakInterfacePtr<INNERuntime> RuntimeDmlInterface(NNERuntimeORTDml.Get());

		UE::NNECore::UnregisterRuntime(RuntimeDmlInterface);
		NNERuntimeORTDml->RemoveFromRoot();
		NNERuntimeORTDml = TWeakObjectPtr<UNNERuntimeORTDmlImpl>(nullptr);
	}
#endif

}

IMPLEMENT_MODULE(FNNERuntimeORTModule, NNERuntimeORT);