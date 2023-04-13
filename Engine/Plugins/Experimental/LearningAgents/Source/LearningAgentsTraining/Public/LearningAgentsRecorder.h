// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsManagerComponent.h"

#include "LearningArray.h"
#include "Containers/Map.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsRecorder.generated.h"

class ULearningAgentsDataStorage;
class ULearningAgentsRecord;

/** A component that can be used to create recordings of training data for imitation learning. */
UCLASS(BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsRecorder : public ULearningAgentsManagerComponent
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
	void SetupRecorder(ALearningAgentsManager* InAgentManager, ULearningAgentsType* InAgentType);

public:

	//~ Begin ULearningAgentsManagerComponent Interface
	virtual bool AddAgent(const int32 AgentId) override;

	virtual bool RemoveAgent(const int32 AgentId) override;
	//~ End ULearningAgentsManagerComponent Interface

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
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsType> AgentType;

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
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	bool bIsRecording = false;

	/** The data storage manager. It can be used to save/load agent records. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsDataStorage> DataStorage;

	/** All records which are currently being written to. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "LearningAgents")
	TMap<int32, TObjectPtr<ULearningAgentsRecord>> CurrentRecords;
};
