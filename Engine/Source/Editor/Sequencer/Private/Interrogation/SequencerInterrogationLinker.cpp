// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interrogation/SequencerInterrogationLinker.h"
#include "Interrogation/SequencerInterrogatedPropertyInstantiator.h"

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "MovieSceneTimeHelpers.h"



USequencerInterrogationLinker::USequencerInterrogationLinker(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemContext = UE::MovieScene::EEntitySystemContext::Interrogation;
	NextChannel = UE::MovieScene::FInterrogationChannel::First();
}

void USequencerInterrogationLinker::Reset()
{
	using namespace UE::MovieScene;

	NextChannel = FInterrogationChannel::First();

	EntitiesScratch.Reset();
	EntityComponentField = FMovieSceneEntityComponentField();

	ChannelToTime.Reset();

	Super::Reset();
}

void USequencerInterrogationLinker::ImportTrack(UMovieSceneTrack* Track)
{
	using namespace UE::MovieScene;

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &EntityManager);

	FFrameRate TickResolution = Track->GetTypedOuter<UMovieScene>()->GetTickResolution();

	const FMovieSceneTrackEvaluationField& EvaluationField = Track->GetEvaluationField();

	for (const FMovieSceneTrackEvaluationFieldEntry& Entry : EvaluationField.Entries)
	{
		IMovieSceneEntityProvider* EntityProvider = Cast<IMovieSceneEntityProvider>(Entry.Section);

		if (!EntityProvider || Entry.Range.IsEmpty())
		{
			continue;
		}

		if (!EntityProvider->PopulateEvaluationField(Entry.Range, &EntityComponentField))
		{
			EntityComponentField.Entities.Populate(Entry.Range, Entry.Section, 0);
		}
	}
}

UE::MovieScene::FInterrogationChannel USequencerInterrogationLinker::AddInterrogation(FFrameTime Time)
{
	using namespace UE::MovieScene;

	if (!ensureMsgf(NextChannel, TEXT("Reached the maximum available number of interrogation channels")))
	{
		return NextChannel;
	}

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &EntityManager);

	FInterrogationChannel Channel = NextChannel;

	TRange<FFrameNumber> UnusedEntityRange;

	// Update the entities that exist at this frame
	EntitiesScratch.Reset();
	EntityComponentField.Entities.ExtractAtTime(Time.FrameNumber, UnusedEntityRange, EntitiesScratch);

	FEntityImportParams Params;
	Params.Sequence.InterrogationChannel = Channel;

	for (const FMovieSceneEvaluationFieldEntityPtr& Entity : EntitiesScratch)
	{
		UObject* EntityOwner = Entity.EntityOwner.Get();
		IMovieSceneEntityProvider* Provider = Cast<IMovieSceneEntityProvider>(EntityOwner);
		if (!Provider)
		{
			continue;
		}

		Params.EntityID = Entity.EntityID;
		Params.ObjectBindingID = EntityComponentField.EntityOwnerToObjectBinding.FindRef(EntityOwner);

		FImportedEntity ImportedEntity;
		Provider->InterrogateEntity(this, Params, &ImportedEntity);

		if (!ImportedEntity.IsEmpty())
		{
			if (UMovieSceneSection* Section = Cast<UMovieSceneSection>(EntityOwner))
			{
				Section->BuildDefaultComponents(this, Params, &ImportedEntity);
			}

			ImportedEntity.Manufacture(Params, &EntityManager);
		}
	}

	if (Channel == FInterrogationChannel::Last())
	{
		NextChannel = FInterrogationChannel();
	}
	else
	{
		++NextChannel;
	}

	ChannelToTime.Add(Channel, Time);

	return Channel;
}

void USequencerInterrogationLinker::Update()
{
	using namespace UE::MovieScene;

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &EntityManager);

	EntityManager.AddMutualComponents();
	LinkRelevantSystems();

	FEntityTaskBuilder()
	.Read(FBuiltInComponentTypes::Get()->Interrogation.InputChannel)
	.Write(FBuiltInComponentTypes::Get()->EvalTime)
	.Iterate_PerEntity(&EntityManager, [this](FInterrogationChannel Channel, FFrameTime& OutEvalTime) { OutEvalTime = this->ChannelToTime.FindChecked(Channel); });

	FMovieSceneEntitySystemRunner Runner;
	Runner.AttachToLinker(this);
	Runner.Flush();

	EntityManager.IncrementSystemSerial();
}
