// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/Class.h"


namespace Optimus
{
	/// Attempts to find an object, first within a specific package, if the dot prefix 
	/// points to a known package, otherwise fall back on searching globally.
	template<typename T>
	T* FindObjectInPackageOrGlobal(const FString& InObjectPath)
	{
		UPackage* Package = ANY_PACKAGE;
		FString PackageName;
		FString PackageObjectName = InObjectPath;
		if (InObjectPath.Split(TEXT("."), &PackageName, &PackageObjectName))
		{
			Package = FindPackage(nullptr, *PackageName);
		}

		T* FoundObject = FindObject<T>(Package, *PackageObjectName);

		// If not found in a specific, search everywhere.
		if (FoundObject == nullptr)
		{
			FoundObject = FindObject<T>(ANY_PACKAGE, *InObjectPath);
		}

		return FoundObject;
	}
}
