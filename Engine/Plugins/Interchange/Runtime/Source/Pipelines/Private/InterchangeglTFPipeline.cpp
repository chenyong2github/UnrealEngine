// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeglTFPipeline.h"

#include "InterchangePipelineLog.h"

#include "InterchangeMeshFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeMaterialFactoryNode.h"

#include "Gltf/InterchangeGLTFMaterialInstances.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Misc/App.h"

const TArray<FString> UGLTFPipelineSettings::ExpectedMaterialInstanceIdentifiers = {TEXT("MI_Default_Opaque"), TEXT("MI_Default_Mask"), TEXT("MI_Default_Blend"), 
																					TEXT("MI_Unlit_Opaque"), TEXT("MI_Unlit_Mask"), TEXT("MI_Unlit_Blend"), 
																					TEXT("MI_ClearCoat_Opaque"), TEXT("MI_ClearCoat_Mask"), TEXT("MI_ClearCoat_Blend"),
																					TEXT("MI_Sheen_Opaque"), TEXT("MI_Sheen_Mask"), TEXT("MI_Sheen_Blend"), 
																					TEXT("MI_Transmission"), 
																					TEXT("MI_SpecularGlossiness_Opaque"), TEXT("MI_SpecularGlossiness_Mask"), TEXT("MI_SpecularGlossiness_Blend"), 
																					TEXT("MI_Default_Opaque_DS"), TEXT("MI_Default_Mask_DS"), TEXT("MI_Default_Blend_DS"), 
																					TEXT("MI_Unlit_Opaque_DS"), TEXT("MI_Unlit_Mask_DS"), TEXT("MI_Unlit_Blend_DS"), 
																					TEXT("MI_ClearCoat_Opaque_DS"), TEXT("MI_ClearCoat_Mask_DS"), TEXT("MI_ClearCoat_Blend_DS"), 
																					TEXT("MI_Sheen_Opaque_DS"), TEXT("MI_Sheen_Mask_DS"), TEXT("MI_Sheen_Blend_DS"), 
																					TEXT("MI_Transmission_DS"), 
																					TEXT("MI_SpecularGlossiness_Opaque_DS"), TEXT("MI_SpecularGlossiness_Mask_DS"), TEXT("MI_SpecularGlossiness_Blend_DS")};

TArray<FString> UGLTFPipelineSettings::ValidateMaterialInstancesAndParameters() const
{
	TArray<FString> NotCoveredIdentifiersParameters;

	//Check if all Material variations are covered:
	TArray<FString> ExpectedIdentifiers = ExpectedMaterialInstanceIdentifiers;
	TArray<FString> IdentifiersUsed;
	MaterialParents.GetKeys(IdentifiersUsed);
	for (const FString& Identifier : IdentifiersUsed)
	{
		ExpectedIdentifiers.Remove(Identifier);
	}
	for (const FString& ExpectedIdentifier : ExpectedIdentifiers)
	{
		NotCoveredIdentifiersParameters.Add(TEXT("[") + ExpectedIdentifier + TEXT("]: MaterialInstance not found for Identifier."));
	}

	for (const TPair<FString, FSoftObjectPath>& MaterialParent : MaterialParents)
	{
		TSet<FString> ExpectedParameters = GenerateExpectedParametersList(MaterialParent.Key);

		if (UMaterialInstance* ParentMaterialInstance = Cast<UMaterialInstance>(MaterialParent.Value.TryLoad()))
		{
			TArray<FGuid> ParameterIds;
			TArray<FMaterialParameterInfo> ScalarParameterInfos;
			TArray<FMaterialParameterInfo> VectorParameterInfos;
			TArray<FMaterialParameterInfo> TextureParameterInfos;
			ParentMaterialInstance->GetAllScalarParameterInfo(ScalarParameterInfos, ParameterIds);
			ParentMaterialInstance->GetAllVectorParameterInfo(VectorParameterInfos, ParameterIds);
			ParentMaterialInstance->GetAllTextureParameterInfo(TextureParameterInfos, ParameterIds);

			for (const FMaterialParameterInfo& ParameterInfo : ScalarParameterInfos)
			{
				ExpectedParameters.Remove(ParameterInfo.Name.ToString());
			}
			for (const FMaterialParameterInfo& ParameterInfo : VectorParameterInfos)
			{
				ExpectedParameters.Remove(ParameterInfo.Name.ToString());
			}
			for (const FMaterialParameterInfo& ParameterInfo : TextureParameterInfos)
			{
				ExpectedParameters.Remove(ParameterInfo.Name.ToString());
			}
		}

		for (const FString& ExpectedParameter : ExpectedParameters)
		{
			NotCoveredIdentifiersParameters.Add(TEXT("[") + MaterialParent.Key + TEXT("]: Does not cover expected parameter: ") + ExpectedParameter + TEXT("."));
		}
	}

	return NotCoveredIdentifiersParameters;
}

