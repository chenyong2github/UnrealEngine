// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gltf/InterchangeGLTFMaterialInstances.h"
#include "GLTFMaterial.h"
#include "GLTFTexture.h"

#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeTextureNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeShaderGraphNode.h"

#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Async/Async.h"


namespace UE::Interchange::GLTFMaterialInstances
{
	//Precedence order based on the logic in the InterchangeGenericMaterialPipeline: ClearCoat > Sheen > unlit
	EShadingModel GetShadingModel(GLTF::FMaterial::EShadingModel GltfShadingModel, bool bHasClearCoat, bool bHasSheen, bool bUnlit, bool bHasTransmission)
	{
		if (GltfShadingModel == GLTF::FMaterial::EShadingModel::SpecularGlossiness)
		{
			return EShadingModel::SPECULARGLOSSINESS;
		}

		if (bHasClearCoat)
		{
			return EShadingModel::CLEARCOAT;
		}

		if (bHasSheen)
		{
			return EShadingModel::SHEEN;
		}

		if (bUnlit)
		{
			return EShadingModel::UNLIT;
		}

		if (bHasTransmission)
		{
			return EShadingModel::TRANSMISSION;
		}

		return EShadingModel::DEFAULT;
	}

	void AddGltfMaterialValuesToShaderGraphNode(const GLTF::FMaterial& GltfMaterial, const TArray<GLTF::FTexture>& Textures, UInterchangeShaderGraphNode* ShaderGraphNode)
	{
		FString ParentIdentifier = GetIdentifier(GetShadingModel(GltfMaterial.ShadingModel, GltfMaterial.bHasClearCoat, GltfMaterial.bHasSheen, GltfMaterial.bIsUnlitShadingModel, GltfMaterial.bHasTransmission), EAlphaMode(GltfMaterial.AlphaMode), GltfMaterial.bIsDoubleSided);

		ShaderGraphNode->AddStringAttribute(*(InterchangeGltfMaterialAttributeIdentifier + TEXT("ParentIdentifier")), ParentIdentifier);

		auto SetMap = [=](const GLTF::FTextureMap& TextureMap, const FString& Name)
		{
			if (Textures.IsValidIndex(TextureMap.TextureIndex))
			{
				ShaderGraphNode->AddStringAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Name), UInterchangeTextureNode::MakeNodeUid(Textures[TextureMap.TextureIndex].UniqueId));

				//TexCoord decision in MaterialInstances work based on a Switch Node
				//Currently Only supports UV0 and UV1
				// [0...1) -> UV0
				// [1...2) -> UV1
				// else    -> UV0 (defaults to 0)
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Name + TEXT("_TexCoord")), TextureMap.TexCoord);
			}

			if (TextureMap.bHasTextureTransform)
			{
				FVector4f OffsetScale(TextureMap.TextureTransform.Offset[0], TextureMap.TextureTransform.Offset[1], TextureMap.TextureTransform.Scale[0], TextureMap.TextureTransform.Scale[1]);
				ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Name + TEXT("_OffsetScale")), OffsetScale);

				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Name + TEXT("_Rotation")), TextureMap.TextureTransform.Rotation);
			}
		};

		if (GltfMaterial.bIsUnlitShadingModel)
		{
			//BaseColorTexture
			//BaseColorTexture_OffsetScale
			//BaseColorTExture_Rotation
			//BaseColorTExture_TexCoord
			SetMap(GltfMaterial.BaseColor, Parameters::BaseColorTexture);

			//BaseColorFactor
			ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::BaseColorFactor), GltfMaterial.BaseColorFactor);

			return;
		}

		if (GltfMaterial.AlphaMode == GLTF::FMaterial::EAlphaMode::Mask)
		{
			//AlphaCutoff
			ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::AlphaCutoff), GltfMaterial.AlphaCutoff);
		}

		if (GltfMaterial.ShadingModel == GLTF::FMaterial::EShadingModel::MetallicRoughness)
		{
			{
				//BaseColorTexture
				//BaseColorTexture_OffsetScale
				//BaseColorTexture_Rotation
				//BaseColorTexture_TexCoord
				SetMap(GltfMaterial.BaseColor, Parameters::BaseColorTexture);

				//BaseColorFactor
				ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::BaseColorFactor), GltfMaterial.BaseColorFactor);
			}

			{
				//MetallicRoughnessTexture
				//MetallicRoughnessTexture_OffsetScale
				//MetallicRoughnessTexture_Rotation
				//MetallicRoughnessTexture_TexCoord
				SetMap(GltfMaterial.MetallicRoughness.Map, Parameters::MetallicRoughnessTexture);

				//MetallicFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::MetallicFactor), GltfMaterial.MetallicRoughness.MetallicFactor);

				//RoughnessFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::RoughnessFactor), GltfMaterial.MetallicRoughness.RoughnessFactor);
			}

			if (GltfMaterial.bHasSpecular)
			{
				//SpecularTexture
				//SpecularTexture_OffsetScale
				//SpecularTexture_Rotation
				//SpecularTexture_TexCoord
				SetMap(GltfMaterial.Specular.SpecularMap, Parameters::SpecularTexture);

				//SpecularFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::SpecularFactor), GltfMaterial.Specular.SpecularFactor);
			}
		}
		else if (GltfMaterial.ShadingModel == GLTF::FMaterial::EShadingModel::SpecularGlossiness)
		{
			//////////////////////
			//specular glossiness:
			//////////////////////

			{
				//DiffuseTexture
				//DiffuseTexture_OffsetScale
				//DiffuseTexture_Rotation
				//DiffuseTexture_TexCoord
				SetMap(GltfMaterial.BaseColor, Parameters::DiffuseTexture);

				//DiffuseFactor
				ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::DiffuseFactor), GltfMaterial.BaseColorFactor);
			}

			{
				//SpecularGlossinessTexture
				//SpecularGlossinessTexture_OffsetScale
				//SpecularGlossinessTexture_Rotation
				//SpecularGlossinessTexture_TexCoord
				SetMap(GltfMaterial.SpecularGlossiness.Map, Parameters::SpecularGlossinessTexture);

				//SpecFactor
				ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::SpecFactor), FVector4f(GltfMaterial.SpecularGlossiness.SpecularFactor[0],
					GltfMaterial.SpecularGlossiness.SpecularFactor[1],
					GltfMaterial.SpecularGlossiness.SpecularFactor[2]));

				//GlossinessFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::GlossinessFactor), GltfMaterial.SpecularGlossiness.GlossinessFactor);
			}
		}

		{
			//NormalTexture
			//NormalTexture_OffsetScale
			//NormalTexture_Rotation
			//NormalTexture_TexCoord
			SetMap(GltfMaterial.Normal, Parameters::NormalTexture);

			//NormalScale
			ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::NormalScale), GltfMaterial.NormalScale);
		}

		{
			//EmissiveTexture
			//EmissiveTexture_OffsetScale
			//EmissiveTexture_Rotation
			//EmissiveTexture_TexCoord
			SetMap(GltfMaterial.Emissive, Parameters::EmissiveTexture);

			//EmissiveFactor
			ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::EmissiveFactor), GltfMaterial.EmissiveFactor);

			//EmissiveStrength
			ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::EmissiveStrength), GltfMaterial.bHasEmissiveStrength ? GltfMaterial.EmissiveStrength : 1.0f);
		}

		{
			//OcclusionTexture
			//OcclusionTexture_OffsetScale
			//OcclusionTexture_Rotation
			//OcclusionTexture_TexCoord
			SetMap(GltfMaterial.Occlusion, Parameters::OcclusionTexture);

			//OcclusionStrength
			ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::OcclusionStrength), GltfMaterial.OcclusionStrength);
		}

		if (GltfMaterial.bHasIOR)
		{
			//ior
			ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::IOR), GltfMaterial.IOR);
		}

		if (GltfMaterial.bHasClearCoat)
		{
			{
				//ClearCoatTexture
				//ClearCoatTexture_OffsetScale
				//ClearCoatTexture_Rotation
				//ClearCoatTexture_TexCoord
				SetMap(GltfMaterial.ClearCoat.ClearCoatMap, Parameters::ClearCoatTexture);

				//ClearCoatFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::ClearCoatFactor), GltfMaterial.ClearCoat.ClearCoatFactor);
			}

			{
				//ClearCoatRoughnessTexture
				//ClearCoatRoughnessTexture_OffsetScale
				//ClearCoatRoughnessTexture_Rotation
				//ClearCoatRoughnessTexture_TexCoord
				SetMap(GltfMaterial.ClearCoat.RoughnessMap, Parameters::ClearCoatRoughnessTexture);

				//ClearCoatRoughnessFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::ClearCoatRoughnessFactor), GltfMaterial.ClearCoat.Roughness);
			}

			{
				//ClearCoatNormalTexture
				//ClearCoatNormalTexture_OffsetScale
				//ClearCoatNormalTexture_Rotation
				//ClearCoatNormalTexture_TexCoord
				SetMap(GltfMaterial.ClearCoat.NormalMap, Parameters::ClearCoatNormalTexture);

				//ClearCoatNormalFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::ClearCoatNormalScale), GltfMaterial.ClearCoat.NormalMapUVScale);
			}
		}
		else if (GltfMaterial.bHasSheen)
		{
			{
				//SheenColorTexture
				//SheenColorTexture_OffsetScale
				//SheenColorTexture_Rotation
				//SheenColorTexture_TexCoord
				SetMap(GltfMaterial.Sheen.SheenColorMap, Parameters::SheenColorTexture);

				//SheenColorFactor
				ShaderGraphNode->AddLinearColorAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::SheenColorFactor), FVector4f(GltfMaterial.Sheen.SheenColorFactor[0], GltfMaterial.Sheen.SheenColorFactor[1], GltfMaterial.Sheen.SheenColorFactor[2]));

				//SheenRoughnessTexture
				//SheenRoughnessTexture_OffsetScale
				//SheenRoughnessTexture_Rotation
				//SheenRoughnessTexture_TexCoord
				SetMap(GltfMaterial.Sheen.SheenRoughnessMap, Parameters::SheenRoughnessTexture);

				//SheenRoughnessFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::SheenRoughnessFactor), GltfMaterial.Sheen.SheenRoughnessFactor);
			}
		}

		if (GltfMaterial.bHasTransmission)
		{
			{
				//TransmissionTexture
				//TransmissionTexture_OffsetScale
				//TransmissionTexture_Rotation
				//TransmissionTexture_TexCoord
				SetMap(GltfMaterial.Transmission.TransmissionMap, Parameters::TransmissionTexture);

				//TransmissionFactor
				ShaderGraphNode->AddFloatAttribute(*(InterchangeGltfMaterialAttributeIdentifier + Parameters::TransmissionFactor), GltfMaterial.Transmission.TransmissionFactor);
			}
		}
	}
}

