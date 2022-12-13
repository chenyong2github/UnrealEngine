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

// Opaque blend mode
bool IsOpaqueBlendMode(EBlendMode BlendMode)													{ return BlendMode == BLEND_Opaque; }
bool IsOpaqueBlendMode(EStrataBlendMode BlendMode)												{ return BlendMode == SBM_Opaque; }
bool IsOpaqueBlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)			{ return IsStrataEnabled() ? IsOpaqueBlendMode(StrataBlendMode) : IsOpaqueBlendMode(LegacyBlendMode); }
bool IsOpaqueBlendMode(const FMaterial& In)														{ return IsStrataEnabled() ? IsOpaqueBlendMode(In.GetStrataBlendMode()) : IsOpaqueBlendMode(In.GetBlendMode()); }
bool IsOpaqueBlendMode(const UMaterialInterface& In)											{ return IsStrataEnabled() ? IsOpaqueBlendMode(In.GetStrataBlendMode()) : IsOpaqueBlendMode(In.GetBlendMode()); }

// Masked blend mode
bool IsMaskedBlendMode(EBlendMode BlendMode)													{ return BlendMode == BLEND_Masked; }
bool IsMaskedBlendMode(EStrataBlendMode BlendMode)												{ return BlendMode == SBM_Masked; }
bool IsMaskedBlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)			{ return IsStrataEnabled() ? IsMaskedBlendMode(StrataBlendMode) : IsMaskedBlendMode(LegacyBlendMode); }
bool IsMaskedBlendMode(const FMaterial& In)														{ return IsStrataEnabled() ? IsMaskedBlendMode(In.GetStrataBlendMode()) : IsMaskedBlendMode(In.GetBlendMode()); }
bool IsMaskedBlendMode(const UMaterialInterface& In)											{ return IsStrataEnabled() ? IsMaskedBlendMode(In.GetStrataBlendMode()) : IsMaskedBlendMode(In.GetBlendMode()); }

// Opaque or Masked blend mode
bool IsOpaqueOrMaskedBlendMode(EBlendMode BlendMode)											{ return BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked; }
bool IsOpaqueOrMaskedBlendMode(EStrataBlendMode BlendMode)										{ return BlendMode == SBM_Opaque || BlendMode == SBM_Masked; }
bool IsOpaqueOrMaskedBlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)	{ return IsStrataEnabled() ? IsOpaqueOrMaskedBlendMode(StrataBlendMode) : IsOpaqueOrMaskedBlendMode(LegacyBlendMode); }
bool IsOpaqueOrMaskedBlendMode(const FMaterial& In)												{ return IsStrataEnabled() ? IsOpaqueOrMaskedBlendMode(In.GetStrataBlendMode()) : IsOpaqueOrMaskedBlendMode(In.GetBlendMode()); }
bool IsOpaqueOrMaskedBlendMode(const UMaterialInterface& In)									{ return IsStrataEnabled() ? IsOpaqueOrMaskedBlendMode(In.GetStrataBlendMode()) : IsOpaqueOrMaskedBlendMode(In.GetBlendMode()); }

// General translucency (i.e., blend mode is something else than Opaque/Masked)
bool IsTranslucentBlendMode(EBlendMode BlendMode)												{ return BlendMode != BLEND_Opaque && BlendMode != BLEND_Masked; }
bool IsTranslucentBlendMode(EStrataBlendMode BlendMode)											{ return BlendMode != SBM_Opaque && BlendMode != SBM_Masked; }
bool IsTranslucentBlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)		{ return IsStrataEnabled() ? IsTranslucentBlendMode(StrataBlendMode) : IsTranslucentBlendMode(LegacyBlendMode); }
bool IsTranslucentBlendMode(const FMaterial& In)												{ return IsStrataEnabled() ? IsTranslucentBlendMode(In.GetStrataBlendMode()) : IsTranslucentBlendMode(In.GetBlendMode()); }
bool IsTranslucentBlendMode(const UMaterialInterface& In)										{ return IsStrataEnabled() ? IsTranslucentBlendMode(In.GetStrataBlendMode()) : IsTranslucentBlendMode(In.GetBlendMode()); }

// Explicit translucency blend modes
bool IsTranslucentOnlyBlendMode(EBlendMode BlendMode)											{ return BlendMode == BLEND_Translucent; }
bool IsTranslucentOnlyBlendMode(EStrataBlendMode BlendMode)										{ return BlendMode == SBM_TranslucentColoredTransmittance || BlendMode == SBM_TranslucentGreyTransmittance; }
bool IsTranslucentOnlyBlendMode(EBlendMode LegacyBlendMode, EStrataBlendMode StrataBlendMode)	{ return IsStrataEnabled() ? IsTranslucentOnlyBlendMode(StrataBlendMode) : IsTranslucentOnlyBlendMode(LegacyBlendMode); }
bool IsTranslucentOnlyBlendMode(const FMaterial& In)											{ return IsStrataEnabled() ? IsTranslucentOnlyBlendMode(In.GetStrataBlendMode()) : IsTranslucentOnlyBlendMode(In.GetBlendMode()); }
bool IsTranslucentOnlyBlendMode(const UMaterialInterface& In)									{ return IsStrataEnabled() ? IsTranslucentOnlyBlendMode(In.GetStrataBlendMode()) : IsTranslucentOnlyBlendMode(In.GetBlendMode()); }