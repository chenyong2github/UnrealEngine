// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class ISlateStyle;

//////////////////////////////////////////////////////////////////////////
// FCameraCalibrationEditorStyle

class FCameraCalibrationEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();

	static const ISlateStyle& Get();
};