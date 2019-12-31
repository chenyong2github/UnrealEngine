// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParserClass.h"
#include "UnrealHeaderTool.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"

namespace
{
	static const FName NAME_HideCategories(TEXT("HideCategories"));
	static const FName NAME_ShowCategories(TEXT("ShowCategories"));
	static const FName NAME_SparseClassDataTypes(TEXT("SparseClassDataTypes"));
}
const FName FClass::NAME_ReplaceConverted(TEXT("ReplaceConverted"));

FString FClass::GetNameWithPrefix(EEnforceInterfacePrefix::Type EnforceInterfacePrefix) const
{
	const TCHAR* Prefix = 0;

	if (HasAnyClassFlags(CLASS_Interface))
	{
		// Grab the expected prefix for interfaces (U on the first one, I on the second one)
		switch (EnforceInterfacePrefix)
		{
			case EEnforceInterfacePrefix::None:
				// For old-style files: "I" for interfaces, unless it's the actual "Interface" class, which gets "U"
				if (GetFName() == NAME_Interface)
				{
					Prefix = TEXT("U");
				}
				else
				{
					Prefix = TEXT("I");
				}
				break;

			case EEnforceInterfacePrefix::I:
				Prefix = TEXT("I");
				break;

			case EEnforceInterfacePrefix::U:
				Prefix = TEXT("U");
				break;

			default:
				check(false);
		}
	}
	else
	{
		// Get the expected class name with prefix
		Prefix = GetPrefixCPP();
	}

	return FString::Printf(TEXT("%s%s"), Prefix, *GetName());
}

FClass* FClass::GetSuperClass() const
{
	return static_cast<FClass*>(static_cast<const UClass*>(this)->GetSuperClass());
}

FClass* FClass::GetClassWithin() const
{
	return (FClass*)ClassWithin;
}

TArray<FClass*> FClass::GetInterfaceTypes() const
{
	TArray<FClass*> Result;
	for (const FImplementedInterface& i : Interfaces)
	{
		Result.Add((FClass*)i.Class);
	}
	return Result;
}

void FClass::GetHideCategories(TArray<FString>& OutHideCategories) const
{
	if (HasMetaData(NAME_HideCategories))
	{
		const FString& HideCategories = GetMetaData(NAME_HideCategories);
		HideCategories.ParseIntoArray(OutHideCategories, TEXT(" "), true);
	}
}

void FClass::GetShowCategories(TArray<FString>& OutShowCategories) const
{
	if (HasMetaData(NAME_ShowCategories))
	{
		const FString& ShowCategories = GetMetaData(NAME_ShowCategories);
		ShowCategories.ParseIntoArray(OutShowCategories, TEXT(" "), true);
	}
}

void FClass::GetSparseClassDataTypes(TArray<FString>& OutSparseClassDataTypes) const
{
	if (HasMetaData(NAME_SparseClassDataTypes))
	{
		const FString& SparseClassDataTypes = GetMetaData(NAME_SparseClassDataTypes);
		SparseClassDataTypes.ParseIntoArray(OutSparseClassDataTypes, TEXT(" "), true);
	}
}

bool FClass::IsOwnedByDynamicType(const UField* Field)
{
	for (const UField* OuterField = Cast<const UField>(Field->GetOuter()); OuterField; OuterField = Cast<const UField>(OuterField->GetOuter()))
	{
		if (IsDynamic(OuterField))
		{
			return true;
		}
	}
	return false;
}

bool FClass::IsOwnedByDynamicType(const FField* Field)
{
	for (FFieldVariant Owner = Field->GetOwnerVariant(); Owner.IsValid(); Owner = Owner.GetOwnerVariant())
	{
		if (Owner.IsUObject())
		{
			return IsOwnedByDynamicType(Cast<const UField>(Owner.ToUObject()));
		}
		else if (IsDynamic(Owner.ToField()))
		{
			return true;
		}
	}
	return false;
}

static TMap<const UField*, FString> UFieldTypePackageNames;
static TMap<const FField*, FString> FFieldTypePackageNames;

template <typename T>
const FString& GetTypePackageName_Inner(const T* Field, TMap<const T*, FString>& TypePackageNames)
{
	FString* TypePackageName = TypePackageNames.Find(Field);
	if (TypePackageName == nullptr)
	{
		FString PackageName = Field->GetMetaData(FClass::NAME_ReplaceConverted);
		if (PackageName.Len())
		{
			int32 ObjectDotIndex = INDEX_NONE;
			// Strip the object name
			if (PackageName.FindChar(TEXT('.'), ObjectDotIndex))
			{
				PackageName.MidInline(0, ObjectDotIndex, false);
			}
		}
		else
		{
			PackageName = Field->GetOutermost()->GetName();
		}
		TypePackageName = &TypePackageNames.Add(Field, MoveTemp(PackageName));
	}
	return *TypePackageName;
}

const FString& FClass::GetTypePackageName(const UField* Field)
{
	return GetTypePackageName_Inner(Field, UFieldTypePackageNames);
}

const FString& FClass::GetTypePackageName(const FField* Field)
{
	return GetTypePackageName_Inner(Field, FFieldTypePackageNames);
}
