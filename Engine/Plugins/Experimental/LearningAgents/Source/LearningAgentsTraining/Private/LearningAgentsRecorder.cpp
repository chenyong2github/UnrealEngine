// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsRecorder.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsRecording.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "LearningTrainer.h"

#include "UObject/Package.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

FLearningAgentsRecorderPathSettings::FLearningAgentsRecorderPathSettings()
{
	IntermediateRelativePath.Path = FPaths::ProjectIntermediateDir();
}

TLearningArrayView<1, float> ULearningAgentsRecorder::FAgentRecordBuffer::GetObservation(const int32 SampleIdx)
{
	return Observations[SampleIdx / ChunkSize][SampleIdx % ChunkSize];
}

TLearningArrayView<1, float> ULearningAgentsRecorder::FAgentRecordBuffer::GetAction(const int32 SampleIdx)
{
	return Actions[SampleIdx / ChunkSize][SampleIdx % ChunkSize];
}

TLearningArrayView<1, const float> ULearningAgentsRecorder::FAgentRecordBuffer::GetObservation(const int32 SampleIdx) const
{
	return Observations[SampleIdx / ChunkSize][SampleIdx % ChunkSize];
}

TLearningArrayView<1, const float> ULearningAgentsRecorder::FAgentRecordBuffer::GetAction(const int32 SampleIdx) const
{
	return Actions[SampleIdx / ChunkSize][SampleIdx % ChunkSize];
}

void ULearningAgentsRecorder::FAgentRecordBuffer::Push(
	const TLearningArrayView<1, const float> Observation,
	const TLearningArrayView<1, const float> Action)
{
	if (SampleNum / ChunkSize <= Observations.Num())
	{
		Observations.AddDefaulted_GetRef().SetNumUninitialized({ ChunkSize, Observation.Num() });
		Actions.AddDefaulted_GetRef().SetNumUninitialized({ ChunkSize, Action.Num() });
	}

	UE::Learning::Array::Copy(GetObservation(SampleNum), Observation);
	UE::Learning::Array::Copy(GetAction(SampleNum), Action);
	SampleNum++;
}

bool ULearningAgentsRecorder::FAgentRecordBuffer::IsEmpty() const
{
	return SampleNum == 0;
}

void ULearningAgentsRecorder::FAgentRecordBuffer::Empty()
{
	SampleNum = 0;
	Observations.Empty();
	Actions.Empty();
}

void ULearningAgentsRecorder::FAgentRecordBuffer::CopyToRecord(FLearningAgentsRecord& Record) const
{
	UE_LEARNING_CHECK(SampleNum > 0);

	Record.SampleNum = SampleNum;
	Record.ObservationDimNum = GetObservation(0).Num();
	Record.ActionDimNum = GetAction(0).Num();
	Record.Observations.SetNumUninitialized({ SampleNum, Observations[0].Num<1>() });
	Record.Actions.SetNumUninitialized({ SampleNum, Actions[0].Num<1>() });

	for (int32 SampleIdx = 0; SampleIdx < SampleNum; SampleIdx++)
	{
		UE::Learning::Array::Copy(Record.Observations[SampleIdx], GetObservation(SampleIdx));
		UE::Learning::Array::Copy(Record.Actions[SampleIdx], GetAction(SampleIdx));
	}
}

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

void ULearningAgentsRecorder::SetupRecorder(
	ALearningAgentsManager* InAgentManager, 
	ULearningAgentsInteractor* InInteractor,
	const FLearningAgentsRecorderPathSettings& RecorderPathSettings)
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
		UE_LOG(LogLearning, Error, TEXT("%s: %s's SetupManager must be run before it can be used."), *GetName(), *InAgentManager->GetName());
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

	Recording = NewObject<ULearningAgentsRecording>(this, TEXT("Recording"));
	RecordingDirectory = RecorderPathSettings.IntermediateRelativePath.Path / TEXT("LearningAgents") / RecorderPathSettings.RecordingsSubdirectory;

	RecordBuffers.Empty();
	RecordBuffers.SetNum(AgentManager->GetMaxInstanceNum());

	bIsSetup = true;
}

