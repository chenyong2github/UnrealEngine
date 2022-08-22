// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISlateStyle;
class UMediaPlayer;
class UMediaTexture;

/**
 * Implements a preview for a single media source manager channel.
 */
class SMediaSourceManagerPreview
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerPreview) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InMediaPlayer				The UMediaPlayer asset to show the details for.
	 * @param InMediaTexture			The UMediaTexture asset to output video to. If nullptr then use our own.
	 * @param InStyle					The style set to use.
	 */
	void Construct(const FArguments& InArgs, UMediaPlayer& InMediaPlayer,
		UMediaTexture* InMediaTexture, const TSharedRef<ISlateStyle>& InStyle);

};
