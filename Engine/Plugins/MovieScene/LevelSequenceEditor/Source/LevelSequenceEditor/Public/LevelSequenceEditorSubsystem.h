// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "ISequencer.h"

#include "SequenceTimeUnit.h"
#include "SequencerBindingProxy.h"

#include "LevelSequenceEditorSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLevelSequenceEditor, Log, All);

class FExtender;
class UMovieSceneCompiledDataManager;

USTRUCT(BlueprintType)
struct FMovieSceneScriptingParams
{
	GENERATED_BODY()
		
	FMovieSceneScriptingParams() {}

	ESequenceTimeUnit TimeUnit = ESequenceTimeUnit::DisplayRate;
};

/**
* ULevelSequenceEditorSubsystem
* Subsystem for level sequencer related utilities to scripts
*/
UCLASS(Blueprintable)
class ULevelSequenceEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void OnSequencerCreated(TSharedRef<ISequencer> InSequencer);
	
	/** Bake transform */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void BakeTransform(const TArray<FSequencerBindingProxy>& ObjectBindings, const FFrameTime& BakeInTime, const FFrameTime& BakeOutTime, const FFrameTime& BakeInterval, const FMovieSceneScriptingParams& Params = FMovieSceneScriptingParams());

	/** Attempts to automatically fix up broken actor references in the current scene */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void FixActorReferences();

private:

	TSharedPtr<ISequencer> GetActiveSequencer();
	
	void BakeTransformInternal();

	FDelegateHandle OnSequencerCreatedHandle;

	/* List of sequencers that have been created */
	TArray<TWeakPtr<ISequencer>> Sequencers;

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FExtender> BakeTransformMenuExtender;
	TSharedPtr<FExtender> FixActorReferencesMenuExtender;
};
