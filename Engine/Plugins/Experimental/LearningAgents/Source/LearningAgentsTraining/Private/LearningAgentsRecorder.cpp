// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsRecorder.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsDataStorage.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningTrainer.h"

#include "Engine/World.h"

ULearningAgentsRecorder::ULearningAgentsRecorder() : ULearningAgentsManagerComponent() {}
ULearningAgentsRecorder::ULearningAgentsRecorder(FVTableHelper& Helper) : ULearningAgentsRecorder() {}
ULearningAgentsRecorder::~ULearningAgentsRecorder() {}

void ULearningAgentsRecorder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsRecording())
	{
		EndRecording();
	}

	Super::EndPlay(EndPlayReason);
}

void ULearningAgentsRecorder::SetupRecorder(ALearningAgentsManager* InAgentManager, ULearningAgentsInteractor* InInteractor)
{
	if (IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already performed!"), *GetName());
		return;
	}

	if (!InAgentManager)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InAgentManager is nullptr."), *GetName());
		return;
	}

	if (!InAgentManager->IsManagerSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s's SetupManager() must be run before %s can be setup."), *InAgentManager->GetName(), *GetName());
		return;
	}

	AgentManager = InAgentManager;

	if (!InInteractor)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InInteractor is nullptr."), *GetName());
		return;
}

	if (!InInteractor->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InInteractor->GetName());
		return;
	}

	Interactor = InInteractor;

	DataStorage = NewObject<ULearningAgentsDataStorage>(this, TEXT("DataStorage"));
#if WITH_EDITOR
	DataDirectory.Path = UE::Learning::Trainer::DefaultEditorIntermediatePath() / TEXT("Recordings");
#else
	UE_LOG(LogLearning, Error, TEXT("ULearningAgentsRecorder: DataDirectory was not set. This is non-editor build so need a directory setting."));
#endif

	bIsSetup = true;
}

bool ULearningAgentsRecorder::AddAgent(const int32 AgentId)
{
	bool bSuccess = Super::AddAgent(AgentId);

	if (bSuccess && IsRecording() && !CurrentRecords.Contains(AgentId))
	{
		CurrentRecords.Add(AgentId, DataStorage->CreateRecord(FName(Interactor->GetName() + "_id" + FString::FromInt(AgentId)), Interactor));
	}

	return bSuccess;
}

bool ULearningAgentsRecorder::RemoveAgent(const int32 AgentId)
{
	bool bSuccess = Super::RemoveAgent(AgentId);

	if (bSuccess && IsRecording() && CurrentRecords.Contains(AgentId))
	{
		CurrentRecords[AgentId]->Trim();
		CurrentRecords.Remove(AgentId);
	}

	return bSuccess;
}

void ULearningAgentsRecorder::AddExperience()
{
	if (!IsRecording())
	{
		UE_LOG(LogLearning, Warning, TEXT("Trying to add experience but we aren't currently recording. Call BeginRecording() before AddExperience()."));
		return;
	}

	for (const int32 AgentId : AddedAgentSet)
	{
		CurrentRecords[AgentId]->AddExperience(
			Interactor->GetObservationFeature().FeatureBuffer()[AgentId], 
			Interactor->GetActionFeature().FeatureBuffer()[AgentId]);
	}
}

bool ULearningAgentsRecorder::IsRecording() const
{
	return bIsRecording;
}

void ULearningAgentsRecorder::EndRecording()
{
	if (!IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("Not Recording!"));
		return;
	}

	if (bSaveDataOnEndPlay)
	{
		DataStorage->SaveAllRecords(DataDirectory);
	}

	for (const int32 AgentId : AddedAgentSet)
	{
		CurrentRecords[AgentId]->Trim();
	}

	CurrentRecords.Empty();

	bIsRecording = false;
}

void ULearningAgentsRecorder::BeginRecording()
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before recording can begin."));
		return;
	}

	if (IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("Already Recording!"));
		return;
	}

	for (const int32 AgentId : AddedAgentSet)
	{
		CurrentRecords.Add(AgentId, DataStorage->CreateRecord(FName(Interactor->GetName() + "_id" + FString::FromInt(AgentId)), Interactor));
	}

	bIsRecording = true;
}
