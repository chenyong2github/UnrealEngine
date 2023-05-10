// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/CulturePointer.h"
#include "UObject/WeakObjectPtr.h"

struct FLocalizationContext
{
	FLocalizationContext() = default;
	explicit FLocalizationContext(UObject* InWorldContext, const FCultureRef& InCulture) : WorldContext(InWorldContext), Culture(InCulture) {}

	TWeakObjectPtr<UObject> WorldContext = nullptr;
	FCultureRef Culture;
};

