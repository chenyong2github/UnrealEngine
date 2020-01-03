// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_ADVANCES_H
THIRD_PARTY_INCLUDES_END

const int32 FontPower = 6;					// Font Size 64
const int32 FontSize = 1 << FontPower;
const float FontInverseScale = 1.0f / FontSize;

DECLARE_LOG_CATEGORY_EXTERN(LogText3D, Log, All);

class FText3DModule : public IModuleInterface
{
public:
	FText3DModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FT_Library GetFreeTypeLibrary();

private:
	FT_Library	FreeTypeLib;
};
