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
	/** Stores the current value of the MediaPath property. */
	FString MediaPath;

	/**
	 * Gets the object path for the media source object.
	 */
	FString GetMediaSourcePath() const;

	/**
	 * Called when the media source widget changes.
	 */
	void OnMediaSourceChanged(const FAssetData& AssetData);

	/**
	 * Updates MediaPath from the current MediaSource.
	 */
	void UpdateMediaPath();

	/**
	 * Called to get the media path for the file picker.
	 */
	FString HandleMediaPath() const;
	
	/**
	 * Called when we select a media path.
	 */
	void HandleMediaPathPicked(const FString& PickedPath);

	/**
	 * Called when the open media plate button is pressed.
	 */
	FReply OnOpenMediaPlate();

	/**
	 * Call this to stop all playback.
	 */
	void StopMediaPlates();

	/**
	 * Get the rate to use when we press the forward button.
	 */
	float GetForwardRate(UMediaPlayer* MediaPlayer) const;

	/**
	 * Get the rate to use when we press the reverse button.
	 */
	float GetReverseRate(UMediaPlayer* MediaPlayer) const;
};

