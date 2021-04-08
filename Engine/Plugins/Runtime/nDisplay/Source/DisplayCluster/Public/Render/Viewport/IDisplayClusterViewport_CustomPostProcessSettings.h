// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class DISPLAYCLUSTER_API IDisplayClusterViewport_CustomPostProcessSettings
{
public:
	enum class ERenderPass : int
	{
		Start,
		Override,
		Final
	};

public:
	virtual void AddCustomPostProcess(const ERenderPass InRenderPass, const struct FPostProcessSettings& InSettings,  float  BlendWeight = 1.f,    bool  bSingleFrame = true) = 0;
	virtual bool GetCustomPostProcess(const ERenderPass InRenderPass,       struct FPostProcessSettings& OutSettings, float& OutBlendWeight,       bool& bOutSingleFrame) const = 0;
	virtual void RemoveCustomPostProcess(const ERenderPass InRenderPass) = 0;

	// Override posproces, if defined
	// * return true, if processed
	virtual bool DoPostProcess(const ERenderPass InRenderPass, struct FPostProcessSettings* OutSettings, float* OutBlendWeight = nullptr) = 0;
};
