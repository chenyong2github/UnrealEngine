// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneSpawnSection.h"
#include "UObject/SequencerObjectVersion.h"

#include "MovieSceneTimeHelpers.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "EntitySystem/MovieSceneEntityManager.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneSpawnablesSystem.h"


UMovieSceneSpawnSection::UMovieSceneSpawnSection(const FObjectInitializer& Init)
	: Super(Init)
{
	EvalOptions.EnableAndSetCompletionMode
		(GetLinkerCustomVersion(FSequencerObjectVersion::GUID) < FSequencerObjectVersion::WhenFinishedDefaultsToProjectDefault ? 
			EMovieSceneCompletionMode::RestoreState : 
			EMovieSceneCompletionMode::ProjectDefault);

	BoolCurve.SetDefault(true);
}

UE::MovieScene::ESequenceUpdateResult UMovieSceneSpawnSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	UMovieScene* ParentMovieScene = GetTypedOuter<UMovieScene>();
	if (!ParentMovieScene || !ParentMovieScene->FindPossessable(Params.ObjectBindingID))
	{
		OutImportedEntity->AddBuilder(
			FEntityBuilder()
			.Add(FBuiltInComponentTypes::Get()->SpawnableBinding, Params.ObjectBindingID)
		);

		return ESequenceUpdateResult::EntitiesDirty;
	}

	return ESequenceUpdateResult::NoChange;
}

bool UMovieSceneSpawnSection::PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, FMovieSceneEntityComponentField* OutField)
{
	// Only add the valid section ranges to the tree
	TArrayView<const FFrameNumber> Times  = BoolCurve.GetTimes();
	TArrayView<const bool>         Values = BoolCurve.GetValues();

	if (Times.Num() == 0)
	{
		if (BoolCurve.GetDefault().Get(false))
		{
			// Add the whole section range
			OutField->Entities.Populate(EffectiveRange, this, 0);
		}
		return true;
	}

	TRangeBound<FFrameNumber> StartBound = EffectiveRange.GetLowerBound();

	// Find the effective key
	int32 Index = FMath::Min(StartBound.IsOpen() ? 0 : Algo::UpperBound(Times, UE::MovieScene::DiscreteInclusiveLower(StartBound)), Times.Num()-1);

	bool bIsSpawned = Values[StartBound.IsOpen() ? 0 : FMath::Max(0, Algo::UpperBound(Times, UE::MovieScene::DiscreteInclusiveLower(StartBound))-1)];
	for ( ; Index < Times.Num(); ++Index)
	{
		if (!EffectiveRange.Contains(Times[Index]))
		{
			break;
		}

		if (bIsSpawned != Values[Index])
		{
			if (bIsSpawned)
			{
				// Add the last range to the tree
				OutField->Entities.Populate(TRange<FFrameNumber>(StartBound, TRangeBound<FFrameNumber>::Exclusive(Times[Index])), this, 0);
			}

			bIsSpawned = Values[Index];

			if (bIsSpawned)
			{
				StartBound = TRangeBound<FFrameNumber>::Inclusive(Times[Index]);
			}
		}
	}

	TRange<FFrameNumber> TailRange(StartBound, EffectiveRange.GetUpperBound());
	if (!TailRange.IsEmpty() && bIsSpawned)
	{
		OutField->Entities.Populate(TailRange, this, 0);
	}

	return true;
}