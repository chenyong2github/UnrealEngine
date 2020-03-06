// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FSoundWaveAssetActionExtenderConvolutionReverb
{
public:
	static void RegisterMenus();
	static void GetExtendedActions(const struct FToolMenuContext& MenuContext);
	static void ExecuteCreateImpulseResponse(const struct FToolMenuContext& MenuContext);
};

