// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class UMediaPlateComponent;
class UMediaPlayer;

/**
 * Implements a details view customization for the UMediaPlateComponent class.
 */
class FMediaPlateCustomization
	: public IDetailCustomization
{
public:

	//~ IDetailCustomization interface

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	/**
	 * Creates an instance of this class.
	 *
	 * @return The new instance.
	 */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FMediaPlateCustomization());
	}

private:

	/** List of the media plates we are editing. */
	TArray<TWeakObjectPtr<UMediaPlateComponent>> MediaPlatesList;

	/**
	 * Called when the open media plate button is pressed.
	 */
	FReply OnOpenMediaPlate();

	/**
	 * Called when bUseMediaSource changes.
	 */
	void OnUseMediaSourceChanged(IDetailLayoutBuilder* DetailBuilder);

	/**
	 * Get the rate to use when we press the forward button.
	 */
	float GetForwardRate(UMediaPlayer* MediaPlayer) const;

	/**
	 * Get the rate to use when we press the reverse button.
	 */
	float GetReverseRate(UMediaPlayer* MediaPlayer) const;
};

