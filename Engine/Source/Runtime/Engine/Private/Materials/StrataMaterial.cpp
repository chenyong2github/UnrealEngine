// Copyright Epic Games, Inc. All Rights Reserved.

#include "StrataMaterial.h"
#include "MaterialCompiler.h"
#include "Materials/MaterialInterface.h"

FString GetStrataBSDFName(uint8 BSDFType)
{
	switch (BSDFType)
	{
	case STRATA_BSDF_TYPE_SLAB:
		return TEXT("SLAB");
		break;
	case STRATA_BSDF_TYPE_VOLUMETRICFOGCLOUD:
		return TEXT("VOLUMETRICFOGCLOUD");
		break;
	case STRATA_BSDF_TYPE_UNLIT:
		return TEXT("UNLIT");
		break;
	case STRATA_BSDF_TYPE_HAIR:
		return TEXT("HAIR");
		break;
	case STRATA_BSDF_TYPE_EYE:
		return TEXT("EYE");
		break;
	case STRATA_BSDF_TYPE_SINGLELAYERWATER:
		return TEXT("SINGLELAYERWATER");
		break;
	}
	check(false);
	return "";
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateNullSharedLocalBasis()
{
	return FStrataRegisteredSharedLocalBasis();
}

FStrataRegisteredSharedLocalBasis StrataCompilationInfoCreateSharedLocalBasis(FMaterialCompiler* Compiler, int32 NormalCodeChunk, int32 TangentCodeChunk)
{
	if (TangentCodeChunk == INDEX_NONE)
	{
		return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk);
	}
	return Compiler->StrataCompilationInfoRegisterSharedLocalBasis(NormalCodeChunk, TangentCodeChunk);
}

bool Engine_IsStrataEnabled();

inline bool IsStrataEnabled()
{
	static bool bStrataEnabled = Engine_IsStrataEnabled();
	return bStrataEnabled;
}

#define IsGenericBlendMode(Name, LegacyCondition, StrataCondition) \
	bool Is##Name##BlendMode(EBlendMode BlendMode)											{ return LegacyCondition; } \
	bool Is##Name##BlendMode(EStrataBlendMode BlendMode)									{ return StrataCondition; } \
	bool Is##Name##BlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)	{ return IsStrataEnabled() ? Is##Name##BlendMode(StrataBlendMode) : Is##Name##BlendMode(LegacyBlendMode); } \
	bool Is##Name##BlendMode(const FMaterial& In)											{ return IsStrataEnabled() ? Is##Name##BlendMode(In.GetStrataBlendMode()) : Is##Name##BlendMode(In.GetBlendMode()); } \
	bool Is##Name##BlendMode(const UMaterialInterface& In)									{ return IsStrataEnabled() ? Is##Name##BlendMode(In.GetStrataBlendMode()) : Is##Name##BlendMode(In.GetBlendMode()); } \
	bool Is##Name##BlendMode(const FMaterialShaderParameters& In)							{ return IsStrataEnabled() ? Is##Name##BlendMode(In.StrataBlendMode) : Is##Name##BlendMode(In.BlendMode); }

IsGenericBlendMode(Opaque,			BlendMode == BLEND_Opaque,								BlendMode == SBM_Opaque)																			// Opaque blend mode
IsGenericBlendMode(Masked,			BlendMode == BLEND_Masked,								BlendMode == SBM_Masked)																			// Masked blend mode
IsGenericBlendMode(OpaqueOrMasked,	BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked, BlendMode == SBM_Opaque || BlendMode == SBM_Masked)													// Opaque or Masked blend mode
IsGenericBlendMode(Translucent,		BlendMode != BLEND_Opaque && BlendMode != BLEND_Masked, BlendMode != SBM_Opaque && BlendMode != SBM_Masked)													// General translucency (i.e., blend mode is something else than Opaque/Masked)
IsGenericBlendMode(TranslucentOnly, BlendMode == BLEND_Translucent,							BlendMode == SBM_TranslucentColoredTransmittance || BlendMode == SBM_TranslucentGreyTransmittance) 	// Explicit translucency blend mode
IsGenericBlendMode(AlphaHoldout,	BlendMode == BLEND_AlphaHoldout,						BlendMode == SBM_AlphaHoldout)																		// AlphaHoldout blend mode
IsGenericBlendMode(Modulate,		BlendMode == BLEND_Modulate,							BlendMode == SBM_ColoredTransmittanceOnly)															// Modulate blend mode