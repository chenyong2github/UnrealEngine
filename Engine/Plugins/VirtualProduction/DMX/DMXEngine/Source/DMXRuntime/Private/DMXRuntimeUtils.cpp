// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXRuntimeUtils.h"

#include "Library/DMXEntityFixturePatch.h"

#define LOCTEXT_NAMESPACE "FDMXRuntimeUtils"

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
	OutName.TrimEnd();
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
		TArray<UDMXEntityFixturePatch*>& UniverseGroup = Result.FindOrAdd(Patch->UniverseID);
		UniverseGroup.Add(Patch);
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
