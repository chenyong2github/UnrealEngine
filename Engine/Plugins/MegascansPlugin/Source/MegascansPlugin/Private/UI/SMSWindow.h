// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreMinimal.h"
#include "UI/MSSettings.h"

class FTabManager;

class MegascansSettingsWindow
{
public:
	
	
	static void OpenSettingsWindow(const TSharedRef<FTabManager>& TabManager);
	static void SaveSettings(const TSharedRef<SWindow>& Window, UMegascansSettings* MegascansSettings);	
	
	
};




