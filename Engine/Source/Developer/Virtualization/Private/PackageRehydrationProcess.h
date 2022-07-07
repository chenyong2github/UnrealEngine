// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

namespace UE::Virtualization
{

void RehydratePackages(const TArray<FString>& Packages, TArray<FText>& OutErrors);

} // namespace UE::Virtualization
