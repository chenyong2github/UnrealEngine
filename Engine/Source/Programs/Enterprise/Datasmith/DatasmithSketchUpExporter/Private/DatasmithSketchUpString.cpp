// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpString.h"


wchar_t* FDatasmithSketchUpString::GetWideString(
	SUStringRef InSStringRef
)
{
	size_t SStringLength = 0;
	SUStringGetUTF16Length(InSStringRef, &SStringLength); // we can ignore the returned SU_RESULT

	wchar_t* WideString = new wchar_t[SStringLength + 1]; // account for the terminating NULL put by SketchUp
	SUStringGetUTF16(InSStringRef, SStringLength + 1, WideString, &SStringLength); // we can ignore the returned SU_RESULT

	return WideString;
}
