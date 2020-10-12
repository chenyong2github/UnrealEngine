// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDisplayClusterRendering.h"
#include "StereoRendering.h"

class IDisplayClusterProjectionPolicy;
class FDisplayClusterRenderingViewExtension;
class FSceneViewFamily;
struct FSceneViewInitOptions;

/** View parameters */
struct FDisplayClusterRenderingViewParameters
{
	FVector ViewLocation;
	FMatrix ViewRotationMatrix;
	FMatrix ProjectionMatrix;
	FIntRect ViewRect;
	EStereoscopicPass StereoPass;
	float StereoIPD;
};


class FDisplayClusterRenderingManager
	: public IDisplayClusterRendering
{
protected:
	FDisplayClusterRenderingManager()
	{ }

public:
	static FDisplayClusterRenderingManager& Get()
	{
		static FDisplayClusterRenderingManager Instance;
		return Instance;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void RenderSceneToTexture(const FDisplayClusterRenderingParameters& RenderParams) override;

protected:
	void SetupViewFamily(
		FSceneViewFamily& ViewFamily,
		const FDisplayClusterRenderingParameters& PreviewRenderInfo,
		const TArrayView<FDisplayClusterRenderingViewParameters> Views,
		float MaxViewDistance,
		bool bCaptureSceneColor,
		bool bIsPlanarReflection,
		FPostProcessSettings* PostProcessSettings,
		float PostProcessBlendWeight,
		const AActor* ViewActor);

	void SetupViewVisibility(const FDisplayClusterRenderingParameters& PreviewRenderInfo, FSceneViewInitOptions& ViewInitOptions);

protected:
	TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe> AddViewExtension(FTextureRenderTargetResource* RenderTarget, IDisplayClusterProjectionPolicy* ProjectionPolicy);
	bool RemoveViewExtension(FTextureRenderTargetResource* RenderTarget);

private:
	TArray<TSharedPtr<FDisplayClusterRenderingViewExtension, ESPMode::ThreadSafe>> DisplayExtensions;
};
