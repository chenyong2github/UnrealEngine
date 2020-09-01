// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "SourceControlHelpers.h"

class UNREALED_API FCommandletPackageHelper
{
public:
	FCommandletPackageHelper();

	void SetSourceControlEnabled(bool bWithSourceControl);
	bool UseSourceControl() const { return SourceControlProvider != nullptr; }
	ISourceControlProvider& GetSourceControlProvider() const { check(UseSourceControl()); return *SourceControlProvider; }

	bool Delete(const FString& PackageName) const;
	bool Delete(UPackage* Package) const;
	bool AddToSourceControl(UPackage* Package) const;
	bool Checkout(UPackage* Package) const;
	bool Save(UPackage* Package) const;

private:
	FScopedSourceControl SourceControl;
	mutable ISourceControlProvider* SourceControlProvider;
};
