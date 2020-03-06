// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelectionSystem/DataprepSelectionSystemUtils.h"

namespace DataprepSelectionSystemUtils
{
	FString LexToString(const TArray<FString>& StringArray)
	{
		if (StringArray.Num() == 0)
		{
			return {};
		}

		int32 NewStringSize = 0;
		for (const FString& Element : StringArray)
		{
			NewStringSize += Element.Len();
		}
		NewStringSize += StringArray.Num() - 1 * 2;

		FString String;
		const FString& Last = StringArray.Last();
		for (const FString& Element : StringArray)
		{
			String.Append(Element);
			if (Element != Last)
			{
				String.Append(TEXT(", "));
			}
		}
		return String;
	}
}
