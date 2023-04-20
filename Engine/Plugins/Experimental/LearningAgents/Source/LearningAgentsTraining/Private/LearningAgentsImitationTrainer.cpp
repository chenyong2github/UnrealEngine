// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsImitationTrainer.h"

#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/FileManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningArrayMap.h"
#include "LearningExperience.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningImitationTrainer.h"
#include "LearningAgentsRecording.h"
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
	const ULearningAgentsRecording* Recording,
	const FLearningAgentsImitationTrainerTrainingSettings& ImitationTrainerTrainingSettings,
	const FLearningAgentsTrainerPathSettings& ImitationTrainerPathSettings,
	const bool bReinitializePolicyNetwork)
{
	if (IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Cannot begin training as we are already training!"), *GetName());
		return;
	}

	if (!InPolicy)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InPolicy is nullptr."), *GetName());
		return;
	}

	if (!InPolicy->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InPolicy->GetName());
		return;
	}

	if (!Recording)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Recording is nullptr."), *GetName());
		return;
	}

	if (Recording->Records.IsEmpty())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Recording is empty!"), *GetName());
		return;
	}

	Policy = InPolicy;

	// Check Paths

	const FString PythonExecutablePath = UE::Learning::Trainer::GetPythonExecutablePath(ImitationTrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::FileExists(PythonExecutablePath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find Python executable \"%s\"."), *GetName(), *PythonExecutablePath);
		return;
	}
	const FString PythonContentPath = UE::Learning::Trainer::GetPythonContentPath(ImitationTrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::DirectoryExists(PythonContentPath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find LearningAgents plugin Content \"%s\"."), *GetName(), *PythonContentPath);
		return;
	}

	const FString SitePackagesPath = UE::Learning::Trainer::GetSitePackagesPath(ImitationTrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::DirectoryExists(SitePackagesPath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find Python site-packages \"%s\"."), *GetName(), *SitePackagesPath);
		return;
	}

	const FString IntermediatePath = UE::Learning::Trainer::GetIntermediatePath(ImitationTrainerPathSettings.GetIntermediatePath());

	// Sizes

	const int32 PolicyInputNum = Policy->GetPolicyNetwork().GetInputNum();
	const int32 PolicyOutputNum = Policy->GetPolicyNetwork().GetOutputNum();

	// Get Number of Steps

	int32 TotalSampleNum = 0;
	for (const FLearningAgentsRecord& Record : Recording->Records)
	{
		if (Record.ObservationDimNum != PolicyInputNum)
		{
			UE_LOG(LogLearning, Warning, TEXT("%s: Record has wrong dimensionality for observations, got %i, policy expected %i."), *GetName(), Record.ObservationDimNum, PolicyInputNum);
			continue;
		}

		if (Record.ActionDimNum != PolicyOutputNum / 2)
		{
			UE_LOG(LogLearning, Warning, TEXT("%s: Record has wrong dimensionality for actions, got %i, policy expected %i."), *GetName(), Record.ActionDimNum, PolicyOutputNum / 2);
			continue;
		}

		TotalSampleNum += Record.SampleNum;
	}

	if (TotalSampleNum == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Recording contains no valid training data."), *GetName());
		return;
	}

	// Copy into Flat Arrays

	RecordedObservations.SetNumUninitialized({ TotalSampleNum, PolicyInputNum });
	RecordedActions.SetNumUninitialized({ TotalSampleNum, PolicyOutputNum / 2 });

	int32 SampleIdx = 0;
	for (const FLearningAgentsRecord& Record : Recording->Records)
	{
		if (Record.ObservationDimNum != PolicyInputNum) { continue; }
		if (Record.ActionDimNum != PolicyOutputNum / 2) { continue; }

		UE::Learning::Array::Copy(RecordedObservations.Slice(SampleIdx, Record.SampleNum), Record.Observations);
		UE::Learning::Array::Copy(RecordedActions.Slice(SampleIdx, Record.SampleNum), Record.Actions);
		SampleIdx += Record.SampleNum;
	}

	UE_LEARNING_CHECK(SampleIdx == TotalSampleNum);

	// Begin Training Properly

	UE_LOG(LogLearning, Display, TEXT("%s: Imitation Training Started"), *GetName());

	bIsTraining = true;
	bIsTrainingComplete = false;
	bRequestImitationTrainingStop = false;

	UE::Learning::FImitationTrainerTrainingSettings ImitationTrainingSettings;
	ImitationTrainingSettings.IterationNum = ImitationTrainerTrainingSettings.NumberOfIterations;
	ImitationTrainingSettings.LearningRateActor = ImitationTrainerTrainingSettings.LearningRate;
	ImitationTrainingSettings.LearningRateDecay = ImitationTrainerTrainingSettings.LearningRateDecay;
	ImitationTrainingSettings.WeightDecay = ImitationTrainerTrainingSettings.WeightDecay;
	ImitationTrainingSettings.BatchSize = ImitationTrainerTrainingSettings.BatchSize;
	ImitationTrainingSettings.Seed = ImitationTrainerTrainingSettings.RandomSeed;
	ImitationTrainingSettings.Device = UE::Learning::Agents::GetTrainerDevice(ImitationTrainerTrainingSettings.Device);
	ImitationTrainingSettings.bUseTensorboard = ImitationTrainerTrainingSettings.bUseTensorboard;

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
	if (!IsTraining())
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Cannot end training as we are not training!"), *GetName());
		return;
	}

	UE_LOG(LogLearning, Display, TEXT("%s: Imitation Training Ended."), *GetName());

	bRequestImitationTrainingStop = true;
	ImitationTrainingTask.Wait(FTimespan(0, 0, 30));

	bIsTraining = false;
}

bool ULearningAgentsImitationTrainer::IsTraining() const
{
	return bIsTraining;
}

bool ULearningAgentsImitationTrainer::IsTrainingComplete() const
{
	return bIsTrainingComplete;
}
