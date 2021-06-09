// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CapsuleShadowRendering.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHIDefinitions.h"

extern int32 GCapsuleShadows;
extern int32 GCapsuleDirectShadows;
extern int32 GCapsuleIndirectShadows;

inline bool DoesPlatformSupportCapsuleShadows(const FStaticShaderPlatform Platform)
{
	// Hasn't been tested elsewhere yet
	return Platform == SP_PCD3D_SM5
		|| IsMetalSM5Platform(Platform)
		|| IsVulkanSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsCapsuleShadows(Platform);
}

inline bool SupportsCapsuleShadows(ERHIFeatureLevel::Type FeatureLevel, FStaticShaderPlatform ShaderPlatform)
{
	return GCapsuleShadows
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& DoesPlatformSupportCapsuleShadows(ShaderPlatform);
}

inline bool SupportsCapsuleDirectShadows(ERHIFeatureLevel::Type FeatureLevel, FStaticShaderPlatform ShaderPlatform)
{
	return GCapsuleDirectShadows && SupportsCapsuleShadows(FeatureLevel, ShaderPlatform);
}

inline bool SupportsCapsuleIndirectShadows(ERHIFeatureLevel::Type FeatureLevel, FStaticShaderPlatform ShaderPlatform)
{
	return GCapsuleIndirectShadows && SupportsCapsuleShadows(FeatureLevel, ShaderPlatform);
}
