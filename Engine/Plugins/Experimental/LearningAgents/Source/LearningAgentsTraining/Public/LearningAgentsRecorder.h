// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Components/ActorComponent.h"
#include "Containers/Map.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsRecorder.generated.h"

class ULearningAgentsDataStorage;
class ULearningAgentsRecord;

UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsRecorder : public UActorComponent
{
	GENERATED_BODY()

// ----- Setup -----
public:

	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsRecorder();
	ULearningAgentsRecorder(FVTableHelper& Helper);
	virtual ~ULearningAgentsRecorder();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupRecorder(ULearningAgentsType* InAgentType);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsRecorderSetupPerformed() const;

// ----- Agent Management -----
public:

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(int32 AgentId);

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(int32 AgentId);

	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(int32 AgentId) const;

	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(TSubclassOf<ULearningAgentsType> AgentClass);

// ----- Recording Process -----
public:

	/** Adds experience to tracked agents' records. Each agent id whose record is full will be returned, e.g. so you can reset them. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddExperience();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginRecording();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndRecording();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	bool IsRecording() const;

// ----- Private Data ----- 
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

// ----- Recorder Configuration -----
private:

	/** Directory where records will be saved. If not set, BeginPlay will automatically set this to the editor's default intermediate folder. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	FDirectoryPath DataDirectory;

	/** If true, recorder will automatically save all records on EndPlay. Set this to false if you want to manually save records. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSaveDataOnEndPlay = true;

// ----- Recorder State -----
private:

	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsRecording = false;

	/** The data storage manager. It can be used to save/load agent records. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsDataStorage> DataStorage;

	/** All records which are currently being written */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TMap<int32, TObjectPtr<ULearningAgentsRecord>> CurrentRecords;

private:

	UE::Learning::FIndexSet SelectedAgentsSet;
};
