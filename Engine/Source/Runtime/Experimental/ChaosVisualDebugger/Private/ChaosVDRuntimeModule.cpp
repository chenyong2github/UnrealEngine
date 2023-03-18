// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/Public/ChaosVDRuntimeModule.h"

#include "ChaosVDRecording.h"
#include "Containers/Array.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FChaosVDRuntimeModule, ChaosVDRuntime);

FAutoConsoleCommand ChaosVDStartRecordingCommand(
	TEXT("p.Chaos.StartVDRecording"),
	TEXT("Turn on the recording of debugging data"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FChaosVDRuntimeModule::Get().StartRecording();
	})
);

FAutoConsoleCommand StopVDStartRecordingCommand(
	TEXT("p.Chaos.StopVDRecording"),
	TEXT("Turn off the recording of debugging data"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FChaosVDRuntimeModule::Get().StopRecording();
	})
);

FChaosVDRuntimeModule& FChaosVDRuntimeModule::Get()
{
	return FModuleManager::Get().LoadModuleChecked<FChaosVDRuntimeModule>(TEXT("ChaosVDRuntime"));
}

void FChaosVDRuntimeModule::StartupModule()
{
}

void FChaosVDRuntimeModule::ShutdownModule()
{
}

void FChaosVDRuntimeModule::StartRecording()
{
	// Note: The code in this method is pretty much place holder code for testing purposes

	if (bIsRecording)
	{
		return;
	}

	bIsRecording = true;

	CurrentRecording = MakeShared<FChaosVDRecording>();
}

void FChaosVDRuntimeModule::StopRecording()
{
	// Note: The code in this method is pretty much place holder code for testing purposes

	if (!ensure(bIsRecording))
	{
		return;
	}

	if (!ensure(CurrentRecording.IsValid()))
	{
		return;
	}

	const TCHAR* FilePrefix = TEXT("ChaosVD");
	const FString FullPathPrefix = FPaths::ProfilingDir() / FilePrefix;

	int32 Tries = 0;
	FString UseFileName;
	do
	{
		UseFileName = FString::Printf(TEXT("%s_%d.cvd"), *FullPathPrefix, Tries++);
	} while (IFileManager::Get().FileExists(*UseFileName));


	TArray<uint8> SerializedRecordedData;
	FMemoryWriter RecordedDataWriter(SerializedRecordedData, false);

	FChaosVDRecording::StaticStruct()->SerializeBin(RecordedDataWriter,CurrentRecording.Get());

	FFileHelper::SaveArrayToFile(SerializedRecordedData, *UseFileName);

	bIsRecording = false;
}
