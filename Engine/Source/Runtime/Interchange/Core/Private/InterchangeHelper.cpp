// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeHelper.h"

namespace UE::Interchange
{
	FString MakeName(const FString& InName, bool bIsJoint)
	{
		FString SpecialChars = bIsJoint ? UnsupportedJointNameCharacters : UnsupportedNameCharacters;
		FString TmpName = InName;

		// Remove namespaces
		int32 LastNamespaceTokenIndex = INDEX_NONE;
		if (TmpName.FindLastChar(TEXT(':'), LastNamespaceTokenIndex))
		{
			const bool bAllowShrinking = true;
			//+1 to remove the ':' character we found
			TmpName.RightChopInline(LastNamespaceTokenIndex + 1, bAllowShrinking);
		}

		//Remove the special chars
		for (size_t CharIndex = 0; CharIndex < SpecialChars.Len(); CharIndex++)
		{
			TmpName.ReplaceCharInline(SpecialChars[CharIndex], TEXT('_'), ESearchCase::CaseSensitive);
		}

		return TmpName;
	}
};