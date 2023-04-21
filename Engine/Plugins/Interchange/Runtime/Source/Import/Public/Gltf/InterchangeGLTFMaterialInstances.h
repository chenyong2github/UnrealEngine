// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace GLTF {
	struct FMaterial;
	struct FTexture;
}
class UInterchangeShaderGraphNode;

const FString InterchangeGltfMaterialAttributeIdentifier = TEXT("Gltf_MI_AttributeIdentifier_");

#define INTERCHANGE_GLTF_STRINGIFY(x) #x
#define DECLARE_INTERCHANGE_GLTF_MI_MAP(MapName) const FString MapName##Texture = TEXT(INTERCHANGE_GLTF_STRINGIFY(MapName ## Texture)); \
												 const FString MapName##Texture_OffsetScale = TEXT(INTERCHANGE_GLTF_STRINGIFY(MapName ## Texture_OffsetScale)); \
												 const FString MapName##Texture_Rotation = TEXT(INTERCHANGE_GLTF_STRINGIFY(MapName ## Texture_Rotation)); \
												 const FString MapName##Texture_TexCoord = TEXT(INTERCHANGE_GLTF_STRINGIFY(MapName ## Texture_TexCoord));

namespace UE::Interchange::GLTFMaterialInstances
{
	namespace Parameters
	{
		//MetalRoughness specific:
		DECLARE_INTERCHANGE_GLTF_MI_MAP(BaseColor)
		const FString BaseColorFactor = TEXT("BaseColorFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(MetallicRoughness)
		const FString MetallicFactor = TEXT("MetallicFactor");
		const FString RoughnessFactor = TEXT("RoughnessFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(Specular)
		const FString SpecularFactor = TEXT("SpecularFactor");


		//SpecularGlossiness specific
		DECLARE_INTERCHANGE_GLTF_MI_MAP(Diffuse)
		const FString DiffuseFactor = TEXT("DiffuseFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(SpecularGlossiness)
		const FString SpecFactor = TEXT("SpecFactor");
		const FString GlossinessFactor = TEXT("GlossinessFactor");


		//Generic:
		DECLARE_INTERCHANGE_GLTF_MI_MAP(Normal)
		const FString NormalScale = TEXT("NormalScale");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(Emissive)
		const FString EmissiveFactor = TEXT("EmissiveFactor");
		const FString EmissiveStrength = TEXT("EmissiveStrength");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(Occlusion)
		const FString OcclusionStrength = TEXT("OcclusionStrength");

		const FString IOR = TEXT("IOR");

		const FString AlphaCutoff = TEXT("AlphaCutoff");


		//ClearCoat specific:
		DECLARE_INTERCHANGE_GLTF_MI_MAP(ClearCoat)
		const FString ClearCoatFactor = TEXT("ClearCoatFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(ClearCoatRoughness)
		const FString ClearCoatRoughnessFactor = TEXT("ClearCoatRoughnessFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(ClearCoatNormal)
		const FString ClearCoatNormalScale = TEXT("ClearCoatNormalScale");


		//Sheen specific:
		DECLARE_INTERCHANGE_GLTF_MI_MAP(SheenColor)
		const FString SheenColorFactor = TEXT("SheenColorFactor");

		DECLARE_INTERCHANGE_GLTF_MI_MAP(SheenRoughness)
		const FString SheenRoughnessFactor = TEXT("SheenRoughnessFactor");


		//Transmission specific:
		DECLARE_INTERCHANGE_GLTF_MI_MAP(Transmission)
		const FString TransmissionFactor = TEXT("TransmissionFactor");
	}
	


	enum EShadingModel : uint8
	{
		DEFAULT = 0, //MetalRoughness
		UNLIT,
		CLEARCOAT,
		SHEEN,
		TRANSMISSION,
		SPECULARGLOSSINESS,
		SHADINGMODELCOUNT
	};

	enum EAlphaMode : uint8
	{
		Opaque = 0,
		Mask,
		Blend
	};

	inline FString ToString(EShadingModel ShadingModel)
	{
		switch (ShadingModel)
		{
		case DEFAULT:
			return TEXT("Default");
		case UNLIT:
			return TEXT("Unlit");
		case CLEARCOAT:
			return TEXT("ClearCoat");
		case SHEEN:
			return TEXT("Sheen");
		case TRANSMISSION:
			return TEXT("Transmission");
		case SPECULARGLOSSINESS:
			return TEXT("SpecularGlossiness");
		default:
			return TEXT("Unexpected");
		}
	}

	inline FString ToString(EAlphaMode AlphaMode)
	{
		switch (AlphaMode)
		{
		case EAlphaMode::Opaque:
			return TEXT("Opaque");
		case EAlphaMode::Mask:
			return TEXT("Mask");
		case EAlphaMode::Blend:
			return TEXT("Blend");
		default:
			return TEXT("Unexpected");
		}
	}

	inline FString GetIdentifier(EShadingModel ShadingModel, EAlphaMode AlphaMode, bool bTwoSided)
	{
		FString Name = TEXT("MI_");

		Name += ToString(ShadingModel);

		if (ShadingModel != EShadingModel::TRANSMISSION)
		{
			Name += TEXT("_") + ToString(AlphaMode);
		}

		Name += bTwoSided ? TEXT("_DS") : TEXT("");

		return Name;
	}

	void AddGltfMaterialValuesToShaderGraphNode(const GLTF::FMaterial& GltfMaterial, const TArray<GLTF::FTexture>& Textures, UInterchangeShaderGraphNode* ShaderGraphNode);
};
