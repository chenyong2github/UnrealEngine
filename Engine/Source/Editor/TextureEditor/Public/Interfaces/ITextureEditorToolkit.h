// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneTypes.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "TextureEditorSettings.h"

class UTexture;

/**
 * Interface for texture editor tool kits.
 */
class ITextureEditorToolkit
	: public FAssetEditorToolkit
{
public:

	/** Returns the Texture asset being inspected by the Texture editor */
	virtual UTexture* GetTexture() const = 0;

	/** Returns if the Texture asset being inspected has a valid texture resource */
	virtual bool HasValidTextureResource() const = 0;

	/** Refreshes the quick info panel */
	virtual void PopulateQuickInfo() = 0;

	/** Calculates the display size of the texture */
	virtual void CalculateTextureDimensions(uint32& Width, uint32& Height) const = 0;

	/** Accessors */ 
	virtual int32 GetMipLevel() const = 0;
	virtual int32 GetLayer() const = 0;
	virtual ESimpleElementBlendMode GetColourChannelBlendMode() const = 0;
	virtual bool GetUseSpecifiedMip() const = 0;
	virtual double GetCustomZoomLevel() const = 0;
	virtual void SetCustomZoomLevel( double ZoomValue ) = 0;
	virtual void ZoomIn() = 0;
	virtual void ZoomOut() = 0;
	virtual ETextureEditorZoomMode GetZoomMode() const = 0;
	virtual void SetZoomMode( const ETextureEditorZoomMode ZoomMode ) = 0;
	virtual double CalculateDisplayedZoomLevel() const = 0;
	virtual float GetVolumeOpacity( ) const = 0;
	virtual void SetVolumeOpacity( float VolumeOpacity ) = 0;
	virtual const FRotator& GetVolumeOrientation( ) const = 0;
	virtual void SetVolumeOrientation( const FRotator& InOrientation ) = 0;

	UE_DEPRECATED(4.26, "There are now more than 2 zoom modes, use GetZoomMode() instead.")
	virtual bool GetFitToViewport() const final
	{
		return GetZoomMode() != ETextureEditorZoomMode::Custom;
	}
	
	UE_DEPRECATED(4.26, "There are now more than 2 zoom modes, use SetZoomMode() instead.")
	virtual void SetFitToViewport(const bool bFitToViewport) final
	{
		SetZoomMode(bFitToViewport ? ETextureEditorZoomMode::Fit : ETextureEditorZoomMode::Custom);
	}

	UE_DEPRECATED(4.26, "GetZoom Has been renamed to GetCustomZoomLevel to clarify it refers to the custom zoom mode's value.")
	virtual double GetZoom() const final
	{
		return GetCustomZoomLevel();
	}

	UE_DEPRECATED(4.26, "SetZoom has been renamed to SetCustomZoomLevel to clarify it refers to the custom zoom mode's value.")
	virtual void SetZoom(double ZoomValue) final
	{
		SetCustomZoomLevel(ZoomValue);
	}

public:

	/**
	 * Toggles the fit-to-viewport mode. If already on, will return to the last custom zoom level.
	 */
	UE_DEPRECATED(4.26, "There are now commands for switching to individual zoom modes rather than toggling. Please use SetZoomMode() instead.")
	void ToggleFitToViewport()
	{
		if (IsCurrentZoomMode(ETextureEditorZoomMode::Fit))
		{
			SetZoomMode(ETextureEditorZoomMode::Custom);
		}
		else
		{
			SetZoomMode(ETextureEditorZoomMode::Fit);
		}
	}

	/** 
	* Returns true if this is the current zoom mode. Useful for Slate bindings.
	*/
	bool IsCurrentZoomMode(ETextureEditorZoomMode ZoomMode) const
	{ 
		return GetZoomMode() == ZoomMode; 
	}
};
