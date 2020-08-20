// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/LinkerInstancingContext.h"
#include "Misc/PackageName.h"

FName FLinkerInstancingContext::GenerateInstancedName(FName PackageLoadName, FName DependantPackageName)
{
	FName DependantPackageShortName = FPackageName::GetShortFName(DependantPackageName);
	return FName(*FString::Printf(TEXT("%s_InstanceOf_%s"), *PackageLoadName.ToString(), *DependantPackageShortName.ToString()));
}
