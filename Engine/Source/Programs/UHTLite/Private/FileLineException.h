// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Templates/IsValidVariadicFunctionArg.h"

struct FFileLineException
{
	FString Message;
	FString Filename;
	int32   Line;

	template <typename... Types>
	UE_NORETURN static void VARARGS Throwf(FString&& Filename, int32 Line, const TCHAR* Fmt, Types... Args)
	{
		static_assert(TAnd<TIsValidVariadicFunctionArg<Types>...>::Value, "Invalid argument(s) passed to FError::Throwf");

		ThrowfImpl(MoveTemp(Filename), Line, Fmt, Args...);
	}

private:
	UE_NORETURN static void VARARGS ThrowfImpl(FString&& Filename, int32 Line, const TCHAR* Fmt, ...);

	FFileLineException(FString&& InMessage, FString&& InFilename, int32 InLine);
};
