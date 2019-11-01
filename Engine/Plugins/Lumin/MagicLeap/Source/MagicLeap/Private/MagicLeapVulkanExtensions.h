// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "IHeadMountedDisplayVulkanExtensions.h"

class FMagicLeapVulkanExtensions : public IHeadMountedDisplayVulkanExtensions, public TSharedFromThis<FMagicLeapVulkanExtensions, ESPMode::ThreadSafe>
{
public:
	FMagicLeapVulkanExtensions();
	virtual ~FMagicLeapVulkanExtensions();

	// IHeadMountedDisplayVulkanExtensions
	virtual bool GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out) override;
	virtual bool GetVulkanDeviceExtensionsRequired(struct VkPhysicalDevice_T *pPhysicalDevice, TArray<const ANSICHAR*>& Out) override;

private:
	struct Implementation;
	TUniquePtr<Implementation> ImpPtr;
};
