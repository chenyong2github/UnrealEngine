// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

/**
 * Windows security descriptors for IPC
 */
class FTextureShareCoreSecurityAttributes
{
public:
	FTextureShareCoreSecurityAttributes();
	~FTextureShareCoreSecurityAttributes();

public:
	const void* GetResourceSecurityAttributes() const;
	const void* GetEventSecurityAttributes() const;

private:
	TUniquePtr<class FSharedResourceSecurityAttributes> ResourceSecurityAttributes;
	TUniquePtr<class FSharedEventSecurityAttributes>    EventSecurityAttributes;
};
