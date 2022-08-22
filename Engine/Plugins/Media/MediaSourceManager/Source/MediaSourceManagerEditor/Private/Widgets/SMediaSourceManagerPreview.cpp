// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreview.h"

#include "Widgets/SMediaPlayerEditorViewer.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerPreview"

void SMediaSourceManagerPreview::Construct(const FArguments& InArg, UMediaPlayer& InMediaPlayer,
	UMediaTexture* InMediaTexture, const TSharedRef<ISlateStyle>& InStyle)
{
	ChildSlot
		[
			SNew(SMediaPlayerEditorViewer, InMediaPlayer, InMediaTexture, InStyle, false)
				.bShowUrl(false)
		];
}

#undef LOCTEXT_NAMESPACE
