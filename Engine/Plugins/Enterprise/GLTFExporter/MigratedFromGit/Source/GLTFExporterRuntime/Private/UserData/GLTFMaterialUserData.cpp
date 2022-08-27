// Copyright Epic Games, Inc. All Rights Reserved.

#include "UserData/GLTFMaterialUserData.h"
#include "Materials/MaterialInterface.h"

UGLTFMaterialUserData::UGLTFMaterialUserData()
	: DefaultBakeSize(EGLTFOverrideMaterialBakeSizePOT::POT_1024)
{
}

EGLTFOverrideMaterialBakeSizePOT UGLTFMaterialUserData::GetBakeSizeForProperty(EMaterialProperty Property) const
{
	const EGLTFOverrideMaterialPropertyGroup PropertyGroup = GetPropertyGroup(Property);
	if (const EGLTFOverrideMaterialBakeSizePOT* PropertyBakeSize = BakeSizePerProperty.Find(PropertyGroup))
	{
		return *PropertyBakeSize;
	}

	return DefaultBakeSize;
}

enum class EGLTFOverrideMater : uint8
{
	None UMETA(DisplayName = "None"),

    BaseColorOpacity UMETA(DisplayName = "Base Color + Opacity (Mask)"),
    MetallicRoughness UMETA(DisplayName = "Metallic + Roughness"),
    EmissiveColor UMETA(DisplayName = "Emissive Color"),
    Normal UMETA(DisplayName = "Normal"),
    AmbientOcclusion UMETA(DisplayName = "Ambient Occlusion"),
    ClearCoatRoughness UMETA(DisplayName = "Clear Coat + Clear Coat Roughness"),
    ClearCoatBottomNormal UMETA(DisplayName = "Clear Coat Bottom Normal"),
};

EGLTFOverrideMaterialPropertyGroup UGLTFMaterialUserData::GetPropertyGroup(EMaterialProperty Property)
{
	switch (Property)
	{
		case MP_BaseColor:
		case MP_Opacity:
		case MP_OpacityMask:
			return EGLTFOverrideMaterialPropertyGroup::BaseColorOpacity;
		case MP_Metallic:
		case MP_Roughness:
			return EGLTFOverrideMaterialPropertyGroup::MetallicRoughness;
		case MP_EmissiveColor:
			return EGLTFOverrideMaterialPropertyGroup::EmissiveColor;
		case MP_Normal:
			return EGLTFOverrideMaterialPropertyGroup::Normal;
		case MP_AmbientOcclusion:
			return EGLTFOverrideMaterialPropertyGroup::AmbientOcclusion;
		case MP_CustomData0:
		case MP_CustomData1:
			return EGLTFOverrideMaterialPropertyGroup::ClearCoatRoughness;
		case MP_CustomOutput: // TODO: fix assumption
			return EGLTFOverrideMaterialPropertyGroup::ClearCoatBottomNormal;
		default:
			return EGLTFOverrideMaterialPropertyGroup::None;
	}
}

const UGLTFMaterialUserData* UGLTFMaterialUserData::GetUserData(const UMaterialInterface* Material)
{
	return const_cast<UMaterialInterface*>(Material)->GetAssetUserData<UGLTFMaterialUserData>();
}
