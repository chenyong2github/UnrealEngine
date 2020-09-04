// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTakeSection.h"
#include "MovieSceneTakeTrack.h"
#include "MovieSceneTakeSettings.h"
#include "Channels/MovieSceneChannelProxy.h"

#define LOCTEXT_NAMESPACE "MovieSceneTakeSection"

#if WITH_EDITOR

struct FTakeSectionEditorData
{
	FTakeSectionEditorData()
	{
		MetaData[0].SortOrder = 0;
		MetaData[0].bCanCollapseToTrack = false;

		MetaData[1].SortOrder = 1;
		MetaData[1].bCanCollapseToTrack = false;

		MetaData[2].SortOrder = 2;
		MetaData[2].bCanCollapseToTrack = false;

		MetaData[3].SortOrder = 3;
		MetaData[3].bCanCollapseToTrack = false;

		MetaData[4].SortOrder = 4;
		MetaData[4].bCanCollapseToTrack = false;

		MetaData[5].SortOrder = 5;
		MetaData[5].bCanCollapseToTrack = false;
	}

	FMovieSceneChannelMetaData      MetaData[6];
	TMovieSceneExternalValue<int32> ExternalValues[4];
	TMovieSceneExternalValue<float> ExternalFloatValues[1];
	TMovieSceneExternalValue<FString> ExternalStringValues[1];
};

#endif	// WITH_EDITOR

UMovieSceneTakeSection::UMovieSceneTakeSection( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	bSupportsInfiniteRange = true;

	ReconstructChannelProxy();
}

#if WITH_EDITORONLY_DATA

void UMovieSceneTakeSection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		ReconstructChannelProxy();
	}
}

#endif

void UMovieSceneTakeSection::PostEditImport()
{
	Super::PostEditImport();

	ReconstructChannelProxy();
}

void UMovieSceneTakeSection::ReconstructChannelProxy()
{
	FMovieSceneChannelProxyData Channels;

#if WITH_EDITOR

	static FTakeSectionEditorData EditorData;

	FText HoursText = GetDefault<UMovieSceneTakeSettings>()->HoursText;
	FText MinutesText = GetDefault<UMovieSceneTakeSettings>()->MinutesText;
	FText SecondsText = GetDefault<UMovieSceneTakeSettings>()->SecondsText;
	FText FramesText = GetDefault<UMovieSceneTakeSettings>()->FramesText;
	FText SubFramesText = GetDefault<UMovieSceneTakeSettings>()->SubFramesText;
	FText SlateText = GetDefault<UMovieSceneTakeSettings>()->SlateText;

	EditorData.MetaData[0].SetIdentifiers(*HoursText.ToString(), HoursText);
	EditorData.MetaData[1].SetIdentifiers(*MinutesText.ToString(), MinutesText);
	EditorData.MetaData[2].SetIdentifiers(*SecondsText.ToString(), SecondsText);
	EditorData.MetaData[3].SetIdentifiers(*FramesText.ToString(), FramesText);
	EditorData.MetaData[4].SetIdentifiers(*SubFramesText.ToString(), SubFramesText);
	EditorData.MetaData[5].SetIdentifiers(*SlateText.ToString(), SlateText);

	Channels.Add(HoursCurve,     EditorData.MetaData[0], EditorData.ExternalValues[0]);
	Channels.Add(MinutesCurve,   EditorData.MetaData[1], EditorData.ExternalValues[1]);
	Channels.Add(SecondsCurve,   EditorData.MetaData[2], EditorData.ExternalValues[2]);
	Channels.Add(FramesCurve,    EditorData.MetaData[3], EditorData.ExternalValues[3]);
	Channels.Add(SubFramesCurve, EditorData.MetaData[4], EditorData.ExternalFloatValues[0]);
	Channels.Add(Slate, EditorData.MetaData[5], EditorData.ExternalStringValues[0]);

#else

	Channels.Add(HoursCurve);
	Channels.Add(MinutesCurve);
	Channels.Add(SecondsCurve);
	Channels.Add(FramesCurve);
	Channels.Add(SubFramesCurve);
	Channels.Add(Slate);

#endif
	
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));
}

#undef LOCTEXT_NAMESPACE