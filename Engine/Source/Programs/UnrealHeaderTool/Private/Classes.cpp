// Copyright Epic Games, Inc. All Rights Reserved.

#include "Classes.h"
#include "UnrealHeaderTool.h"
#include "UObject/ErrorException.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/ObjectRedirector.h"
#include "StringUtils.h"

namespace
{
	/** 
	 * Returns True if the given class name includes a valid Unreal prefix and matches based on the given class.
	 *
	 * @param InNameToCheck - Name w/ potential prefix to check
	 * @param OriginalClass - Class to check against
	 */
	bool ClassNameHasValidPrefix(const FString& InNameToCheck, const FClass* OriginalClass)
	{
		bool bIsLabledDeprecated;
		GetClassPrefix( InNameToCheck, bIsLabledDeprecated );

		// If the class is labeled deprecated, don't try to resolve it during header generation, valid results can't be guaranteed.
		if (bIsLabledDeprecated)
		{
			return true;
		}

		const FString OriginalClassName = OriginalClass->GetNameWithPrefix();

		bool bNamesMatch = (InNameToCheck == OriginalClassName);

		if (!bNamesMatch)
		{
			//@TODO: UCREMOVAL: I/U interface hack - Ignoring prefixing for this call
			if (OriginalClass->HasAnyClassFlags(CLASS_Interface))
			{
				bNamesMatch = InNameToCheck.Mid(1) == OriginalClassName.Mid(1);
			}
		}

		return bNamesMatch;
	}
}

FClasses::FClasses(const TArray<UClass*>* Classes)
	: UObjectClass((FClass*)UObject::StaticClass())
	, ClassTree(UObjectClass)
{
	if (Classes)
	{
		for (UClass* Class : *Classes)
		{
			ClassTree.AddClass(Class);
		}
	}
}

FClass* FClasses::GetRootClass() const
{
	return UObjectClass;
}

FClass* FClasses::FindClass(const TCHAR* ClassName)
{
	check(ClassName);

	UObject* ClassPackage = ANY_PACKAGE;

	if (UClass* Result = FindObject<UClass>(ClassPackage, ClassName))
	{
		return (FClass*)Result;
	}

	if (UObjectRedirector* RenamedClassRedirector = FindObject<UObjectRedirector>(ClassPackage, ClassName))
	{
		return (FClass*)CastChecked<UClass>(RenamedClassRedirector->DestinationObject);
	}

	return nullptr;
}

FClass* FClasses::FindScriptClassOrThrow(const FString& InClassName)
{
	FString ErrorMsg;
	if (FClass* Result = FindScriptClass(InClassName, &ErrorMsg))
	{
		return Result;
	}

	FError::Throwf(*ErrorMsg);

	// Unreachable, but compiler will warn otherwise because FError::Throwf isn't declared noreturn
	return 0;
}

FClass* FClasses::FindScriptClass(const FString& InClassName, FString* OutErrorMsg)
{
	// Strip the class name of its prefix and then do a search for the class
	FString ClassNameStripped = GetClassNameWithPrefixRemoved(InClassName);
	if (FClass* FoundClass = FindClass(*ClassNameStripped))
	{
		// If the class was found with the stripped class name, verify that the correct prefix was used and throw an error otherwise
		if (!ClassNameHasValidPrefix(InClassName, FoundClass))
		{
			if (OutErrorMsg)
			{
				*OutErrorMsg = FString::Printf(TEXT("Class '%s' has an incorrect prefix, expecting '%s'"), *InClassName, *FoundClass->GetNameWithPrefix());
			}
			return nullptr;
		}

		return (FClass*)FoundClass;
	}

	// Couldn't find the class with a class name stripped of prefix (or a prefix was not found)
	// See if the prefix was forgotten by trying to find the class with the given identifier
	if (FClass* FoundClass = FindClass(*InClassName))
	{
		// If the class was found with the given identifier, the user forgot to use the correct Unreal prefix	
		if (OutErrorMsg)
		{
			*OutErrorMsg = FString::Printf(TEXT("Class '%s' is missing a prefix, expecting '%s'"), *InClassName, *FoundClass->GetNameWithPrefix());
		}
	}
	else
	{
		// If the class was still not found, it wasn't a valid identifier
		if (OutErrorMsg)
		{
			*OutErrorMsg = FString::Printf(TEXT("Class '%s' not found."), *InClassName);
		}
	}

	return nullptr;
}

TArray<FClass*> FClasses::GetClassesInPackage(const UPackage* InPackage) const
{
	TArray<FClass*> Result;
	Result.Add(UObjectClass);

	// This cast is evil, but it'll work until we get TArray covariance. ;-)
	ClassTree.GetChildClasses((TArray<UClass*>&)Result, [=](const UClass* Class) { return InPackage == ANY_PACKAGE || Class->GetOuter() == InPackage; }, true);

	return Result;
}

#if WIP_UHT_REFACTOR

void FClasses::Validate()
{
	ClassTree.Validate();
}

#endif
