// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/Scene.h"
#include "Render/Viewport/IDisplayClusterViewport_CustomPostProcessSettings.h"


class FDisplayClusterViewport_CustomPostProcessSettings
	: public IDisplayClusterViewport_CustomPostProcessSettings
{
public:
	FDisplayClusterViewport_CustomPostProcessSettings() = default;
	virtual ~FDisplayClusterViewport_CustomPostProcessSettings() = default;

public:
	virtual void AddCustomPostProcess(const ERenderPass InRenderPass, const FPostProcessSettings& InSettings, float BlendWeight = 1.f, bool bSingleFrame = true) override;
	virtual bool GetCustomPostProcess(const ERenderPass InRenderPass, FPostProcessSettings& OutSettings, float& OutBlendWeight, bool& bOutSingleFrame) const override;
	virtual void RemoveCustomPostProcess(const ERenderPass InRenderPass) override;
	virtual bool DoPostProcess(const ERenderPass InRenderPass, FPostProcessSettings* OutSettings, float* OutBlendWeight = nullptr) override;

	void RemoveAllSingleFramePosprocess();

private:
	struct FPostprocessData
	{
		FPostProcessSettings Settings;
		float BlendWeight;
		bool bSingleFrame;

		FPostprocessData(const FPostProcessSettings& InSettings, float InBlendWeight = 1.f, bool bInSingleFrame = true)
			: Settings(InSettings)
			, BlendWeight(InBlendWeight)
			, bSingleFrame(bInSingleFrame)
		{ }
	};

	// Custom post processing settings
	TMap<ERenderPass, FPostprocessData> PostprocessAsset;
};
