// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

struct FKeyHandle;
struct FTimeToPixel;
struct FMovieSceneFloatChannel;
class UMovieSceneSection;

class MOVIESCENETOOLS_API MovieSceneSectionHelpers
{
public:

	/** Consolidate color curves for all track sections. */
	static void ConsolidateColorCurves(TArray< TTuple<float, FLinearColor> >& OutColorKeys, const FLinearColor& DefaultColor, const TArray<FMovieSceneFloatChannel*>& ColorChannels, const FTimeToPixel& TimeConverter);
};

class MOVIESCENETOOLS_API FMovieSceneKeyColorPicker
{
public:
	FMovieSceneKeyColorPicker(UMovieSceneSection* Section, FMovieSceneFloatChannel* RChannel, FMovieSceneFloatChannel* GChannel, FMovieSceneFloatChannel* BChannel, FMovieSceneFloatChannel* AChannel, const TArray<FKeyHandle>& KeyHandles);

private:
		
	void OnColorPickerPicked(FLinearColor NewFolderColor, FMovieSceneFloatChannel* RChannel, FMovieSceneFloatChannel* GChannel, FMovieSceneFloatChannel* BChannel, FMovieSceneFloatChannel* AChannel);
	void OnColorPickerClosed(const TSharedRef<SWindow>& Window, UMovieSceneSection* Section, FMovieSceneFloatChannel* RChannel, FMovieSceneFloatChannel* GChannel, FMovieSceneFloatChannel* BChannel, FMovieSceneFloatChannel* AChannel);
	void OnColorPickerCancelled(FLinearColor NewFolderColor, FMovieSceneFloatChannel* RChannel, FMovieSceneFloatChannel* GChannel, FMovieSceneFloatChannel* BChannel, FMovieSceneFloatChannel* AChannel);

private:

	static FFrameNumber KeyTime;
	static FLinearColor InitialColor;
	static bool bColorPickerWasCancelled;
};