// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/Docking/SDockTab.h"
#include "SWebBrowser.h"
#include "UI/SLoginWindow.h"

class FBridgeUIManagerImpl;
class FArguments;
class SCompoundWidget;

class  FBridgeUIManager
{
public:
	static void Initialize();
	static void Shutdown();
	static TSharedPtr<FBridgeUIManagerImpl> Instance;
	static UMegascansAuthentication* MegascansAuthentication;
};


