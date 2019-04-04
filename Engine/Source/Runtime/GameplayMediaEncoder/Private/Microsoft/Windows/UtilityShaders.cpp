// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UtilityShaders.cpp: Utility shaders for Windows Media Foundation pipeline.
=============================================================================*/

#include "UtilityShaders.h"

GAMEPLAYMEDIAENCODER_START

// Shader implementations.
IMPLEMENT_SHADER_TYPE(,FScreenSwizzlePS, TEXT("/Engine/Private/GameplayMediaEncoderShaders.usf"), TEXT("ScreenSwizzlePS"), SF_Pixel);

GAMEPLAYMEDIAENCODER_END

