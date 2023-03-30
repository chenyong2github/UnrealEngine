// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsTypeComponent.h"

#include "Containers/Map.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsRecorder.generated.h"

class ULearningAgentsType;
class ULearningAgentsDataStorage;
class ULearningAgentsRecord;

UCLASS(Abstract, BlueprintType, Blueprintable)
class LEARNINGAGENTSTRAINING_API ULearningAgentsRecorder : public ULearningAgentsTypeComponent
{
	GENERATED_BODY()

public:
	// These constructors/destructors are needed to make forward declarations happy
	ULearningAgentsRecorder();
	ULearningAgentsRecorder(FVTableHelper& Helper);
	virtual ~ULearningAgentsRecorder();

	virtual void OnRegister() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

//** ----- Recording Process ----- */
public:

	/** Adds experience to tracked agents' records. Each agent id whose record is full will be returned, e.g. so you can reset them. */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void AddExperience();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	const bool IsRecording() const;

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "LearningAgents")
	void StopRecording();

//** ----- Recorder Configuration ----- */
private:

	/** Directory where records will be saved. If not set, BeginPlay will automatically set this to the editor's default intermediate folder. */
	UPROPERTY(EditDefaultsOnly, Category = "LearningAgents")
	FDirectoryPath DataDirectory;

	/** If true, recorder will automatically save all records on EndPlay. Set this to false if you want to manually save records. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	bool bSaveDataOnEndPlay = true;

//** ----- Recorder State ----- */
private:

	/** The data storage manager. It can be used to save/load agent records. */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TObjectPtr<ULearningAgentsDataStorage> DataStorage;

	/** All records which are currently being written */
	UPROPERTY(VisibleAnywhere, Category = "LearningAgents")
	TMap<int32, TObjectPtr<ULearningAgentsRecord>> CurrentRecords;
};