TSet<FString> UGLTFPipelineSettings::GenerateExpectedParametersList(const FString& Identifier) const
{
	using namespace UE::Interchange::GLTFMaterialInstances;

	TSet<FString> ExpectedParameters;

	if (Identifier.Contains(TEXT("_Unlit")))
	{
		ExpectedParameters.Add(Parameters::BaseColorTexture);
		ExpectedParameters.Add(Parameters::BaseColorTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::BaseColorTexture_Rotation);
		ExpectedParameters.Add(Parameters::BaseColorTexture_TexCoord);
		ExpectedParameters.Add(Parameters::BaseColorFactor);

		return ExpectedParameters;
	}

	//Generic ones:
	{
		ExpectedParameters.Add(Parameters::NormalTexture);
		ExpectedParameters.Add(Parameters::NormalTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::NormalTexture_Rotation);
		ExpectedParameters.Add(Parameters::NormalTexture_TexCoord);
		ExpectedParameters.Add(Parameters::NormalScale);

		if (!Identifier.Contains(TEXT("Transmission")))
		{
			ExpectedParameters.Add(Parameters::EmissiveTexture);
			ExpectedParameters.Add(Parameters::EmissiveTexture_OffsetScale);
			ExpectedParameters.Add(Parameters::EmissiveTexture_Rotation);
			ExpectedParameters.Add(Parameters::EmissiveTexture_TexCoord);
			ExpectedParameters.Add(Parameters::EmissiveFactor);
			ExpectedParameters.Add(Parameters::EmissiveStrength);
		}

		ExpectedParameters.Add(Parameters::OcclusionTexture);
		ExpectedParameters.Add(Parameters::OcclusionTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::OcclusionTexture_Rotation);
		ExpectedParameters.Add(Parameters::OcclusionTexture_TexCoord);
		ExpectedParameters.Add(Parameters::OcclusionStrength);

		ExpectedParameters.Add(Parameters::IOR);
	}

	//Based on ShadingModel:

	if (Identifier.Contains(TEXT("Default")))
	{
		//MetalRoughness Specific:

		ExpectedParameters.Add(Parameters::BaseColorTexture);
		ExpectedParameters.Add(Parameters::BaseColorTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::BaseColorTexture_Rotation);
		ExpectedParameters.Add(Parameters::BaseColorTexture_TexCoord);
		ExpectedParameters.Add(Parameters::BaseColorFactor);

		ExpectedParameters.Add(Parameters::MetallicRoughnessTexture);
		ExpectedParameters.Add(Parameters::MetallicRoughnessTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::MetallicRoughnessTexture_Rotation);
		ExpectedParameters.Add(Parameters::MetallicRoughnessTexture_TexCoord);
		ExpectedParameters.Add(Parameters::MetallicFactor);
		ExpectedParameters.Add(Parameters::RoughnessFactor);

		ExpectedParameters.Add(Parameters::SpecularTexture);
		ExpectedParameters.Add(Parameters::SpecularTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::SpecularTexture_Rotation);
		ExpectedParameters.Add(Parameters::SpecularTexture_TexCoord);
		ExpectedParameters.Add(Parameters::SpecularFactor);

	}
	else if (Identifier.Contains(TEXT("ClearCoat")))
	{
		ExpectedParameters.Add(Parameters::ClearCoatTexture);
		ExpectedParameters.Add(Parameters::ClearCoatTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::ClearCoatTexture_Rotation);
		ExpectedParameters.Add(Parameters::ClearCoatTexture_TexCoord);
		ExpectedParameters.Add(Parameters::ClearCoatFactor);

		ExpectedParameters.Add(Parameters::ClearCoatRoughnessTexture);
		ExpectedParameters.Add(Parameters::ClearCoatRoughnessTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::ClearCoatRoughnessTexture_Rotation);
		ExpectedParameters.Add(Parameters::ClearCoatRoughnessTexture_TexCoord);
		ExpectedParameters.Add(Parameters::ClearCoatRoughnessFactor);

		ExpectedParameters.Add(Parameters::ClearCoatNormalTexture);
		ExpectedParameters.Add(Parameters::ClearCoatNormalTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::ClearCoatNormalTexture_Rotation);
		ExpectedParameters.Add(Parameters::ClearCoatNormalTexture_TexCoord);
		ExpectedParameters.Add(Parameters::ClearCoatNormalScale);
	}
	else if (Identifier.Contains(TEXT("Sheen")))
	{
		ExpectedParameters.Add(Parameters::SheenColorTexture);
		ExpectedParameters.Add(Parameters::SheenColorTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::SheenColorTexture_Rotation);
		ExpectedParameters.Add(Parameters::SheenColorTexture_TexCoord);
		ExpectedParameters.Add(Parameters::SheenColorFactor);

		ExpectedParameters.Add(Parameters::SheenRoughnessTexture);
		ExpectedParameters.Add(Parameters::SheenRoughnessTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::SheenRoughnessTexture_Rotation);
		ExpectedParameters.Add(Parameters::SheenRoughnessTexture_TexCoord);
		ExpectedParameters.Add(Parameters::SheenRoughnessFactor);
	}
	else if (Identifier.Contains(TEXT("Transmission")))
	{
		ExpectedParameters.Add(Parameters::TransmissionTexture);
		ExpectedParameters.Add(Parameters::TransmissionTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::TransmissionTexture_Rotation);
		ExpectedParameters.Add(Parameters::TransmissionTexture_TexCoord);
		ExpectedParameters.Add(Parameters::TransmissionFactor);
	}
	else if (Identifier.Contains(TEXT("SpecularGlossiness")))
	{
		ExpectedParameters.Add(Parameters::DiffuseTexture);
		ExpectedParameters.Add(Parameters::DiffuseTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::DiffuseTexture_Rotation);
		ExpectedParameters.Add(Parameters::DiffuseTexture_TexCoord);
		ExpectedParameters.Add(Parameters::DiffuseFactor);

		ExpectedParameters.Add(Parameters::SpecularGlossinessTexture);
		ExpectedParameters.Add(Parameters::SpecularGlossinessTexture_OffsetScale);
		ExpectedParameters.Add(Parameters::SpecularGlossinessTexture_Rotation);
		ExpectedParameters.Add(Parameters::SpecularGlossinessTexture_TexCoord);
		ExpectedParameters.Add(Parameters::SpecFactor);
		ExpectedParameters.Add(Parameters::GlossinessFactor);
	}

	return ExpectedParameters;
}

