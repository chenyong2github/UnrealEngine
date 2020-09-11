// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "SourceControlHelpers.h"
#include "AssetRegistryModule.h"

class UNREALED_API FCommandletPackageHelper
{
public:
	FCommandletPackageHelper();
	~FCommandletPackageHelper();

	bool UseSourceControl() const;

	bool Delete(const FString& PackageName) const;
	bool Delete(UPackage* Package) const;
	bool Delete(const TArray<UPackage*>& Packages) const;
	bool Delete(const TArray<FAssetData>& Assets) const;
	bool AddToSourceControl(UPackage* Package) const;
	bool Checkout(UPackage* Package) const;
	bool Save(UPackage* Package) const;

private:
	ISourceControlProvider& GetSourceControlProvider() const;

	bool DeleteInternal(const FString& PackageName) const;

	FScopedSourceControl SourceControl;	
};
