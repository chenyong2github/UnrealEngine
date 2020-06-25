
#pragma once


#include "CoreMinimal.h"

class FTabManager;

class MegascansSettingsWindow
{
public:
	
	
	static void OpenSettingsWindow(const TSharedRef<FTabManager>& TabManager);
	static void SaveSettings(const TSharedRef<SWindow>& Window, UMegascansSettings* MegascansSettings);	
	
	
};




