// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Features/IModularFeatures.h"

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
	 * Start capturing rendering information
	 */
	virtual void StartCapturing() = 0;

	/**
	 * Stop capturing rendering information and save the captured data
	 *
	 * @param	DestPath	The destination path to save captured rendering information. If nullptr default will be used. (optional)
	 */
	virtual void StopCapturing(const FString* DestPath = nullptr) = 0;
};