// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/Field.h"

class UField;

struct EEnforceInterfacePrefix
{
	enum Type
	{
		None,
		I,
		U
	};
};

class FClasses;

class FClass : public UClass
{
public:
	FClass();

	/** 
	 * Returns the name of the given class with a valid prefix.
	 *
	 * @param InClass Class used to create a valid class name with prefix
	 */
	FString GetNameWithPrefix(EEnforceInterfacePrefix::Type EnforceInterfacePrefix = EEnforceInterfacePrefix::None) const;

	/**
	 * Returns the super class of this class, or NULL if there is no superclass.
	 *
	 * @return The super class of this class.
	 */
	FClass* GetSuperClass() const;

	/**
	 * Returns the 'within' class of this class.
	 *
	 * @return The 'within' class of this class.
	 */
	FClass* GetClassWithin() const;

	TArray<FClass*> GetInterfaceTypes() const;

	void GetHideCategories(TArray<FString>& OutHideCategories) const;
	void GetShowCategories(TArray<FString>& OutShowCategories) const;
	void GetSparseClassDataTypes(TArray<FString>& OutSparseClassDataTypes) const;

	/** Helper function that checks if the field is a dynamic type (can be constructed post-startup) */
	template <typename T>
	static bool IsDynamic(const T* Field)
	{
		static const FName NAME_ReplaceConverted(TEXT("ReplaceConverted"));
		return Field->HasMetaData(NAME_ReplaceConverted);
	}

	/** Helper function that checks if the field is belongs to a dynamic type */
	static bool IsOwnedByDynamicType(const UField* Field);
	static bool IsOwnedByDynamicType(const FField* Field);

	/** Helper function to get the source replaced package name */
	template <typename T>
	static FString GetTypePackageName(const T* Field)
	{
		static const FName NAME_ReplaceConverted(TEXT("ReplaceConverted"));
		FString PackageName = Field->GetMetaData(NAME_ReplaceConverted);
		if (PackageName.Len())
		{
			int32 ObjectDotIndex = INDEX_NONE;
			// Strip the object name
			if (PackageName.FindChar(TEXT('.'), ObjectDotIndex))
			{
				PackageName = PackageName.Mid(0, ObjectDotIndex);
			}
		}
		else
		{
			PackageName = Field->GetOutermost()->GetName();
		}
		return PackageName;
	}
};
