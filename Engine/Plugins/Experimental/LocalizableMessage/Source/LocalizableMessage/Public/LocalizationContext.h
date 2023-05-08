// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

struct FLocalizationContext
{
	FLocalizationContext() = default;
	explicit FLocalizationContext(UObject* InWorldContext) : WorldContext(InWorldContext) {}

	TWeakObjectPtr<UObject> WorldContext = nullptr;
};

