// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"


void FDisplayClusterViewport_CustomPostProcessSettings::AddCustomPostProcess(const ERenderPass InRenderPass, const FPostProcessSettings& InSettings, float BlendWeight, bool bSingleFrame)
{
	PostprocessAsset.Emplace(InRenderPass, FPostprocessData(InSettings, BlendWeight, bSingleFrame));
}

bool FDisplayClusterViewport_CustomPostProcessSettings::GetCustomPostProcess(const ERenderPass InRenderPass, FPostProcessSettings& OutSettings, float& OutBlendWeight, bool& bOutSingleFrame) const
{
	const FPostprocessData* ExistSettings = PostprocessAsset.Find(InRenderPass);
	if (ExistSettings)
	{
		OutSettings = ExistSettings->Settings;
		OutBlendWeight = ExistSettings->BlendWeight;
		bOutSingleFrame = ExistSettings->bSingleFrame;
		return true;
	}

	return false;
}

void FDisplayClusterViewport_CustomPostProcessSettings::RemoveCustomPostProcess(const ERenderPass InRenderPass)
{
	if (PostprocessAsset.Contains(InRenderPass))
	{
		PostprocessAsset.Remove(InRenderPass);
	}
}

bool FDisplayClusterViewport_CustomPostProcessSettings::DoPostProcess(const ERenderPass InRenderPass, FPostProcessSettings* OutSettings, float* OutBlendWeight)
{
	const FPostprocessData* ExistSettings = PostprocessAsset.Find(InRenderPass);
	if (ExistSettings)
	{
		if (OutSettings != nullptr)
		{
			*OutSettings = ExistSettings->Settings;
		}

		if (OutBlendWeight != nullptr)
		{
			*OutBlendWeight = ExistSettings->BlendWeight;
		}

		return true;
	}

	return false;
}

void FDisplayClusterViewport_CustomPostProcessSettings::RemoveAllSingleFramePosprocess()
{
	TArray<ERenderPass> SingleFrameKeys;
	for (TPair<ERenderPass, FPostprocessData>& It : PostprocessAsset)
	{
		if (It.Value.bSingleFrame)
		{
			SingleFrameKeys.Add(It.Key);
		}
	}

	// Safe remove items out of iterator
	for (const ERenderPass& keyIt : SingleFrameKeys)
	{
		PostprocessAsset.Remove(keyIt);
	}
}
