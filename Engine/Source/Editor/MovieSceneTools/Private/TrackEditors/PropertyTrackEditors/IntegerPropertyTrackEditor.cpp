// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/PropertyTrackEditors/IntegerPropertyTrackEditor.h"


TSharedRef<ISequencerTrackEditor> FIntegerPropertyTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable(new FIntegerPropertyTrackEditor(OwningSequencer));
}


void FIntegerPropertyTrackEditor::GenerateKeysFromPropertyChanged( const FPropertyChangedParams& PropertyChangedParams, UMovieSceneSection* SectionToKey, FGeneratedTrackKeys& OutGeneratedKeys )
{
	const int32 KeyedValue = PropertyChangedParams.GetPropertyValue<int32>();
	OutGeneratedKeys.Add(FMovieSceneChannelValueSetter::Create<FMovieSceneIntegerChannel>(0, KeyedValue, true));
}

bool FIntegerPropertyTrackEditor::ModifyGeneratedKeysByCurrentAndWeight(UObject *Object, UMovieSceneTrack *Track, UMovieSceneSection* SectionToKey, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedTotalKeys, float Weight) const
{
	IMovieSceneTrackTemplateProducer* TrackTemplateProducer = Cast<IMovieSceneTrackTemplateProducer>(Track);
	if (!TrackTemplateProducer)
	{
		return false;
	}

	FFrameRate TickResolution = GetSequencer()->GetFocusedTickResolution();
	FMovieSceneEvaluationTrack EvalTrack = TrackTemplateProducer->GenerateTrackTemplate(Track);
	FMovieSceneInterrogationData InterrogationData;
	GetSequencer()->GetEvaluationTemplate().CopyActuators(InterrogationData.GetAccumulator());

	FMovieSceneContext Context(FMovieSceneEvaluationRange(KeyTime, GetSequencer()->GetFocusedTickResolution()));
	EvalTrack.Interrogate(Context, InterrogationData, Object);

	int32 CurValue = 0;
	for (const int32 &Value : InterrogationData.Iterate<int32>(FMovieScenePropertySectionTemplate::GetInt32InterrogationKey()))
	{
		CurValue = Value;
		break;
	}

	FMovieSceneChannelProxy& Proxy = SectionToKey->GetChannelProxy();
	GeneratedTotalKeys[0]->ModifyByCurrentAndWeight(Proxy, KeyTime, (void *)&CurValue, Weight);
	return true;
	
}