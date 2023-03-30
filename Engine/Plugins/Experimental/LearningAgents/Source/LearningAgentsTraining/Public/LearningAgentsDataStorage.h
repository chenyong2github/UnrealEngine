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

USTRUCT(BlueprintType)
struct FRecordMetaData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FDateTime CreatedOn;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FDateTime CreatedOnUtc;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	FName RecordName;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	bool bWasLoadedFromFile = false;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "LearningAgents")
	bool bHasUnsavedChanges = false;
};

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

	void Init(int32 ObsNum, int32 ActNum);

	void AddExperience(TLearningArrayView<1, const float> Observations, TLearningArrayView<1, const float> Actions);

	 const TConstArrayView<TLearningArray<2, float>> GetObservations() const;
	const TConstArrayView<TLearningArray<2, float>> GetActions() const;

	void Trim();

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

UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsDataStorage : public UObject
{
	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsDataStorage();
	ULearningAgentsDataStorage(FVTableHelper& Helper);
	virtual ~ULearningAgentsDataStorage();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	ULearningAgentsRecord* CreateRecord(FName RecordName, ULearningAgentsType* AgentType);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	const TArray<ULearningAgentsRecord*>& GetAllRecords() const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	int32 LoadAllRecords(ULearningAgentsType* AgentType, const FDirectoryPath& Directory);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	ULearningAgentsRecord* LoadRecord(ULearningAgentsType* AgentType, const FDirectoryPath& Directory, const FString& Filename);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveAllRecords(const FDirectoryPath& Directory) const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SaveRecord(const FDirectoryPath& Directory, ULearningAgentsRecord* Record) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<TObjectPtr<ULearningAgentsRecord>> Records;

	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	bool bPrependUtcTimeStamp = true;

	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	FString FileExtension = TEXT("bin");
};
