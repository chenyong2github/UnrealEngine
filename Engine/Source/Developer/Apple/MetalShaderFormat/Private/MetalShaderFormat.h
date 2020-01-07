// Copyright Epic Games, Inc. All Rights Reserved.
// ....

#pragma once

#include "CoreMinimal.h"

// WatchOS is also an option but we do not support it yet.
enum EAppleSDKType
{
	AppleSDKMac,
	AppleSDKIOS,
	AppleSDKTVOS,
	AppleSDKCount,
};

extern void CompileShader_Metal(const struct FShaderCompilerInput& Input, struct FShaderCompilerOutput& Output, const class FString& WorkingDirectory);
extern uint32 GetMetalFormatVersion(FName Format);

