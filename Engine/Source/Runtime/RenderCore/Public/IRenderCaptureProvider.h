// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeatures.h"

class FRHICommandListImmediate;
class FViewport;

class IRenderCaptureProvider : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("RenderCaptureProvider"));
		return FeatureName;
	}

	/**
	 * Checks to see if the specified feature is available
	 * 
	 * @return	True if the feature is available right now and it is safe to call Get()
	 */
	static inline bool IsAvailable()
	{
		return IModularFeatures::Get().IsModularFeatureAvailable(GetModularFeatureName());
	}

	/**
	 * Gets the first registered implementation of this feature. Will assert or crash 
	 * if the specified feature is not available!  You should call IsAvailable() first!
	 *
	 * @return	The feature.
	 */
	static inline IRenderCaptureProvider& Get()
	{
		return IModularFeatures::Get().GetModularFeature<IRenderCaptureProvider>(GetModularFeatureName());
	}

	/**
	 * Capture the next full frame of rendering information.
	 * Currently the capture details (number of frames etc.) can only be set up by CVars exposed by the underlying capture tool.
	 * Call from main thread only.
	 */
	virtual void CaptureFrame(FViewport* Viewport = nullptr, FString const& DestPath = FString(), bool bLaunch = true) = 0;

	/**
	 * Start capturing rendering information.
	 * Call from render thread only.
	 * 
	 * @param	RHICommandList	The command list to capture on.
	 * @param	ScopeName		Name for capture scope. (Optional).
	 */
	virtual void BeginCapture(FRHICommandListImmediate* RHICommandList, FString const& ScopeName = FString()) = 0;
	
	/**
	 * Stop capturing rendering information and save the captured data.
	 * Call from render thread only.
	 *
	 * @param	RHICommandList	The command list to capture on.
	 * @param	DestPath		The destination path for saving the capture. (Optional).
	 */
	virtual void EndCapture(FRHICommandListImmediate* RHICommandList, FString const& DestPath = FString(), bool bLaunch = true) = 0;
};
