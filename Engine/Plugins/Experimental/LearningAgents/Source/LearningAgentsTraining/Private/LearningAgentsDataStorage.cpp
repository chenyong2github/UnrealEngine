// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsDataStorage.h"

#include "Engine/EngineTypes.h"
#include "HAL/FileManager.h"
#include "LearningAgentsType.h"
#include "LearningFeatureObject.h"
#include "LearningLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"

ULearningAgentsRecord::ULearningAgentsRecord() {}
ULearningAgentsRecord::ULearningAgentsRecord(FVTableHelper& Helper) {}
ULearningAgentsRecord::~ULearningAgentsRecord() {}

void ULearningAgentsRecord::Init(int32 ObsNum, int32 ActNum)
{
	ObservationNum = ObsNum;
	ActionNum = ActNum;

	AddChunk();
}

void ULearningAgentsRecord::AddExperience(TLearningArrayView<1, const float> NewObservations, TLearningArrayView<1, const float> NewActions)
{
	MetaData.bHasUnsavedChanges = true;

	if (DataIndex >= ChunkSize)
	{
		AddChunk();
	}

	UE::Learning::Array::Copy(Observations.Last()[DataIndex], NewObservations);
	UE::Learning::Array::Copy(Actions.Last()[DataIndex], NewActions);

	DataIndex++;
}

const TConstArrayView<TLearningArray<2, float>> ULearningAgentsRecord::GetObservations() const
{
	return Observations;
}

const TConstArrayView<TLearningArray<2, float>> ULearningAgentsRecord::GetActions() const
{
	return Actions;
}

void ULearningAgentsRecord::Trim()
{
	Observations.Last().SetNumUninitialized({ DataIndex, ObservationNum });
	Actions.Last().SetNumUninitialized({ DataIndex, ActionNum });
}

void ULearningAgentsRecord::AddChunk()
{
	TLearningArray<2, float>& Observation = Observations.AddDefaulted_GetRef();
	Observation.SetNumUninitialized({ ChunkSize, ObservationNum });

	TLearningArray<2, float>& Action = Actions.AddDefaulted_GetRef();
	Action.SetNumUninitialized({ ChunkSize, ActionNum });

	DataIndex = 0;
}

ULearningAgentsDataStorage::ULearningAgentsDataStorage() {}
ULearningAgentsDataStorage::ULearningAgentsDataStorage(FVTableHelper& Helper) {}
ULearningAgentsDataStorage::~ULearningAgentsDataStorage() {}

ULearningAgentsRecord* ULearningAgentsDataStorage::CreateRecord(FName RecordName, ULearningAgentsType* AgentType)
{
	if (!AgentType)
	{
		UE_LOG(LogLearning, Warning, TEXT("CreateRecord: AgentType is nullptr. You must pass a valid agent type. Skipping creation."));
		return nullptr;
	}

	if (!AgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Warning, TEXT("CreateRecord: AgentType Setup must be performed before record can be created."));
		return nullptr;
	}

	ULearningAgentsRecord* NewRecord = NewObject<ULearningAgentsRecord>(this, RecordName);
	NewRecord->MetaData.AgentType = AgentType;
	NewRecord->MetaData.CreatedOn = FDateTime::Now();
	NewRecord->MetaData.CreatedOnUtc = FDateTime::UtcNow();
	NewRecord->MetaData.RecordName = RecordName;

	NewRecord->Init(AgentType->GetObservationFeature().DimNum(), AgentType->GetActionFeature().DimNum());
	Records.Add(NewRecord);

	return NewRecord;
}

const TArray<ULearningAgentsRecord*>& ULearningAgentsDataStorage::GetAllRecords() const
{
	return Records;
}

int32 ULearningAgentsDataStorage::LoadAllRecords(ULearningAgentsType* AgentType, const FDirectoryPath& Directory)
{
	if (!AgentType)
	{
		UE_LOG(LogLearning, Warning, TEXT("LoadAllRecords: AgentType is nullptr. You must pass a valid agent type. Skipping loading."));
		return 0;
	}

	TArray<FString> DataFiles;
	IFileManager::Get().FindFiles(DataFiles, *Directory.Path, *FileExtension);

	if (DataFiles.Num() == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("LoadAllRecords: Found no .%s files in directory %s"), *FileExtension, *Directory.Path);
		return 0;
	}

	for (const FString& DataFile : DataFiles)
	{
		LoadRecord(AgentType, Directory, DataFile);
	}

	return DataFiles.Num();
}

