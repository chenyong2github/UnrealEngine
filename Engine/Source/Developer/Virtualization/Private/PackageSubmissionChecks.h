// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

namespace UE::Virtualization
{

/** @See IVirtualizationSystem::TryVirtualizePackages */
void VirtualizePackages(const TArray<FString>& FilesToSubmit, TArray<FText>& OutDescriptionTags, TArray<FText>& OutErrors);

} // namespace UE::Virtualization
