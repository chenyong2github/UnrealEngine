// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsRecorder.h"

#include "Engine/World.h"
#include "LearningAgentsDataStorage.h"
#include "LearningAgentsType.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningTrainer.h"

ULearningAgentsRecorder::ULearningAgentsRecorder() : UActorComponent() {}
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

void ULearningAgentsRecorder::SetupRecorder(ULearningAgentsType* InAgentType)
{
	if (IsRecorderSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup already performed!"));
		return;
	}

	if (!InAgentType)
	{
		UE_LOG(LogLearning, Error, TEXT("SetupRecorder called but AgentType is nullptr."));
		return;
	}

	if (!InAgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("AgentType Setup must be run before recorder can be setup."));
		return;
	}

	AgentType = InAgentType;

	// Create Data Storage

	DataStorage = NewObject<ULearningAgentsDataStorage>(this, TEXT("DataStorage"));
#if WITH_EDITOR
	DataDirectory.Path = UE::Learning::Trainer::DefaultEditorIntermediatePath() / TEXT("Recordings");
#else
	UE_LOG(LogLearning, Error, TEXT("ULearningAgentsRecorder: DataDirectory was not set. This is non-editor build so need a directory setting."));
#endif
}

bool ULearningAgentsRecorder::IsRecorderSetupPerformed() const
{
	return AgentType ? true : false;
}

void ULearningAgentsRecorder::AddAgent(const int32 AgentId)
{
	if (!IsRecorderSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Recorder setup must be run before agents can be added!"));
		return;
	}

	if (!AgentType->GetOccupiedAgentSet().Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to add: AgentId %d not found on AgentType. Make sure to add agents to the agent type before adding."), AgentId);
		return;
	}

	if (SelectedAgentIds.Contains(AgentId))
	{
		UE_LOG(LogLearning, Error, TEXT("AgentId %i is already included in agents set"), AgentId);
		return;
	}

	SelectedAgentIds.Add(AgentId);
	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();

	if (IsRecording() && !CurrentRecords.Contains(AgentId))
	{
		CurrentRecords.Add(AgentId, DataStorage->CreateRecord(FName(AgentType->GetName() + "_id" + FString::FromInt(AgentId)), AgentType));
	}
}

void ULearningAgentsRecorder::RemoveAgent(const int32 AgentId)
{
	if (!IsRecorderSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Recorder setup must be run before agents can be removed!"));
		return;
	}

	if (SelectedAgentIds.RemoveSingleSwap(AgentId, false) == 0)
	{
		UE_LOG(LogLearning, Error, TEXT("Unable to remove: AgentId %d not found in the added agents set."), AgentId);
		return;
	}

	SelectedAgentsSet = SelectedAgentIds;
	SelectedAgentsSet.TryMakeSlice();

	if (IsRecording() && CurrentRecords.Contains(AgentId))
	{
		CurrentRecords[AgentId]->Trim();
		CurrentRecords.Remove(AgentId);
	}
}

bool ULearningAgentsRecorder::HasAgent(const int32 AgentId) const
{
	return SelectedAgentsSet.Contains(AgentId);
}

ULearningAgentsType* ULearningAgentsRecorder::GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass)
{
	if (!IsRecorderSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Recorder setup must be run before getting the agent type!"));
		return nullptr;
	}

	return AgentType;
}

void ULearningAgentsRecorder::AddExperience()
{
	if (!IsRecording())
	{
		UE_LOG(LogLearning, Warning, TEXT("Trying to add experience but we aren't currently recording. Call BeginRecording() before AddExperience()."));
		return;
	}

	// Add Experience to Records

	for (const int32 AgentId : SelectedAgentsSet)
	{
		CurrentRecords[AgentId]->AddExperience(
			AgentType->GetObservationFeature().FeatureBuffer()[AgentId], 
			AgentType->GetActionFeature().FeatureBuffer()[AgentId]);
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

	// Save Records

	if (bSaveDataOnEndPlay)
	{
		DataStorage->SaveAllRecords(DataDirectory);
	}

	// Trim Records

	for (const int32 AgentId : SelectedAgentsSet)
	{
		CurrentRecords[AgentId]->Trim();
	}

	// Reset Current Records

	CurrentRecords.Empty();

	// Done

	bIsRecording = false;
}

void ULearningAgentsRecorder::BeginRecording()
{
	if (IsRecorderSetupPerformed())
	{
		UE_LOG(LogLearning, Error, TEXT("Setup must be run before recording can begin."));
		return;
	}

	if (IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("Already Recording!"));
		return;
	}

	// Create Records

	for (const int32 AgentId : SelectedAgentsSet)
	{
		CurrentRecords.Add(AgentId, DataStorage->CreateRecord(FName(AgentType->GetName() + "_id" + FString::FromInt(AgentId)), AgentType));
	}

	// Done

	bIsRecording = true;
}
