// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXRuntimeUtils.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"


#define LOCTEXT_NAMESPACE "FDMXRuntimeUtils"

FString FDMXRuntimeUtils::GenerateUniqueNameFromExisting(const TSet<FString>& InExistingNames, const FString& InBaseName)
{
	if (!InBaseName.IsEmpty() && !InExistingNames.Contains(InBaseName))
	{
		return InBaseName;
	}

	FString FinalName;
	FString BaseName;

	int32 Index = 0;
	if (InBaseName.IsEmpty())
	{
		BaseName = TEXT("Default name");
	}
	else
	{
		// If there's an index at the end of the name, start from there
		FDMXRuntimeUtils::GetNameAndIndexFromString(InBaseName, BaseName, Index);
	}

	int32 Count = (Index == 0) ? 1 : Index;
	FinalName = BaseName;
	// Add Count to the BaseName, increasing Count, until it's a non-existent name
	do
	{
		// Calculate the number of digits in the number, adding 2 (1 extra to correctly count digits, another to account for the '_' that will be added to the name
		int32 CountLength = Count > 0 ? (int32)FGenericPlatformMath::LogX(10.0f, Count) + 2 : 2;

		// If the length of the final string will be too long, cut off the end so we can fit the number
		if (CountLength + BaseName.Len() >= NAME_SIZE)
		{
			BaseName = BaseName.Left(NAME_SIZE - CountLength - 1);
		}

		FinalName = FString::Printf(TEXT("%s_%d"), *BaseName, Count);
		++Count;
	} while (InExistingNames.Contains(FinalName));

	return FinalName;
}

FString FDMXRuntimeUtils::FindUniqueEntityName(const UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> InEntityClass, const FString& InBaseName /*= TEXT("")*/)
{
	check(InLibrary);

	// Get existing names for the current entity type
	TSet<FString> EntityNames;
	InLibrary->ForEachEntityOfType(InEntityClass, [&EntityNames](UDMXEntity* Entity)
		{
			EntityNames.Add(Entity->GetDisplayName());
		});

	FString BaseName = InBaseName;

	// If no base name was set, use the entity class name as base
	if (BaseName.IsEmpty())
	{
		BaseName = *InEntityClass->GetName();
	}

	return FDMXRuntimeUtils::GenerateUniqueNameFromExisting(EntityNames, BaseName);
}

bool FDMXRuntimeUtils::GetNameAndIndexFromString(const FString& InString, FString& OutName, int32& OutIndex)
{
	OutName = InString.TrimEnd();

	// If there's an index at the end of the name, erase it
	int32 DigitIndex = OutName.Len();
	while (DigitIndex > 0 && OutName[DigitIndex - 1] >= '0' && OutName[DigitIndex - 1] <= '9')
	{
		--DigitIndex;
	}

	bool bHadIndex = false;
	if (DigitIndex < OutName.Len() && DigitIndex > -1)
	{
		OutIndex = FCString::Atoi(*OutName.RightChop(DigitIndex));
		OutName.LeftInline(DigitIndex);
		bHadIndex = true;
	}
	else
	{
		OutIndex = 0;
	}

	// Remove separator characters at the end of the string
	OutName.TrimEndInline();
	DigitIndex = OutName.Len(); // reuse this variable for the separator index

	while (DigitIndex > 0
		&& (OutName[DigitIndex  - 1] == '_'
		|| OutName[DigitIndex - 1] == '.'
		|| OutName[DigitIndex - 1] == '-'))
	{
		--DigitIndex;
	}

	if (DigitIndex < OutName.Len() && DigitIndex > -1)
	{
		OutName.LeftInline(DigitIndex);
	}

	return bHadIndex;
}

TMap<int32, TArray<UDMXEntityFixturePatch*>> FDMXRuntimeUtils::MapToUniverses(const TArray<UDMXEntityFixturePatch*>& AllPatches)
{
	TMap<int32, TArray<UDMXEntityFixturePatch*>> Result;
	for (UDMXEntityFixturePatch* Patch : AllPatches)
	{
		TArray<UDMXEntityFixturePatch*>& UniverseGroup = Result.FindOrAdd(Patch->GetUniverseID());
		UniverseGroup.Add(Patch);
	}
	return Result;
}

FString FDMXRuntimeUtils::GenerateUniqueNameForImportFunction(TMap<FString, uint32>& OutPotentialFunctionNamesAndCount, const FString& InBaseName)
{
	if (!InBaseName.IsEmpty() && !OutPotentialFunctionNamesAndCount.Contains(InBaseName))
	{
		return InBaseName;
	}

	FString BaseName;

	int32 Index = 0;
	if (InBaseName.IsEmpty())
	{
		BaseName = TEXT("Default name");
	}
	else
	{
		// If there's an index at the end of the name, start from there
		FDMXRuntimeUtils::GetNameAndIndexFromString(InBaseName, BaseName, Index);
	}

	FString FinalName = BaseName;

	// Generate a new Unique name and update the Unique counter
	if (uint32* CountPointer = OutPotentialFunctionNamesAndCount.Find(InBaseName))
	{
		// Calculate the number of digits in the number, adding 2 (1 extra to correctly count digits, another to account for the '_' that will be added to the name
		int32 CountLength = *CountPointer > 0 ? (int32)FGenericPlatformMath::LogX(10.0f, *CountPointer) + 2 : 2;

		// If the length of the final string will be too long, cut off the end so we can fit the number
		if (CountLength + BaseName.Len() >= NAME_SIZE)
		{
			BaseName = BaseName.Left(NAME_SIZE - CountLength - 1);
		}
		
		if (*CountPointer > 0)
		{
			FinalName = FString::Printf(TEXT("%s_%d"), *BaseName, *CountPointer);
		}
		else
		{
			FinalName = FString::Printf(TEXT("%s"), *BaseName);
		}

		*CountPointer = *CountPointer + 1;
	}

	return FinalName;
}

#undef LOCTEXT_NAMESPACE
