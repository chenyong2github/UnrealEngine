// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class FOpenXRAssetDirectory
{
public: 
#if WITH_EDITORONLY_DATA
	OPENXRHMD_API static void LoadForCook();
	OPENXRHMD_API static void ReleaseAll();
#endif

	static FSoftObjectPath GoogleDaydream;
	static FSoftObjectPath HPMixedRealityLeft;
	static FSoftObjectPath HPMixedRealityRight;
	static FSoftObjectPath HTCVive;
	static FSoftObjectPath HTCViveCosmosLeft;
	static FSoftObjectPath HTCViveCosmosRight;
	static FSoftObjectPath HTCViveFocus;
	static FSoftObjectPath HTCViveFocusPlus;
	static FSoftObjectPath MagicLeapOne;
	static FSoftObjectPath MicrosoftMixedRealityLeft;
	static FSoftObjectPath MicrosoftMixedRealityRight;
	static FSoftObjectPath OculusGo;
	static FSoftObjectPath OculusTouchLeft;
	static FSoftObjectPath OculusTouchRight;
	static FSoftObjectPath OculusTouchV2Left;
	static FSoftObjectPath OculusTouchV2Right;
	static FSoftObjectPath OculusTouchV3Left;
	static FSoftObjectPath OculusTouchV3Right;
	static FSoftObjectPath PicoG2;
	static FSoftObjectPath PicoNeo2Left;
	static FSoftObjectPath PicoNeo2Right;
	static FSoftObjectPath SamsungGearVR;
	static FSoftObjectPath SamsungOdysseyLeft;
	static FSoftObjectPath SamsungOdysseyRight;
	static FSoftObjectPath ValveIndexLeft;
	static FSoftObjectPath ValveIndexRight;
};
