// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

#include "ImgMediaMipMapInfo.h"

class FImgMediaSceneViewExtension final : public FSceneViewExtensionBase
{
public:
	FImgMediaSceneViewExtension(const FAutoRegister& AutoReg);

	/**
	 * Get the cached camera information array, updated on the game thread by BeginRenderViewFamily.
	 *
	 * @return Array of info on each camera.
	 */
	const TArray<FImgMediaMipMapCameraInfo>& GetCameraInfos() const { return CachedCameraInfos; };

	void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

private:
	/** Array of info on each camera used for mipmap calculations. */
	TArray<FImgMediaMipMapCameraInfo> CachedCameraInfos;

	/** Last received FSceneViewFamily frame number. */
	uint32 LastFrameNumber;
};
