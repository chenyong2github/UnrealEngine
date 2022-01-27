// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Framework/SlateDelegates.h"

template< typename ObjectType > class TAttribute;
class UMovieSceneTrack;
class UMovieSceneSection;
class ISequencer;
class FMenuBuilder;
class ULevelSequence;
class UMovieSceneCompiledDataManager;
enum class EMovieSceneBlendType : uint8;

struct SEQUENCER_API FSequencerUtilities
{
	/* Creates a button (used for +Section) that opens a ComboButton with a user-defined sub-menu content. */
	static TSharedRef<SWidget> MakeAddButton(FText HoverText, FOnGetContent MenuContent, const TAttribute<bool>& HoverState, TWeakPtr<ISequencer> InSequencer);
	/* Creates a button (used for +Section) that fires a user-defined OnClick response with no sub-menu. */
	static TSharedRef<SWidget> MakeAddButton(FText HoverText, FOnClicked OnClicked, const TAttribute<bool>& HoverState, TWeakPtr<ISequencer> InSequencer);

	static void CreateNewSection(UMovieSceneTrack* InTrack, TWeakPtr<ISequencer> InSequencer, int32 InRowIndex, EMovieSceneBlendType InBlendType);

	static void PopulateMenu_CreateNewSection(FMenuBuilder& MenuBuilder, int32 RowIndex, UMovieSceneTrack* Track, TWeakPtr<ISequencer> InSequencer);

	static void PopulateMenu_SetBlendType(FMenuBuilder& MenuBuilder, UMovieSceneSection* Section, TWeakPtr<ISequencer> InSequencer);

	static void PopulateMenu_SetBlendType(FMenuBuilder& MenuBuilder, const TArray<TWeakObjectPtr<UMovieSceneSection>>& InSections, TWeakPtr<ISequencer> InSequencer);

	static TArray<FString> GetAssociatedMapPackages(const ULevelSequence* InSequence);

	/** 
	 * Generates a unique FName from a candidate name given a set of already existing names.  
	 * The name is made unique by appending a number to the end.
	 */
	static FName GetUniqueName(FName CandidateName, const TArray<FName>& ExistingNames);

	static FGuid DoAssignActor(ISequencer* InSequencerPtr, AActor* Actor, FGuid InObjectBinding);

	static void UpdateBindingIDs(ISequencer* InSequencerPtr, UMovieSceneCompiledDataManager* InCompiledDataManagerPtr, FGuid OldGuid, FGuid NewGuid);

	static void ShowReadOnlyError();
};