ULearningAgentsRecord* ULearningAgentsDataStorage::LoadRecord(ULearningAgentsType* AgentType, const FDirectoryPath& Directory, const FString& Filename)
{
	if (!AgentType)
	{
		UE_LOG(LogLearning, Warning, TEXT("LoadRecord: AgentType is nullptr. You must pass a valid agent type. Skipping loading."));
		return nullptr;
	}

	if (!AgentType->IsSetupPerformed())
	{
		UE_LOG(LogLearning, Warning, TEXT("LoadRecord: AgentType Setup must be performed before record can be created."));
		return nullptr;
	}

	TArray<uint8> Bytes;

	FString InputFilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() + Filename;

	if (!FPaths::FileExists(InputFilePath))
	{
		UE_LOG(LogLearning, Error, TEXT("LoadRecord: FilePath %s does not exist."), *InputFilePath);
		return nullptr;
	}

	const bool bSuccess = FFileHelper::LoadFileToArray(Bytes, *InputFilePath);
	if (!bSuccess)
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to load data from file %s"), *InputFilePath);
		return nullptr;
	}

	FMemoryReader Reader(Bytes);

	int32 ChunksNum = -1;
	Reader.Serialize(&ChunksNum, sizeof(int32));
	if (ChunksNum <= 0)
	{
		UE_LOG(LogLearning, Error, TEXT("LoadRecord: file ChunksNum header was %d. Must be greater than 0."), ChunksNum);
		return nullptr;
	}

	int32 ObsNum = -1;
	Reader.Serialize(&ObsNum, sizeof(int32));
	int32 AgentObsNum = AgentType->GetObservationFeature().DimNum();
	if (AgentObsNum != ObsNum)
	{
		UE_LOG(LogLearning, Error, TEXT("LoadRecord: Observation data dimension size %d incompatible with agent's observation dimension size %d"), ObsNum, AgentObsNum);
		return nullptr;
	}

	int32 ActNum = -1;
	Reader.Serialize(&ActNum, sizeof(int32));
	int32 AgentActionNum = AgentType->GetActionFeature().DimNum();
	if (AgentActionNum != ActNum)
	{
		UE_LOG(LogLearning, Error, TEXT("LoadRecord: Action data dimension size %d incompatible with agent's action dimension size %d"), ObsNum, AgentObsNum);
		return nullptr;
	}

	// Load all the data into the first chunk
	ULearningAgentsRecord* Record = CreateRecord(FName(Filename), AgentType);
	Reader.Serialize(&Record->MetaData.CreatedOn, sizeof(FDateTime));
	Reader.Serialize(&Record->MetaData.CreatedOnUtc, sizeof(FDateTime));
	Record->MetaData.bWasLoadedFromFile = true;

	for (int32 ChunkIndex = 0; ChunkIndex < ChunksNum; ChunkIndex++)
	{
		if (ChunkIndex > 0)
		{
			Record->AddChunk();
		}

		UE::Learning::Array::Serialize(Reader, Record->Observations[ChunkIndex]);
		UE::Learning::Array::Serialize(Reader, Record->Actions[ChunkIndex]);
	}

	return Record;
}

void ULearningAgentsDataStorage::SaveAllRecords(const FDirectoryPath& Directory) const
{
	for (TObjectPtr<ULearningAgentsRecord> Record : Records)
	{
		if (Record->MetaData.bWasLoadedFromFile && !Record->MetaData.bHasUnsavedChanges)
		{
			continue;
		}

		SaveRecord(Directory, Record);
	}
}

void ULearningAgentsDataStorage::SaveRecord(const FDirectoryPath& Directory, ULearningAgentsRecord* Record) const
{
	if (!Record)
	{
		UE_LOG(LogLearning, Warning, TEXT("SaveRecord: Record is nullptr. You must pass a valid record. Skipping save."));
		return;
	}

	Record->Trim();

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);

	// Write some info we would need to extract the data again
	int32 ChunksNum = Record->Observations.Num();
	int32 ObsNum = Record->Observations[0].Num<1>();
	int32 ActNum = Record->Actions[0].Num<1>();

	Writer << ChunksNum;
	Writer << ObsNum;
	Writer << ActNum;
	Writer << Record->MetaData.CreatedOn;
	Writer << Record->MetaData.CreatedOnUtc;

	for (int32 ChunkIndex = 0; ChunkIndex < ChunksNum; ChunkIndex++)
	{
		UE::Learning::Array::Serialize(Writer, Record->Observations[ChunkIndex]);
		UE::Learning::Array::Serialize(Writer, Record->Actions[ChunkIndex]);
	}

	const FString FileName = Record->MetaData.RecordName.IsNone() ? Record->MetaData.AgentType->GetName() : Record->MetaData.RecordName.ToString();
	const FString OutputFilePath = Directory.Path + FGenericPlatformMisc::GetDefaultPathSeparator() +
		(!Record->MetaData.bWasLoadedFromFile && bPrependUtcTimeStamp ? FDateTime::UtcNow().ToString() + "_" : "") + FileName + "." + FileExtension;
	const bool bSuccess = FFileHelper::SaveArrayToFile(Bytes, *OutputFilePath, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
	if (!bSuccess)
	{
		UE_LOG(LogLearning, Error, TEXT("Failed to write data to output file for %s"), *FileName);
	}

	UE_LOG(LogLearning, Display, TEXT("SaveRecord: Saved data for agent to %s"), *OutputFilePath);
}
