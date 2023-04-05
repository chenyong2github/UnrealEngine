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
#include "LearningAgentsPolicy.h"
#include "LearningNeuralNetworkObject.h"
#include "Misc/Paths.h"

ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer() : UActorComponent() {}
ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer(FVTableHelper& Helper) : ULearningAgentsImitationTrainer() {}
ULearningAgentsImitationTrainer::~ULearningAgentsImitationTrainer() {}

void ULearningAgentsImitationTrainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsTraining())
	{
		EndTraining();
	}

	Super::EndPlay(EndPlayReason);
}

void ULearningAgentsImitationTrainer::BeginTraining(
	ULearningAgentsPolicy* InPolicy, 
	const TArray<ULearningAgentsRecord*>& Records, 
	const FLearningAgentsImitationTrainerTrainingSettings& TrainingSettings,
	const bool bReinitializePolicyNetwork)
{
	if (IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("Already Training!"));
		return;
	}

	if (!InPolicy)
	{
		UE_LOG(LogLearning, Error, TEXT("BeginTraining called with nullptr for Policy."));
		return;
	}

	if (!InPolicy->IsPolicySetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Policy Setup not Performed"));
		return;
	}

	if (Records.IsEmpty())
	{
		UE_LOG(LogLearning, Error, TEXT("Records list is empty! Imitation Trainer needs at least one record to work correctly."));
		return;
	}

	Policy = InPolicy;

	// Get Number of Steps

	int32 StepNum = 0;
	for (const ULearningAgentsRecord* Record : Records)
	{
		if (!Record)
		{
			UE_LOG(LogLearning, Warning, TEXT("BeginTraining: Null record object."));
			continue;
		}

		// Assuming everyone has been trimmed already
		for (const TLearningArray<2, float>& Chunk : Record->GetObservations())
		{
			StepNum += Chunk.Num<0>();
		}
	}

	// Parse into Flat Arrays

	const int32 PolicyInputNum = Policy->GetPolicyNetwork().GetInputNum();
	const int32 PolicyOutputNum = Policy->GetPolicyNetwork().GetOutputNum();

	RecordedObservations.SetNumUninitialized({ StepNum, PolicyInputNum });
	RecordedActions.SetNumUninitialized({ StepNum, PolicyOutputNum / 2 });

	int32 DataIndex = 0;
	for (const ULearningAgentsRecord* Record : Records)
	{
		if (!Record) { continue; }

		TConstArrayView<TLearningArray<2, float>> RecordObsChunks = Record->GetObservations();
		TConstArrayView<TLearningArray<2, float>> RecordActionChunks = Record->GetActions();

		for (int32 Idx = 0; Idx < RecordObsChunks.Num(); Idx++)
		{
			if (RecordObsChunks[Idx].Num<1>() == PolicyInputNum &&
				RecordActionChunks[Idx].Num<1>() == PolicyOutputNum / 2)
			{
				int32 ChunkLength = RecordObsChunks[Idx].Num<0>();
				UE::Learning::Array::Copy(RecordedObservations.Slice(DataIndex, ChunkLength), RecordObsChunks[Idx]);
				UE::Learning::Array::Copy(RecordedActions.Slice(DataIndex, ChunkLength), RecordActionChunks[Idx]);
				DataIndex += ChunkLength;
			}
			else
			{
				UE_LOG(LogLearning, Warning, TEXT("Record input or output size does not match policy."));
			}
		}
	}

	// Return if no valid records found

	if (DataIndex == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("BeginTraining: input contains no valid training data."));
		RecordedObservations.Empty();
		RecordedActions.Empty();
		return;
	}

	// Resize to final size

	RecordedObservations.SetNumUninitialized({ DataIndex, PolicyInputNum });
	RecordedActions.SetNumUninitialized({ DataIndex, PolicyOutputNum / 2 });

	// Begin Training Properly

	UE_LOG(LogLearning, Display, TEXT("Imitation Training Started"));

	bIsTraining = true;
	bIsTrainingComplete = false;
	bRequestImitationTrainingStop = false;

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

	UE::Learning::FImitationTrainerTrainingSettings ImitationTrainingSettings;
	ImitationTrainingSettings.IterationNum = TrainingSettings.NumberOfIterations;
	ImitationTrainingSettings.LearningRateActor = TrainingSettings.LearningRate;
	ImitationTrainingSettings.LearningRateDecay = TrainingSettings.LearningRateDecay;
	ImitationTrainingSettings.WeightDecay = TrainingSettings.WeightDecay;
	ImitationTrainingSettings.BatchSize = TrainingSettings.BatchSize;
	ImitationTrainingSettings.Seed = TrainingSettings.RandomSeed;
	ImitationTrainingSettings.Device = TrainingSettings.Device == ELearningAgentsTrainerDevice::CPU ? UE::Learning::ETrainerDevice::CPU : UE::Learning::ETrainerDevice::GPU;
	ImitationTrainingSettings.bUseTensorboard = TrainingSettings.bUseTensorboard;

	const UE::Learning::EImitationTrainerFlags TrainerFlags = 
		bReinitializePolicyNetwork ? 
		UE::Learning::EImitationTrainerFlags::None : 
		UE::Learning::EImitationTrainerFlags::UseInitialPolicyNetwork;

	ImitationTrainer = MakeUnique<UE::Learning::FSharedMemoryImitationTrainer>(
		GetName(),
		PythonExecutablePath,
		SitePackagesPath,
		PythonContentPath,
		IntermediatePath,
		RecordedObservations.Num<0>(),
		RecordedObservations.Num<1>(),
		RecordedActions.Num<1>(),
		ImitationTrainingSettings);

	ImitationTrainingTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, TrainerFlags]()
	{
		UE::Learning::ImitationTrainer::Train(
			*ImitationTrainer,
			Policy->GetPolicyNetwork(),
			RecordedObservations,
			RecordedActions,
			TrainerFlags,
			&bRequestImitationTrainingStop,
			&NetworkLock);

			bIsTrainingComplete = true;
	});
}

void ULearningAgentsImitationTrainer::EndTraining()
{
	if (IsTraining())
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

bool ULearningAgentsImitationTrainer::IsTraining() const
{
	return bIsTraining;
}

bool ULearningAgentsImitationTrainer::IsTrainingComplete() const
{
	return bIsTrainingComplete;
}
