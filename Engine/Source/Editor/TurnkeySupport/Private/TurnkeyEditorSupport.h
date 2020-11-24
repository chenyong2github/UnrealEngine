// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class FTurnkeyEditorSupport
{
public:

	static FString GetUATOptions();

	static void PrepareToLaunchRunningMap(const FString& DeviceId, const FString& DeviceName);
	static void LaunchRunningMap(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey);
	static void AddEditorOptions(class FMenuBuilder& MenuBuilder);

	static bool DoesProjectHaveCode();
	static void RunUAT(const FString& CommandLine, const FText& PlatformDisplayName, const FText& TaskName, const FText& TaskShortName, const struct FSlateBrush* TaskIcon, TFunction<void(FString, double)> ResultCallback=TFunction<void(FString, double)>());

	static bool ShowOKCancelDialog(FText Message, FText Title);
	static void ShowRestartToast();
	static bool CheckSupportedPlatforms(FName IniPlatformName);

};