void UGLTFPipelineSettings::BuildMaterialInstance(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode)
{
	using namespace UE::Interchange::GLTFMaterialInstances;

	TArray<UE::Interchange::FAttributeKey> AttributeKeys;
	ShaderGraphNode->GetAttributeKeys(AttributeKeys);

	TMap<FString, UE::Interchange::FAttributeKey> GltfAttributeKeys;
	for (const UE::Interchange::FAttributeKey& AttributeKey : AttributeKeys)
	{
		if (AttributeKey.ToString().Contains(InterchangeGltfMaterialAttributeIdentifier))
		{
			GltfAttributeKeys.Add(AttributeKey.ToString().Replace(*InterchangeGltfMaterialAttributeIdentifier, TEXT(""), ESearchCase::CaseSensitive), AttributeKey);
		}
	}
	if (GltfAttributeKeys.Num() == 0)
	{
		return;
	}

	FString ParentIdentifier;
	if (!ShaderGraphNode->GetStringAttribute(*(InterchangeGltfMaterialAttributeIdentifier + TEXT("ParentIdentifier")), ParentIdentifier))
	{
		return;
	}
	GltfAttributeKeys.Remove(TEXT("ParentIdentifier"));

	FString Parent;
	if (const FSoftObjectPath* ObjectPath = MaterialParents.Find(ParentIdentifier))
	{
		Parent = ObjectPath->GetAssetPathString();
	}
	else
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("[Interchange] Failed to load MaterialParent for ParentIdentifier: %s"), *ParentIdentifier);
		return;
	}

	FString MaterialFactoryNodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(ShaderGraphNode->GetUniqueID());
	FString MaterialFactoryNodeName = ShaderGraphNode->GetDisplayLabel();

	MaterialInstanceFactoryNode->InitializeNode(MaterialFactoryNodeUid, MaterialFactoryNodeName, EInterchangeNodeContainerType::FactoryData);

	MaterialInstanceFactoryNode->SetCustomParent(Parent);

	const UClass* MaterialClass = FApp::IsGame() ? UMaterialInstanceDynamic::StaticClass() : UMaterialInstanceConstant::StaticClass();
	MaterialInstanceFactoryNode->SetCustomInstanceClassName(MaterialClass->GetPathName());

	for (const TPair<FString, UE::Interchange::FAttributeKey>& GltfAttributeKey : GltfAttributeKeys)
	{
		UE::Interchange::EAttributeTypes AttributeType = ShaderGraphNode->GetAttributeType(GltfAttributeKey.Value);

		FString InputValueKey = UInterchangeShaderPortsAPI::MakeInputValueKey(GltfAttributeKey.Key);

		//we are only using 3 attribute types:
		switch (AttributeType)
		{
		case UE::Interchange::EAttributeTypes::Float:
		{
			float Value;
			if (ShaderGraphNode->GetFloatAttribute(GltfAttributeKey.Value.Key, Value))
			{
				MaterialInstanceFactoryNode->AddFloatAttribute(InputValueKey, Value);
			}
		}
		break;
		case UE::Interchange::EAttributeTypes::LinearColor:
		{
			FLinearColor Value;
			if (ShaderGraphNode->GetLinearColorAttribute(GltfAttributeKey.Value.Key, Value))
			{
				MaterialInstanceFactoryNode->AddLinearColorAttribute(InputValueKey, Value);
			}
		}
		break;
		case UE::Interchange::EAttributeTypes::String:
		{
			FString TextureUid;
			if (ShaderGraphNode->GetStringAttribute(GltfAttributeKey.Value.Key, TextureUid))
			{
				FString FactoryTextureUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(TextureUid);

				MaterialInstanceFactoryNode->AddStringAttribute(InputValueKey, FactoryTextureUid);
				MaterialInstanceFactoryNode->AddFactoryDependencyUid(FactoryTextureUid);
			}
		}
		break;
		default:
			break;
		}
	}
}

