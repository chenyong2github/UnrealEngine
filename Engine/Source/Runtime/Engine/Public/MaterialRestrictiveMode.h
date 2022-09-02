// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITORONLY_DATA

/// Get whether the HLSL material translator is in restrive mode.
ENGINE_API bool GetHLSLMaterialTranslatorRestrictiveMode();

/// Puts the HLSL material translator into restrictive mode.
ENGINE_API void SetHLSLMaterialTranslatorRestrictiveMode(bool bRestrictiveMode);

/// Scope helper to toggle restrictive mode for a code block
struct FScopedHLSLMaterialTranslatorRestrictiveModeChange
{
	FORCEINLINE FScopedHLSLMaterialTranslatorRestrictiveModeChange(bool bRestrictiveMode)
	{
		bRestoreRestrictiveMode = GetHLSLMaterialTranslatorRestrictiveMode();
		SetHLSLMaterialTranslatorRestrictiveMode(bRestrictiveMode);
	}
	
	FORCEINLINE ~FScopedHLSLMaterialTranslatorRestrictiveModeChange()
	{
		SetHLSLMaterialTranslatorRestrictiveMode(bRestoreRestrictiveMode);
	}

private:
	bool bRestoreRestrictiveMode;
};

#endif // WITH_EDITORONLY_DATA
