// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystemTask.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "MovieSceneTimeHelpers.h"



UMovieSceneInterrogationLinker::UMovieSceneInterrogationLinker(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemContext = UE::MovieScene::EEntitySystemContext::Interrogation;
	NextChannel = UE::MovieScene::FInterrogationChannel::First();
}

void UMovieSceneInterrogationLinker::Reset()
{
	using namespace UE::MovieScene;

	NextChannel = FInterrogationChannel::First();

	EntitiesScratch.Reset();
	EntityComponentField = FMovieSceneEntityComponentField();
	ImportedEntities.Reset();

	ChannelToTime.Reset();

	Super::Reset();
}

void UMovieSceneInterrogationLinker::ImportTrack(UMovieSceneTrack* Track)
{
	using namespace UE::MovieScene;

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &EntityManager);

	FFrameRate TickResolution = Track->GetTypedOuter<UMovieScene>()->GetTickResolution();

	const FMovieSceneTrackEvaluationField& EvaluationField = Track->GetEvaluationField();

	FMovieSceneEntityComponentFieldBuilder FieldBuilder(&EntityComponentField);
	FieldBuilder.GetSharedMetaData().ObjectBindingID = Track->FindObjectBindingGuid();

	for (const FMovieSceneTrackEvaluationFieldEntry& Entry : EvaluationField.Entries)
	{
		IMovieSceneEntityProvider* EntityProvider = Cast<IMovieSceneEntityProvider>(Entry.Section);

		if (!EntityProvider || Entry.Range.IsEmpty())
		{
			continue;
		}

		FMovieSceneEvaluationFieldEntityMetaData MetaData;

		MetaData.ForcedTime = Entry.ForcedTime;
		MetaData.Flags      = Entry.Flags;
		MetaData.bEvaluateInSequencePreRoll  = Track->EvalOptions.bEvaluateInPreroll;
		MetaData.bEvaluateInSequencePostRoll = Track->EvalOptions.bEvaluateInPostroll;

		if (!EntityProvider->PopulateEvaluationField(Entry.Range, MetaData, &FieldBuilder))
		{
			const int32 EntityIndex   = FieldBuilder.FindOrAddEntity(Entry.Section, 0);
			const int32 MetaDataIndex = FieldBuilder.AddMetaData(MetaData);
			FieldBuilder.AddPersistentEntity(Entry.Range, EntityIndex, MetaDataIndex);
		}
	}
}

UE::MovieScene::FInterrogationChannel UMovieSceneInterrogationLinker::AddInterrogation(FFrameTime Time)
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
	EntityComponentField.QueryPersistentEntities(Time.FrameNumber, UnusedEntityRange, EntitiesScratch);

	FEntityImportSequenceParams Params;
	Params.InterrogationChannel = Channel;

	for (const FMovieSceneEvaluationFieldEntityQuery& Query : EntitiesScratch)
	{
		InterrogateEntity(Params, Channel, Query);
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

void UMovieSceneInterrogationLinker::InterrogateEntity(const UE::MovieScene::FEntityImportSequenceParams& ImportParams, UE::MovieScene::FInterrogationChannel InterrogationChannel, const FMovieSceneEvaluationFieldEntityQuery& Query)
{
	using namespace UE::MovieScene;

	UObject* EntityOwner = Query.Entity.Key.EntityOwner.Get();
	IMovieSceneEntityProvider* Provider = Cast<IMovieSceneEntityProvider>(EntityOwner);
	if (!Provider)
	{
		return;
	}

	FEntityImportParams Params;
	Params.Sequence = ImportParams;
	Params.EntityID = Query.Entity.Key.EntityID;
	Params.EntityMetaData = EntityComponentField.FindMetaData(Query);
	Params.SharedMetaData = EntityComponentField.FindSharedMetaData(Query);

	FImportedEntity ImportedEntity;
	Provider->InterrogateEntity(this, Params, &ImportedEntity);

	if (!ImportedEntity.IsEmpty())
	{
		if (UMovieSceneSection* Section = Cast<UMovieSceneSection>(EntityOwner))
		{
			Section->BuildDefaultComponents(this, Params, &ImportedEntity);
		}

		const FMovieSceneEntityID NewEntityID = ImportedEntity.Manufacture(Params, &EntityManager);

		const FImportedEntityKey NewEntityKey { InterrogationChannel, Query.Entity.Key };

		ImportedEntities.Add(NewEntityKey, NewEntityID);
	}
}

void UMovieSceneInterrogationLinker::Update()
{
	using namespace UE::MovieScene;

	TGuardValue<FEntityManager*> DebugVizGuard(GEntityManagerForDebuggingVisualizers, &EntityManager);

	EntityManager.AddMutualComponents();
	LinkRelevantSystems();

	FEntityTaskBuilder()
	.Read(FBuiltInComponentTypes::Get()->Interrogation.InputChannel)
	.Write(FBuiltInComponentTypes::Get()->EvalTime)
	.FilterNone({ FBuiltInComponentTypes::Get()->Tags.FixedTime })
	.Iterate_PerEntity(&EntityManager, [this](FInterrogationChannel Channel, FFrameTime& OutEvalTime) { OutEvalTime = this->ChannelToTime.FindChecked(Channel); });

	FMovieSceneEntitySystemRunner Runner;
	Runner.AttachToLinker(this);
	Runner.Flush();

	EntityManager.IncrementSystemSerial();
}

UE::MovieScene::FMovieSceneEntityID UMovieSceneInterrogationLinker::FindEntityFromOwner(UE::MovieScene::FInterrogationChannel InterrogationChannel, UObject* Owner, uint32 EntityID) const
{
	using namespace UE::MovieScene;

	FImportedEntityKey Key { InterrogationChannel, FMovieSceneEvaluationFieldEntityKey { Owner, EntityID } };
	return ImportedEntities.FindRef(Key);
}

UE::MovieScene::FMovieSceneEntityID UMovieSceneInterrogationLinker::FindEntityFromOwner(FFrameTime InterrogationTime, UObject* Owner, uint32 EntityID) const
{
	using namespace UE::MovieScene;

	if (const FInterrogationChannel* InterrogationChannel = ChannelToTime.FindKey(InterrogationTime))
	{
		return FindEntityFromOwner(*InterrogationChannel, Owner, EntityID);
	}
	return FMovieSceneEntityID::Invalid();
}

