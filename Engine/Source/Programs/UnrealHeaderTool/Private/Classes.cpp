// Copyright Epic Games, Inc. All Rights Reserved.

#include "Classes.h"
#include "UnrealHeaderTool.h"
#include "UObject/ErrorException.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/ObjectRedirector.h"
#include "StringUtils.h"
#include "UnrealTypeDefinitionInfo.h"

namespace
{
	/** 
	 * Returns True if the given class name includes a valid Unreal prefix and matches based on the given class.
	 *
	 * @param InNameToCheck - Name w/ potential prefix to check
	 * @param OriginalClass - Class to check against
	 */
	bool ClassNameHasValidPrefix(const FString& InNameToCheck, const UClass* OriginalClass)
	{
		bool bIsLabledDeprecated;
		GetClassPrefix( InNameToCheck, bIsLabledDeprecated );

		// If the class is labeled deprecated, don't try to resolve it during header generation, valid results can't be guaranteed.
		if (bIsLabledDeprecated)
		{
			return true;
		}

		const FString OriginalClassName = FUnrealTypeDefinitionInfo::GetNameWithPrefix(OriginalClass);

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

UClass* FClasses::FindClass(const TCHAR* ClassName)
{
	check(ClassName);

	UObject* ClassPackage = ANY_PACKAGE;

	if (UClass* Result = FindObject<UClass>(ClassPackage, ClassName))
	{
		return Result;
	}

	if (UObjectRedirector* RenamedClassRedirector = FindObject<UObjectRedirector>(ClassPackage, ClassName))
	{
		return CastChecked<UClass>(RenamedClassRedirector->DestinationObject);
	}

	return nullptr;
}

UClass* FClasses::FindScriptClassOrThrow(const FString& InClassName)
{
	FString ErrorMsg;
	if (UClass* Result = FindScriptClass(InClassName, &ErrorMsg))
	{
		return Result;
	}

	FError::Throwf(*ErrorMsg);

	// Unreachable, but compiler will warn otherwise because FError::Throwf isn't declared noreturn
	return 0;
}

UClass* FClasses::FindScriptClass(const FString& InClassName, FString* OutErrorMsg)
{
	// Strip the class name of its prefix and then do a search for the class
	FString ClassNameStripped = GetClassNameWithPrefixRemoved(InClassName);
	if (UClass* FoundClass = FindClass(*ClassNameStripped))
	{
		// If the class was found with the stripped class name, verify that the correct prefix was used and throw an error otherwise
		if (!ClassNameHasValidPrefix(InClassName, FoundClass))
		{
			if (OutErrorMsg)
			{
				*OutErrorMsg = FString::Printf(TEXT("Class '%s' has an incorrect prefix, expecting '%s'"), *InClassName, *FUnrealTypeDefinitionInfo::GetNameWithPrefix(FoundClass));
			}
			return nullptr;
		}

		return FoundClass;
	}

	// Couldn't find the class with a class name stripped of prefix (or a prefix was not found)
	// See if the prefix was forgotten by trying to find the class with the given identifier
	if (UClass* FoundClass = FindClass(*InClassName))
	{
		// If the class was found with the given identifier, the user forgot to use the correct Unreal prefix	
		if (OutErrorMsg)
		{
			*OutErrorMsg = FString::Printf(TEXT("Class '%s' is missing a prefix, expecting '%s'"), *InClassName, *FUnrealTypeDefinitionInfo::GetNameWithPrefix(FoundClass));
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
