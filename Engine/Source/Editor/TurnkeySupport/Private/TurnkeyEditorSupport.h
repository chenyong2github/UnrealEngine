// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class FTurnkeyEditorSupport
{
public:

	static FString GetUATOptions();

	static void LaunchRunningMap(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey);
	static void AddEditorOptions(FMenuBuilder& MenuBuilder);

	static bool DoesProjectHaveCode();
	static void RunUAT(const FString& CommandLine, const FText& PlatformDisplayName, const FText& TaskName, const FText& TaskShortName, const FSlateBrush* TaskIcon, TFunction<void(FString, double)> ResultCallback=TFunction<void(FString, double)>());

	static bool ShowOKCancelDialog(FText Message, FText Title);
	static bool CheckSupportedPlatforms(FName IniPlatformName);

};
