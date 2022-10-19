// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialOverrideNanite.h"

#include "Interfaces/ITargetPlatform.h"
#include "Materials/MaterialInterface.h"
#include "RenderUtils.h"
#include "UObject/FortniteMainBranchObjectVersion.h"


bool FMaterialOverrideNanite::CanUseOverride(EShaderPlatform ShaderPlatform) const
{
	return DoesPlatformSupportNanite(ShaderPlatform);
}

#if WITH_EDITOR

void FMaterialOverrideNanite::RefreshOverrideMaterial(bool& bOutUpdated)
{
	bOutUpdated = false;

	// We don't resolve the soft pointer if we're cooking. 
	// Instead we defer any resolve to LoadOverrideForPlatform() which should be called in BeginCacheForCookedPlatformData().
	if (FApp::CanEverRender())
	{
		// We evaluate with the bEnableOverride flag here.
		// That way we return the correct bOutUpdated status when the flag is being toggled.
		// And if the flag is not set we don't need to cook the material.
		UMaterialInterface* ObjectPtr = bEnableOverride ? OverrideMaterialRef.LoadSynchronous() : nullptr;
		if (ObjectPtr != OverrideMaterial.Get())
		{
			OverrideMaterial = ObjectPtr;
			bOutUpdated = true;
		}
	}
}

#endif // WITH_EDITOR

bool FMaterialOverrideNanite::Serialize(FArchive& Ar)
{
	{
		// Use non-collecting serialization scope for override material.
		// This prevents the cook from automatically seeing it, so that we can avoid cooking it on non-nanite platforms.
		FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);
		Ar << OverrideMaterialRef;
	}

	Ar << bEnableOverride;

	// We don't want the hard references somehow serializing to saved maps.
	// So we only serialize hard references when loading, or when cooking supported platforms.
	// Note that this approach won't be correct for a multi-platform cook with both nanite and non-nanite platforms.
	bool bSerializeOverrideObject = Ar.IsLoading();
#if WITH_EDITOR
	if (Ar.IsCooking())
	{
		TArray<FName> ShaderFormats;
		Ar.CookingTarget()->GetAllTargetedShaderFormats(ShaderFormats);
		for (FName ShaderFormat : ShaderFormats)
		{
			const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormat);
			if (CanUseOverride(ShaderPlatform))
			{
				bSerializeOverrideObject = true;
				break;
			}
		}
	}
#endif

	if (bSerializeOverrideObject)
	{
		Ar << OverrideMaterial;
	}
	else
	{
		TObjectPtr<UMaterialInterface> Dummy;
		Ar << Dummy;
	}

	return true;
}

void FMaterialOverrideNanite::PostLoad()
{
#if WITH_EDITOR
	bool bUpdated;
	RefreshOverrideMaterial(bUpdated);
#endif
}

#if WITH_EDITOR

void FMaterialOverrideNanite::PostEditChange()
{
	bool bUpdated;
	RefreshOverrideMaterial(bUpdated);
}

void FMaterialOverrideNanite::LoadOverrideForPlatform(const ITargetPlatform* TargetPlatform)
{
	bool bCookOverrideObject = false;
	TArray<FName> ShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
	for (FName ShaderFormat : ShaderFormats)
	{
		const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormat);
		if (CanUseOverride(ShaderPlatform))
		{
			bCookOverrideObject = true;
			break;
		}
	}

	if (bCookOverrideObject)
	{
		OverrideMaterial = bEnableOverride ? OverrideMaterialRef.LoadSynchronous() : nullptr;
	}
}

void FMaterialOverrideNanite::ClearOverride()
{
	OverrideMaterial = nullptr;
}

#endif // WITH_EDITOR
