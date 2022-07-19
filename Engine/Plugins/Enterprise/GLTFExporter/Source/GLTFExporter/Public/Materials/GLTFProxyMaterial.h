// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Materials/GLTFProxyMaterialInfo.h"
#include "Materials/GLTFProxyMaterialParameter.h"
#include "Utilities/GLTFProxyMaterialUtilities.h"
#include "UObject/StrongObjectPtr.h"

template <typename MaterialType, typename = typename TEnableIf<TIsDerivedFrom<MaterialType, UMaterialInstance>::Value>::Type>
class TGLTFProxyMaterial
{
public:

	template <typename = typename TEnableIf<TNot<TIsSame<MaterialType, UMaterialInstance>>::Value>::Type>
	static TGLTFProxyMaterial Create(EGLTFJsonShadingModel ShadingModel, UObject* Outer = GetTransientPackage(), FName Name = NAME_None, EObjectFlags Flags = RF_NoFlags)
	{
		return TGLTFProxyMaterial(FGLTFProxyMaterialUtilities::CreateProxyMaterial<MaterialType>(ShadingModel, Outer, Name, Flags));
	}

	TGLTFProxyMaterial(MaterialType* Material)
		: Material(Material)
	{
		check(FGLTFProxyMaterialUtilities::IsProxyMaterial(Material));
	}

	MaterialType* GetMaterial() { return Material.Get(); }

	void SetProxy(UMaterialInterface* OriginalMaterial) { FGLTFProxyMaterialUtilities::SetProxyMaterial(OriginalMaterial, GetMaterial()); }

	bool GetTwoSided() { return FGLTFProxyMaterialUtilities::GetTwoSided(GetMaterial()); }
	EBlendMode GetBlendMode() { return FGLTFProxyMaterialUtilities::GetBlendMode(GetMaterial()); }
	float GetOpacityMaskClipValue() { return FGLTFProxyMaterialUtilities::GetOpacityMaskClipValue(GetMaterial()); }

	void SetTwoSided(bool Value, bool NonDefaultOnly = false) { FGLTFProxyMaterialUtilities::SetTwoSided(GetMaterial(), Value, NonDefaultOnly); }
	void SetBlendMode(EBlendMode Value, bool NonDefaultOnly = false) { FGLTFProxyMaterialUtilities::SetBlendMode(GetMaterial(), Value, NonDefaultOnly); }
	void SetOpacityMaskClipValue(float Value, bool NonDefaultOnly = false) { FGLTFProxyMaterialUtilities::SetOpacityMaskClipValue(GetMaterial(), Value, NonDefaultOnly); }

private:

	TStrongObjectPtr<MaterialType> Material;

public:

	const TGLTFProxyMaterialTextureParameter<MaterialType> BaseColor = { FGLTFProxyMaterialInfo::BaseColor, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<FLinearColor, MaterialType> BaseColorFactor = { FGLTFProxyMaterialInfo::BaseColorFactor, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> Emissive = { FGLTFProxyMaterialInfo::Emissive, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<FLinearColor, MaterialType> EmissiveFactor = { FGLTFProxyMaterialInfo::EmissiveFactor, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> MetallicRoughness = { FGLTFProxyMaterialInfo::MetallicRoughness, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> MetallicFactor = { FGLTFProxyMaterialInfo::MetallicFactor, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> RoughnessFactor = { FGLTFProxyMaterialInfo::RoughnessFactor, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> Normal = { FGLTFProxyMaterialInfo::Normal, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> NormalScale = { FGLTFProxyMaterialInfo::NormalScale, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> Occlusion = { FGLTFProxyMaterialInfo::Occlusion, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> OcclusionStrength = { FGLTFProxyMaterialInfo::OcclusionStrength, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> ClearCoat = { FGLTFProxyMaterialInfo::ClearCoat, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> ClearCoatFactor = { FGLTFProxyMaterialInfo::ClearCoatFactor, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> ClearCoatRoughness = { FGLTFProxyMaterialInfo::ClearCoatRoughness, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> ClearCoatRoughnessFactor = { FGLTFProxyMaterialInfo::ClearCoatRoughnessFactor, this->GetMaterial() };

	const TGLTFProxyMaterialTextureParameter<MaterialType> ClearCoatNormal = { FGLTFProxyMaterialInfo::ClearCoatNormal, this->GetMaterial() };
	const TGLTFProxyMaterialParameter<float, MaterialType> ClearCoatNormalScale = { FGLTFProxyMaterialInfo::ClearCoatNormalScale, this->GetMaterial() };
};

typedef TGLTFProxyMaterial<UMaterialInstance, void> FGLTFProxyMaterial;
typedef TGLTFProxyMaterial<UMaterialInstanceDynamic, void> FGLTFProxyMaterialDynamic;
typedef TGLTFProxyMaterial<UMaterialInstanceConstant, void> FGLTFProxyMaterialConstant;
