// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningArray.h"

#include "Components/ActorComponent.h"
#include "Containers/Map.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsRecorder.generated.h"

class ULearningAgentsDataStorage;
class ULearningAgentsRecord;

/** A component that can be used to create recordings of training data for imitation learning. */
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

	/** Will automatically call EndRecording if recording is still in-progress when play is ending. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	* Initializes this object and runs the setup functions for the underlying data storage.
	* @param InAgentType The agent type we are recording with.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void SetupRecorder(ULearningAgentsType* InAgentType);

	/** Returns true if SetupRecorder has been run successfully; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool IsRecorderSetupPerformed() const;

// ----- Agent Management -----
public:

	/**
	 * Adds an agent to this recorder.
	 * @param AgentId The id of the agent to be added.
	 * @warning The agent id must exist for the agent type.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddAgent(const int32 AgentId);

	/**
	* Removes an agent from this recorder.
	* @param AgentId The id of the agent to be removed.
	* @warning The agent id must exist for this recorder already.
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void RemoveAgent(const int32 AgentId);

	/** Returns true if the given id has been previously added to this recorder; Otherwise, false. */
	UFUNCTION(BlueprintPure, Category = "LearningAgents")
	bool HasAgent(const int32 AgentId) const;

	/**
	* Gets the agent type this recorder is associated with.
	* @param AgentClass The class to cast the agent type to (in blueprint).
	*/
	UFUNCTION(BlueprintPure, Category = "LearningAgents", meta = (DeterminesOutputType = "AgentClass"))
	ULearningAgentsType* GetAgentType(const TSubclassOf<ULearningAgentsType> AgentClass);

// ----- Recording Process -----
public:

	/**
	* Adds experience to the added agents' recordings. Call this after ULearningAgentsType::EncodeObservations and
	* either ULearningAgentsController::EncodeActions (if recording a human/AI demonstration) or
	* ULearningAgentsType::DecodeActions (if recording another policy).
	*/
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddExperience();

	/** Begin new recordings for each added agent. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void BeginRecording();

	/** End all recordings. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void EndRecording();

	/** Returns true if the recorder is currently recording; Otherwise, false. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	bool IsRecording() const;

// ----- Private Data ----- 
private:

	/** The agent type this recorder is associated with. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

	/** The agent ids this recorder is managing. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TArray<int32> SelectedAgentIds;

// ----- Recorder Configuration -----
private:

	/** Directory where records will be saved. If not set, SetupRecorder will automatically set this to the editor's default intermediate folder. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	FDirectoryPath DataDirectory;

	/** If true, recorder will automatically save all records on EndRecording. Set this to false if you want to manually save records. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSaveDataOnEndPlay = true;

// ----- Recorder State -----
private:

	/** True if recording is currently in-progress. Otherwise, false. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	bool bIsRecording = false;

	/** The data storage manager. It can be used to save/load agent records. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsDataStorage> DataStorage;

	/** All records which are currently being written to. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TMap<int32, TObjectPtr<ULearningAgentsRecord>> CurrentRecords;

private:

	UE::Learning::FIndexSet SelectedAgentsSet;
};
