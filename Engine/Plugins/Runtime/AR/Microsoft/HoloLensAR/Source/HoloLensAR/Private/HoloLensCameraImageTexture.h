// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityAvailability.h"
#include "ARTextures.h"

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	#include "Windows/AllowWindowsPlatformTypes.h"

	THIRD_PARTY_INCLUDES_START
		#include <windows.h>
		#include <D3D11.h>
	THIRD_PARTY_INCLUDES_END

	#include "Windows/COMPointer.h"
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "HoloLensCameraImageTexture.generated.h"

/**
 * Provides access to the camera's image data as a texture
 */
UCLASS()
class HOLOLENSAR_API UHoloLensCameraImageTexture :
	public UARTextureCameraImage
{
	GENERATED_UCLASS_BODY()

public:
	// UTexture interface implementation
	virtual void BeginDestroy() override;
	virtual FTextureResource* CreateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_Texture2D; }
	virtual float GetSurfaceWidth() const override { return Size.X; }
	virtual float GetSurfaceHeight() const override { return Size.Y; }
	// End UTexture interface

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	/** Forces the reconstruction of the texture data and conversion from Nv12 to RGB */
	virtual void Init(ID3D11Texture2D* InCameraImage);

	friend class FHoloLensCameraImageResource;

private:
	/** Used to prevent two updates of the texture in the same game frame */
	uint64 LastUpdateFrame;
	/** The D3D texture that was passed to us from the HoloLens pass through camera */
	TComPtr<ID3D11Texture2D> CameraImage;
#endif
};

