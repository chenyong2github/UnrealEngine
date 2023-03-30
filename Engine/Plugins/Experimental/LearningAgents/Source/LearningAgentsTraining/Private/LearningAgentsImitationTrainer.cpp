// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsImitationTrainer.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "LearningAgentsType.h"
#include "LearningArrayMap.h"
#include "LearningExperience.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningImitationTrainer.h"
#include "LearningAgentsDataStorage.h"
#include "LearningPolicyObject.h"
#include "Misc/Paths.h"

ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer() : ULearningAgentsTypeComponent() {}
ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer(FVTableHelper& Helper) : ULearningAgentsImitationTrainer() {}
ULearningAgentsImitationTrainer::~ULearningAgentsImitationTrainer() {}

void ULearningAgentsImitationTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bIsTraining)
	{
		EndTraining();
	}
}

const bool ULearningAgentsImitationTrainer::IsTraining() const
{
	return bIsTraining;
}

void ULearningAgentsImitationTrainer::BeginTraining(TArray<UObject*> Records)
{
	if (bIsTraining)
	{
		UE_LOG(LogLearning, Warning, TEXT("Already Training!"));
		return;
	}

	if (Records.IsEmpty())
	{
		UE_LOG(LogLearning, Error, TEXT("Records list is empty! Imitation Trainer needs at least one record to work correctly."));
		return;
	}

	UE_LOG(LogLearning, Display, TEXT("Imitation Training Started"));

	bIsTraining = true;
	bIsTrainingComplete = false;
	bRequestImitationTrainingStop = false;

	// Start Python Training Process (this must be done on main thread)

#if WITH_EDITOR
	const FString PythonExecutablePath = UE::Learning::Trainer::DefaultEditorPythonExecutablePath();
	const FString SitePackagesPath = UE::Learning::Trainer::DefaultEditorSitePackagesPath();
	const FString PythonContentPath = UE::Learning::Trainer::DefaultEditorPythonContentPath();
	const FString IntermediatePath = UE::Learning::Trainer::DefaultEditorIntermediatePath();
#else
	UE_LEARNING_CHECKF(PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX, TEXT("Python only supported on Windows, Mac, and Linux."));
	const FString PythonExe = PLATFORM_WINDOWS ? TEXT("python.exe") : TEXT("bin/python");
	const FString EnginePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*(FPaths::RootDir() / TEXT("../../../../../../Engine")));
	const FString PythonExecutablePath = EnginePath / TEXT("Binaries/ThirdParty/Python3") / FPlatformMisc::GetUBTPlatform() / PythonExe;
	const FString SitePackagesPath = EnginePath / TEXT("Plugins/Experimental/PythonFoundationPackages/Content/Python/Lib/") / FPlatformMisc::GetUBTPlatform() / TEXT("site-packages");
	const FString PythonContentPath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Content/Python/");
	const FString IntermediatePath = EnginePath / TEXT("Plugins/Experimental/LearningAgents/Intermediate");
#endif

	UE::Learning::FImitationTrainerSettings Settings;
	Settings.bReinitializeNetwork = true;
	Settings.Device = UE::Learning::ETrainerDevice::CPU;
	Settings.bUseTensorboard = false;

	// Get length
	int32 StepNum = 0;
	for (UObject* Object : Records)
	{
		ULearningAgentsRecord* Record = Cast<ULearningAgentsRecord>(Object);
		if (!Record)
		{
			UE_LOG(LogLearning, Warning, TEXT("BeginTraining: input contains object (%s) that can't be cast to ULearningAgentsRecord."), *(Object ? Object->GetName() : FString(TEXT("nullptr"))));
			continue;
		}

		TConstArrayView<TLearningArray<2, float>> RecordObsChunks = Record->GetObservations();

		// Assuming everyone has been trimmed already
		for (TLearningArray<2, float> Chunk : RecordObsChunks)
		{
			StepNum += Chunk.Num<0>();
		}
	}

	if (StepNum == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("BeginTraining: input contains no valid training data. Returning early."));
		return;
	}

	RecordedObservations.SetNumUninitialized({ StepNum, AgentType->GetObservationFeature().DimNum() });
	RecordedActions.SetNumUninitialized({ StepNum, AgentType->GetActionFeature().DimNum() });

	int32 DataIndex = 0;
	for (UObject* Object : Records)
	{
		ULearningAgentsRecord* Record = Cast<ULearningAgentsRecord>(Object);
		if (!Record)
		{
			UE_LOG(LogLearning, Warning, TEXT("BeginTraining: input contains object (%s) that can't be cast to ULearningAgentsRecord."), *(Object ? Object->GetName() : FString(TEXT("nullptr"))));
			continue;
		}

		TConstArrayView<TLearningArray<2, float>> RecordObsChunks = Record->GetObservations();
		TConstArrayView<TLearningArray<2, float>> RecordActionChunks = Record->GetActions();

		// Assuming everyone has been trimmed already
		for (int32 i = 0; i < RecordObsChunks.Num(); i++)
		{
			int32 ChunkLength = RecordObsChunks[i].Num<0>();
			UE::Learning::Array::Copy(RecordedObservations.Slice(DataIndex, ChunkLength), RecordObsChunks[i]);
			UE::Learning::Array::Copy(RecordedActions.Slice(DataIndex, ChunkLength), RecordActionChunks[i]);
			DataIndex += ChunkLength;
		}
	}

	ImitationTrainer = MakeUnique<UE::Learning::FSharedMemoryImitationTrainer>(
		TEXT("ImitationLearner"),
		PythonExecutablePath,
		SitePackagesPath,
		PythonContentPath,
		IntermediatePath,
		AgentType->GetNeuralNetwork(),
		AgentType->GetPolicy().Settings.ActionNoiseMin,
		AgentType->GetPolicy().Settings.ActionNoiseMax,
		RecordedObservations.Num<0>(),
		Settings);

	ImitationTrainingTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [&]()
		{
			UE::Learning::ImitationTrainer::Train(
				*ImitationTrainer,
				AgentType->GetNeuralNetwork(),
				RecordedObservations,
				RecordedActions,
				true,
				&bRequestImitationTrainingStop,
				&NetworkLock);

			bIsTrainingComplete = true;
		});
}

void ULearningAgentsImitationTrainer::EndTraining()
{
	if (bIsTraining)
	{
		UE_LOG(LogLearning, Display, TEXT("Imitation Training Ended"));

		bRequestImitationTrainingStop = true;
		ImitationTrainingTask.Wait(FTimespan(0, 0, 30));

		bIsTraining = false;
	}
	else
	{
		UE_LOG(LogLearning, Warning, TEXT("Not Training!"));
	}
}

const bool ULearningAgentsImitationTrainer::IsTrainingComplete() const
{
	return bIsTrainingComplete;
}
