// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsRecorder.h"

#include "Engine/World.h"
#include "LearningAgentsDataStorage.h"
#include "LearningAgentsType.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningTrainer.h"

ULearningAgentsRecorder::ULearningAgentsRecorder() : ULearningAgentsTypeComponent() {}
ULearningAgentsRecorder::ULearningAgentsRecorder(FVTableHelper& Helper) : ULearningAgentsRecorder() {}
ULearningAgentsRecorder::~ULearningAgentsRecorder() {}

void ULearningAgentsRecorder::OnRegister()
{
	Super::OnRegister();

	if (!GetWorld()->IsGameWorld())
	{
		// We're not in a game yet so we don't need the storage yet
		return;
	}

	DataStorage = NewObject<ULearningAgentsDataStorage>(this);

	if (DataDirectory.Path.IsEmpty())
	{
#if WITH_EDITOR
		UE_LOG(LogLearning, Display, TEXT("ULearningAgentsRecorder writing data to default editor intermediate directory since DataDirectory was not set."));
		DataDirectory.Path = UE::Learning::Trainer::DefaultEditorIntermediatePath();
#else
		UE_LOG(LogLearning, Error, TEXT("ULearningAgentsRecorder: DataDirectory was not set. This is non-editor build so need a directory setting."));
#endif
	}
}

void ULearningAgentsRecorder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRecording();

	if (bSaveDataOnEndPlay)
	{
		DataStorage->SaveAllRecords(DataDirectory);
	}

	Super::EndPlay(EndPlayReason);
}

void ULearningAgentsRecorder::AddExperience()
{
	if (!IsRecording())
	{
		UE_LOG(LogLearning, Warning, TEXT("Trying to add experience but we aren't currently recording. Call StartRecording() before AddExperience()."));
		return;
	}

	for (const int32 AgentId : SelectedAgentsSet)
	{
		CurrentRecords[AgentId]->AddExperience(AgentType->GetObservationFeature().FeatureBuffer()[AgentId], AgentType->GetActionFeature().FeatureBuffer()[AgentId]);
	}
}

const bool ULearningAgentsRecorder::IsRecording() const
{
	return !CurrentRecords.IsEmpty();
}

void ULearningAgentsRecorder::StartRecording()
{
	for (const int32 AgentId : SelectedAgentsSet)
	{
		CurrentRecords.Add(AgentId, DataStorage->CreateRecord(FName(AgentType->GetName() + "_id" + FString::FromInt(AgentId)), AgentType));
	}
}

void ULearningAgentsRecorder::StopRecording()
{
	for (TPair<int32, ULearningAgentsRecord*> Record : CurrentRecords)
	{
		Record.Value->Trim();
	}

	CurrentRecords.Empty();
}
