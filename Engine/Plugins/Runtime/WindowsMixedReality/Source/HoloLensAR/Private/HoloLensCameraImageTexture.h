// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WindowsMixedRealityAvailability.h"
#include "ARTextures.h"

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	#include "Windows/AllowWindowsPlatformTypes.h"

	THIRD_PARTY_INCLUDES_START
		#include <windows.h>
		#include <D3D11.h>
		#include <d3d11_1.h>
		#include <dxgi1_2.h>
	THIRD_PARTY_INCLUDES_END

	#include "Microsoft/COMPointer.h"
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "HoloLensCameraImageTexture.generated.h"

/**
 * Provides access to the camera's image data as a texture
 */
UCLASS(NotBlueprintType)
class HOLOLENSAR_API UHoloLensCameraImageTexture :
	public UARTextureCameraImage
{
	GENERATED_UCLASS_BODY()

public:
	// UTexture interface implementation
	virtual void BeginDestroy() override;
	virtual FTextureResource* CreateResource() override;
	virtual EMaterialValueType GetMaterialType() const override { return MCT_Texture2D; }
	// End UTexture interface

#if SUPPORTS_WINDOWS_MIXED_REALITY_AR
	/** Forces the reconstruction of the texture data and conversion from Nv12 to RGB */
	virtual void Init(void* handle);

	friend class FHoloLensCameraImageResource;

private:
	/** Used to prevent two updates of the texture in the same game frame */
	uint64 LastUpdateFrame;
#endif
};

