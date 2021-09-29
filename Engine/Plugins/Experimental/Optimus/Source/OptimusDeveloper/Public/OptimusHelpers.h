// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
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

	/** Given an object hierarchy scope, and object class, ensure that the given name is
	    unique within those parameters. If the name is already unique, it will be returned
		unchanged. */
	FName GetUniqueNameForScopeAndClass(UObject *InScopeObj, UClass *InClass, FName InName);

	/** A small helper class to enable binary reads on an archive, since the 
		FObjectReader::Serialize(TArray<uint8>& InBytes) constructor is protected */
	class FBinaryObjectReader : public FObjectReader
	{
	public:
		FBinaryObjectReader(UObject* Obj, const TArray<uint8>& InBytes)
			// FIXME: The constructor is broken. It only needs a const ref.
		    : FObjectReader(const_cast<TArray<uint8>&>(InBytes))
		{
			this->SetWantBinaryPropertySerialization(true);
			Obj->Serialize(*this);
		}
	};

	class FBinaryObjectWriter : public FObjectWriter
	{
	public:
		FBinaryObjectWriter(UObject* Obj, TArray<uint8>& OutBytes)
		    : FObjectWriter(OutBytes)
		{
			this->SetWantBinaryPropertySerialization(true);
			Obj->Serialize(*this);
		}
	};

}
