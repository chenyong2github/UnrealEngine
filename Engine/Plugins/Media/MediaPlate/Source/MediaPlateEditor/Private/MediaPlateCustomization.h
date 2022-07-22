// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "MediaPlateCustomizationMesh.h"
#include "MediaTextureTracker.h"
#include "Styling/SlateTypes.h"

class IDetailCategoryBuilder;
class IDetailLayoutBuilder;
class SWidget;
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

	/** Whether we have a plane, sphere, etc. */
	EMediaTextureVisibleMipsTiles MeshMode;

	/** Handles mesh stuff. */
	FMediaPlateCustomizationMesh MeshCustomization;

	/**
	 * Adds widgets for editing the mesh.
	 */
	void AddMeshCustomization(IDetailCategoryBuilder& MediaPlateCategory);

	/**
	 * Controls visibility for widgets for plane meshes.
	 */
	EVisibility ShouldShowMeshPlaneWidgets() const;

	/**
	 * Controls visibility for widgets for sphere meshes.
	 */
	EVisibility ShouldShowMeshSphereWidgets() const;

	/**
	 * Call this to switch between planes, spheres, etc.
	 */
	void SetMeshMode(EMediaTextureVisibleMipsTiles InMode);

	/**
	 * Call this to apply a sphere mesh to an object.
	 */
	void SetSphereMesh(UMediaPlateComponent* MediaPlate);

	/**
	 * Returns menu options for all aspect ratio presets.
	 */
	TSharedRef<SWidget> OnGetAspectRatios();

	/**
	 * Call this to see if auto aspect ratio is enabled.
	 */
	ECheckBoxState IsAspectRatioAuto() const;

	/**
	 * Call this to enable/disable automatic aspect ratio.
	 */
	void SetIsAspectRatioAuto(ECheckBoxState State);

	/**
	 * Call this to set the aspect ratio.
	 */
	void SetAspectRatio(float AspectRatio);

	/**
	 * Call this to get the aspect ratio.
	 */
	TOptional<float> GetAspectRatio() const;

	/**
	 * Call this to set the horizontal range of the mesh.
	 */
	void SetMeshHorizontalRange(float HorizontalRange);

	/**
	 * Call this to get the horizontal range of the mesh.
	 */
	TOptional<float> GetMeshHorizontalRange() const;

	/**
	 * Call this to set the vertical range of the mesh.
	 */
	void SetMeshVerticalRange(float VerticalRange);

	/**
	 * Call this to get the vertical range of the mesh.
	 */
	TOptional<float> GetMeshVerticalRange() const;

	/**
	 * Call this to set the range of the mesh.
	 */
	void SetMeshRange(FVector2D Range);

	/**
	 * Gets the object path for the media source object.
	 */
	FString GetMediaSourcePath() const;

	/**
	 * Called when the playlist changes.
	 */
	void OnPlaylistChanged();

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

