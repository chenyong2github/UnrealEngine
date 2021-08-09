// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "UObject/NameTypes.h"

class ITargetPlatform;
class FCbObject;
class FString;
class UPackage;
struct FIoHash;

namespace UE::TargetDomain
{

bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage);
FCbObject CollectDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage);

bool TryFetchKeyAndDependencies(FName PackageName, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage);
bool TryFetchDependencies(FName PackageName, const ITargetPlatform* TargetPlatform,
	TArray<FName>& OutBuildDependencies, TArray<FName>& OutRuntimeOnlyDependencies);

}
