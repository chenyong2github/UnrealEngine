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
class FMenuBuilder;
class UMovieSceneCompiledDataManager;
class UMovieSceneSection;

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
	
	/** Snap sections to timeline using source timecode */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void SnapSectionsToTimelineUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections);

	/** Sync section using source timecode */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void SyncSectionsUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections);

	/** Bake transform */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void BakeTransform(const TArray<FSequencerBindingProxy>& ObjectBindings, const FFrameTime& BakeInTime, const FFrameTime& BakeOutTime, const FFrameTime& BakeInterval, const FMovieSceneScriptingParams& Params = FMovieSceneScriptingParams());

	/** Attempts to automatically fix up broken actor references in the current scene */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void FixActorReferences();

	/** Assigns the given actors to the binding */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void AddActorsToBinding(const TArray<AActor*>& Actors, const FSequencerBindingProxy& ObjectBinding);

	/** Replaces the binding with the given actors */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void ReplaceBindingWithActors(const TArray<AActor*>& Actors, const FSequencerBindingProxy& ObjectBinding);

	/** Removes the given actors from the binding */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveActorsFromBinding(const TArray<AActor*>& Actors, const FSequencerBindingProxy& ObjectBinding);

	/** Remove all bound actors from this track */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveAllBindings(const FSequencerBindingProxy& ObjectBinding);

	/** Remove missing objects bound to this track */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveInvalidBindings(const FSequencerBindingProxy& ObjectBinding);

	/** Rebind the component binding to the requested component */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RebindComponent(const TArray<FSequencerBindingProxy>& ComponentBindings, const FName& ComponentName);

private:

	TSharedPtr<ISequencer> GetActiveSequencer();
	
	void SnapSectionsToTimelineUsingSourceTimecodeInternal();
	void SyncSectionsUsingSourceTimecodeInternal();
	void BakeTransformInternal();
	void AddActorsToBindingInternal();
	void ReplaceBindingWithActorsInternal();
	void RemoveActorsFromBindingInternal();
	void RemoveAllBindingsInternal();
	void RemoveInvalidBindingsInternal();
	void RebindComponentInternal(const FName& ComponentName);

	void AddAssignActorMenu(FMenuBuilder& MenuBuilder);

	void GetRebindComponentNames(TArray<FName>& OutComponentNames);
	void RebindComponentMenu(FMenuBuilder& MenuBuilder);

	FDelegateHandle OnSequencerCreatedHandle;

	/* List of sequencers that have been created */
	TArray<TWeakPtr<ISequencer>> Sequencers;

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FExtender> TransformMenuExtender;
	TSharedPtr<FExtender> FixActorReferencesMenuExtender;

	TSharedPtr<FExtender> AssignActorMenuExtender;
	TSharedPtr<FExtender> RebindComponentMenuExtender;
};
