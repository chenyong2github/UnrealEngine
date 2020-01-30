// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_ADVANCES_H
THIRD_PARTY_INCLUDES_END

const int32 FontSize = 64;
const float FontInverseScale = 1.0f / FontSize;

DECLARE_LOG_CATEGORY_EXTERN(LogText3D, Log, All);

class FText3DModule final : public IModuleInterface
{
public:
	FText3DModule();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FT_Library GetFreeTypeLibrary();

private:
	FT_Library	FreeTypeLib;
};