UInterchangeglTFPipeline::UInterchangeglTFPipeline()
	: GLTFPipelineSettings(UGLTFPipelineSettings::StaticClass()->GetDefaultObject<UGLTFPipelineSettings>())
{
}

void UInterchangeglTFPipeline::AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset)
{
	Super::AdjustSettingsForContext(ImportType, ReimportAsset);

	TArray<FString> MaterialInstanceIssues = GLTFPipelineSettings->ValidateMaterialInstancesAndParameters();
	for (const FString& MaterialInstanceIssue : MaterialInstanceIssues)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("%s"), *MaterialInstanceIssue);
	}
}

void UInterchangeglTFPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* NodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	Super::ExecutePipeline(NodeContainer, InSourceDatas);

	if ((FApp::IsGame() || bUseGLTFMaterialInstanceLibrary) && GLTFPipelineSettings)
	{
		TMap<FString, const UInterchangeShaderGraphNode*> MaterialFactoryNodeUidsToShaderGraphNodes;
		auto FindGLTFShaderGraphNode = [&MaterialFactoryNodeUidsToShaderGraphNodes, &NodeContainer](const FString& NodeUid, UInterchangeFactoryBaseNode* /*Material or MaterialInstance*/ FactoryNode)
		{
			TArray<FString> TargetNodeUids;
			FactoryNode->GetTargetNodeUids(TargetNodeUids);

			for (const FString& TargetNodeUid : TargetNodeUids)
			{

				if (const UInterchangeShaderGraphNode* ShaderGraphNode = Cast<UInterchangeShaderGraphNode>(NodeContainer->GetNode(TargetNodeUid)))
				{
					FString ParentIdentifier;
					if (ShaderGraphNode->GetStringAttribute(*(InterchangeGltfMaterialAttributeIdentifier + TEXT("ParentIdentifier")), ParentIdentifier))
					{
						MaterialFactoryNodeUidsToShaderGraphNodes.Add(NodeUid, ShaderGraphNode);
						break;
					}
				}
			}
		};
		NodeContainer->IterateNodesOfType<UInterchangeMaterialFactoryNode>([&MaterialFactoryNodeUidsToShaderGraphNodes, &NodeContainer, &FindGLTFShaderGraphNode](const FString& NodeUid, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
			{
				FindGLTFShaderGraphNode(NodeUid, MaterialFactoryNode);
			});

		NodeContainer->IterateNodesOfType<UInterchangeMaterialInstanceFactoryNode>([&MaterialFactoryNodeUidsToShaderGraphNodes, &NodeContainer, &FindGLTFShaderGraphNode](const FString& NodeUid, UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode)
			{
				FindGLTFShaderGraphNode(NodeUid, MaterialInstanceFactoryNode);
			});

		for (const TPair<FString, const UInterchangeShaderGraphNode*>& ShaderGraphNode : MaterialFactoryNodeUidsToShaderGraphNodes)
		{
			UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode = NewObject<UInterchangeMaterialInstanceFactoryNode>(NodeContainer);
			GLTFPipelineSettings->BuildMaterialInstance(ShaderGraphNode.Value, MaterialInstanceFactoryNode);

			NodeContainer->ReplaceNode(ShaderGraphNode.Key, MaterialInstanceFactoryNode);
		}
	}
}

