// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Containers/Array.h"
#include "LearningArray.h"
#include "Misc/DateTime.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsDataStorage.generated.h"

struct FDirectoryPath;
class ULearningAgentsType;

/** Metadata for an agent record. */
USTRUCT(BlueprintType)
struct FRecordMetaData
{
	GENERATED_BODY()

	/** The agent type the data was recorded from. Determine the shape of observations and actions. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The time the data was recorded (local). */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FDateTime CreatedOn;

	/** The time the data was recorded (universal). */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FDateTime CreatedOnUtc;

	/** The name of the record. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FName RecordName;

	/** True if this record was loaded from a file. Otherwise, false. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	bool bWasLoadedFromFile = false;

	/** True if this record has unsaved changes. Otherwise, false. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	bool bHasUnsavedChanges = false;
};

/** A recording of a human/AI demonstration from which we can learn. */
UCLASS(BlueprintType)
class LEARNINGAGENTSTRAINING_API ULearningAgentsRecord : public UObject
{
	friend class ULearningAgentsDataStorage;

	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsRecord();
	ULearningAgentsRecord(FVTableHelper& Helper);
	virtual ~ULearningAgentsRecord();

	/** Initialize this record with the given observation and action sizes. */
	void Init(const int32 ObsNum, const int32 ActNum);

	/** Add experience data to this record. */
	void AddExperience(TLearningArrayView<1, const float> Observations, TLearningArrayView<1, const float> Actions);

	/** Get a const view of the observations. */
	const TConstArrayView<TLearningArray<2, float>> GetObservations() const;

	/** Get a const view of the actions. */
	const TConstArrayView<TLearningArray<2, float>> GetActions() const;

	/** Remove unused space from this record. Call after no more data will be written. */
	void Trim();

	/** The metadata for this record. */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FRecordMetaData MetaData;

private:
	void AddChunk();

	int32 DataIndex = 0;
	int32 ChunkSize = 1024;

	int32 ObservationNum = 0;
	int32 ActionNum = 0;

	TArray<TLearningArray<2, float>> Observations;
	TArray<TLearningArray<2, float>> Actions;
};

/** A manager for the saving/loading of recordings. */
UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsDataStorage : public UObject
{
	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsDataStorage();
	ULearningAgentsDataStorage(FVTableHelper& Helper);
	virtual ~ULearningAgentsDataStorage();

	/** Create a new record with the given name for the agent type. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	ULearningAgentsRecord* CreateRecord(const FName RecordName, ULearningAgentsType* AgentType);

	/** Get all currently loaded records. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const TArray<ULearningAgentsRecord*>& GetAllRecords() const;

	/** Load all records from a given directory, ensuring they are valid for the given agent type. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	int32 LoadAllRecords(ULearningAgentsType* AgentType, const FDirectoryPath& Directory);

	/** Load a record from a given directory and filename, ensuring the data is valid for the given agent type. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	ULearningAgentsRecord* LoadRecord(ULearningAgentsType* AgentType, const FDirectoryPath& Directory, const FString& Filename);

	/** Save all records which have unsaved changes to the given directory. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveAllRecords(const FDirectoryPath& Directory) const;

	/** Forcefully save a record to the given directory. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveRecord(const FDirectoryPath& Directory, ULearningAgentsRecord* Record) const;

private:
	
	/** All currently loaded records. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsRecord>> Records;

	/** If true, prepends a timestamp to the saved file names. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	bool bPrependUtcTimeStamp = true;

	/** The file extension to use when searching for records to load or saving a new record. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	FString FileExtension = TEXT("bin");
};