bool ULearningAgentsRecorder::RemoveAgent(const int32 AgentId)
{
	bool bSuccess = Super::RemoveAgent(AgentId);

	if (bSuccess)
	{
		if (!RecordBuffers[AgentId].IsEmpty())
		{
			RecordBuffers[AgentId].CopyToRecord(Recording->Records.AddDefaulted_GetRef());
			RecordBuffers[AgentId].Empty();
		}
	}

	return bSuccess;
}

void ULearningAgentsRecorder::AddExperience()
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Trying to add experience but we aren't currently recording. Call BeginRecording before AddExperience."), *GetName());
		return;
	}

	for (const int32 AgentId : AddedAgentSet)
	{
		RecordBuffers[AgentId].Push(
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
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Cannot end recording as we are not currently recording!"), *GetName());
		return;
	}

	// Write to Recording

	for (const int32 AgentId : AddedAgentSet)
	{
		if (!RecordBuffers[AgentId].IsEmpty())
		{
			RecordBuffers[AgentId].CopyToRecord(Recording->Records.AddDefaulted_GetRef());
			RecordBuffers[AgentId].Empty();
		}
	}

	// Save Recording to Intermediate Directory

	FFilePath RecordingFilePath;
	RecordingFilePath.FilePath = RecordingDirectory / FString::Printf(TEXT("%s_%s.bin"), *GetName(), *FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d_%H-%M-%S")));
	SaveRecordingToFile(RecordingFilePath);

	bIsRecording = false;
}

void ULearningAgentsRecorder::LoadRecordingFromFile(const FFilePath& File)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	Recording->LoadRecordingFromFile(File);
}

void ULearningAgentsRecorder::SaveRecordingToFile(const FFilePath& File) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	Recording->SaveRecordingToFile(File);
}

void ULearningAgentsRecorder::LoadRecordingFromAsset(const ULearningAgentsRecording* Asset)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!Asset)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Asset is nullptr."), *GetName());
		return;
	}

	Recording->Records = Asset->Records;
}

void ULearningAgentsRecorder::SaveRecordingToAsset(ULearningAgentsRecording* Asset) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!Asset)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Asset is nullptr."), *GetName());
		return;
	}

	Asset->Records = Recording->Records;

	// Manually mark the package as dirty since just using `Modify` prevents 
	// marking packages as dirty during PIE which is most likely when this
	// is being used.
	if (UPackage* Package = Asset->GetPackage())
	{
		const bool bIsDirty = Package->IsDirty();

		if (!bIsDirty)
		{
			Package->SetDirtyFlag(true);
		}

		Package->PackageMarkedDirtyEvent.Broadcast(Package, bIsDirty);
	}
}

void ULearningAgentsRecorder::AppendRecordingToAsset(ULearningAgentsRecording* Asset) const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!Asset)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Asset is invalid."), *GetName());
		return;
	}

	Asset->Records.Append(Recording->Records);

	// Manually mark the package as dirty since just using `Modify` prevents 
	// marking packages as dirty during PIE which is most likely when this
	// is being used.
	if (UPackage* Package = Asset->GetPackage())
	{
		const bool bIsDirty = Package->IsDirty();

		if (!bIsDirty)
		{
			Package->SetDirtyFlag(true);
		}

		Package->PackageMarkedDirtyEvent.Broadcast(Package, bIsDirty);
	}
}

void ULearningAgentsRecorder::BeginRecording(bool bReinitializeRecording)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (IsRecording())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Cannot being recording as we are already Recording!"), *GetName());
		return;
	}

	if (bReinitializeRecording)
	{
		Recording->Records.Empty();
	}

	for (const int32 AgentId : AddedAgentSet)
	{
		RecordBuffers[AgentId].Empty();
	}

	bIsRecording = true;
}

const ULearningAgentsRecording* ULearningAgentsRecorder::GetCurrentRecording() const
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return nullptr;
	}

	return Recording;
}