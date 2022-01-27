// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericMaterialPipeline.h"

#include "CoreMinimal.h"

#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSourceData.h"

#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

void UInterchangeGenericMaterialPipeline::ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMaterialPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}
	
	//Find all translated node we need for this pipeline
	BaseNodeContainer->IterateNodes([this](const FString& NodeUid, UInterchangeBaseNode* Node)
	{
		switch(Node->GetNodeContainerType())
		{
			case EInterchangeNodeContainerType::TranslatedAsset:
			{
				if (UInterchangeShaderGraphNode* MaterialNode = Cast<UInterchangeShaderGraphNode>(Node))
				{
					MaterialNodes.Add(MaterialNode);
				}
			}
			break;
		}
	});

	//import materials
	if (MaterialImport == EInterchangeMaterialImportOption::ImportAsMaterials)
	{
		for (const UInterchangeShaderGraphNode* ShaderGraphNode : MaterialNodes)
		{
			if (UInterchangeMaterialFactoryNode* MaterialFactoryNode = CreateMaterialFactoryNode(ShaderGraphNode))
			{
				//By default we do not create the materials, every node with mesh attribute can enable them. So we wont create unused materials.
				MaterialFactoryNode->SetEnabled(false);
			}
		}
	}

	return;
}

UInterchangeBaseMaterialFactoryNode* UInterchangeGenericMaterialPipeline::CreateBaseMaterialFactoryNode(const UInterchangeBaseNode* MaterialNode, TSubclassOf<UInterchangeBaseMaterialFactoryNode> NodeType)
{
	FString DisplayLabel = MaterialNode->GetDisplayLabel();
	const FString NodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialNode->GetUniqueID());
	UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		//The node already exist, just return it
		MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(BaseNodeContainer->GetNode(NodeUid));
		if (!ensure(MaterialFactoryNode))
		{
			//Log an error
		}
	}
	else
	{
		MaterialFactoryNode = NewObject<UInterchangeBaseMaterialFactoryNode>(BaseNodeContainer, NodeType.Get(), NAME_None);
		if (!ensure(MaterialFactoryNode))
		{
			return nullptr;
		}
		//Creating a Material
		MaterialFactoryNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::FactoryData);
		
		BaseNodeContainer->AddNode(MaterialFactoryNode);
		MaterialFactoryNodes.Add(MaterialFactoryNode);
		MaterialFactoryNode->AddTargetNodeUid(MaterialNode->GetUniqueID());
		MaterialNode->AddTargetNodeUid(MaterialFactoryNode->GetUniqueID());
	}
	return MaterialFactoryNode;
}

bool UInterchangeGenericMaterialPipeline::HandlePhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Phong;

	const bool bHasDiffuseInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::DiffuseColor);
	const bool bHasSpecularInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SpecularColor);

	if (bHasDiffuseInput && bHasSpecularInput)
	{
		// ConvertFromDiffSpec function call
		UInterchangeMaterialExpressionFactoryNode* FunctionCallExpression = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer, NAME_None);
		FunctionCallExpression->SetCustomExpressionClassName(UMaterialExpressionMaterialFunctionCall::StaticClass()->GetName());
		FString FunctionCallExpressionUid = MaterialFactoryNode->GetUniqueID() + TEXT("\\Inputs\\BaseColor\\DiffSpecFunc");
		FunctionCallExpression->InitializeNode(FunctionCallExpressionUid, TEXT("DiffSpecFunc"), EInterchangeNodeContainerType::FactoryData);

		BaseNodeContainer->AddNode(FunctionCallExpression);
		BaseNodeContainer->SetNodeParentUid(FunctionCallExpressionUid, MaterialFactoryNode->GetUniqueID());

		const FName MaterialFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialFunctionCall, MaterialFunction);

		FunctionCallExpression->AddStringAttribute(
			MaterialFunctionMemberName.ToString(),
			TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions01/Shading/ConvertFromDiffSpec.ConvertFromDiffSpec'"));
		FunctionCallExpression->AddApplyAndFillDelegates<FString>(MaterialFunctionMemberName.ToString(), UMaterialExpressionMaterialFunctionCall::StaticClass(), MaterialFunctionMemberName);

		MaterialFactoryNode->ConnectOutputToBaseColor(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::BaseColor.ToString());
		MaterialFactoryNode->ConnectOutputToMetallic(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::Metallic.ToString());
		MaterialFactoryNode->ConnectOutputToSpecular(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::Specular.ToString());
		
		// Diffuse
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> DiffuseExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::DiffuseColor.ToString(), FunctionCallExpression->GetUniqueID());

			if (DiffuseExpressionFactoryNode.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInput(FunctionCallExpression, TEXT("DiffuseColor"),
					DiffuseExpressionFactoryNode.Get<0>()->GetUniqueID(), DiffuseExpressionFactoryNode.Get<1>());
			}
		}

		// Specular
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> SpecularExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SpecularColor.ToString(), FunctionCallExpression->GetUniqueID());

			if (SpecularExpressionFactoryNode.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInput(FunctionCallExpression, TEXT("SpecularColor"),
					SpecularExpressionFactoryNode.Get<0>()->GetUniqueID(), SpecularExpressionFactoryNode.Get<1>());
			}
		}
		
		// Shininess
		{
			const bool bHasShininessInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Shininess);
			if (bHasShininessInput)
			{
				TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ShininessExpressionFactoryNode =
					CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Shininess.ToString(), MaterialFactoryNode->GetUniqueID());

				if (ShininessExpressionFactoryNode.Get<0>())
				{
					UInterchangeMaterialExpressionFactoryNode* InverseShininessNode =
						CreateExpressionNode(TEXT("InverseShininess"), ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), UMaterialExpressionOneMinus::StaticClass());

					UInterchangeShaderPortsAPI::ConnectOuputToInput(InverseShininessNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionOneMinus, Input).ToString(),
						ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), ShininessExpressionFactoryNode.Get<1>());

					MaterialFactoryNode->ConnectToRoughness(InverseShininessNode->GetUniqueID());
				}
			}
		}

		return true;
	}

	return false;
}

bool UInterchangeGenericMaterialPipeline::HandleLambertModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Lambert;

	// Diffuse
	const bool bHasDiffuseInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::DiffuseColor);

	if (bHasDiffuseInput)
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> DiffuseExpressionFactoryNode =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::DiffuseColor.ToString(), MaterialFactoryNode->GetUniqueID());

		if (DiffuseExpressionFactoryNode.Get<0>())
		{
			MaterialFactoryNode->ConnectOutputToBaseColor(DiffuseExpressionFactoryNode.Get<0>()->GetUniqueID(), DiffuseExpressionFactoryNode.Get<1>());
		}

		return true;
	}

	return false;
}

bool UInterchangeGenericMaterialPipeline::HandlePBRModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::PBR;

	// BaseColor
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::BaseColor);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::BaseColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToBaseColor(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	// Metallic
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Metallic);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Metallic.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToMetallic(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	// Specular
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Specular);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Specular.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToSpecular(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	// Roughness
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Roughness);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Roughness.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToRoughness(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	return false;
}

bool UInterchangeGenericMaterialPipeline::HandleCommonParameters(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Common;

	// Emissive
	{
		const bool bHasEmissiveInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::EmissiveColor);

		if (bHasEmissiveInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> EmissiveExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::EmissiveColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if (EmissiveExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToEmissiveColor(EmissiveExpressionFactoryNode.Get<0>()->GetUniqueID(), EmissiveExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	// Normal
	{
		const bool bHasNormalInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Normal);

		if (bHasNormalInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Normal.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToNormal(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	// Opacity
	{
		const bool bHasOpacityInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Opacity);

		if (bHasOpacityInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> OpacityExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Opacity.ToString(), MaterialFactoryNode->GetUniqueID());

			if (OpacityExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToOpacity(OpacityExpressionFactoryNode.Get<0>()->GetUniqueID(), OpacityExpressionFactoryNode.Get<1>());
			}

			return true;
		}
	}

	return false;
}

void UInterchangeGenericMaterialPipeline::HandleTextureSampleNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	FString TextureUid;
	ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::TextureSample::Inputs::Texture.ToString()), TextureUid);

	FString ExpressionClassName;

	if (UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
	{
		if (TextureNode->IsA<UInterchangeTextureCubeNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameterCube::StaticClass()->GetName();
		}
		else if (TextureNode->IsA<UInterchangeTexture2DArrayNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2DArray::StaticClass()->GetName();
		}
		else if (TextureNode->IsA<UInterchangeTexture2DNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2D::StaticClass()->GetName();
		}
		else
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2D::StaticClass()->GetName();
		}
	}

	TextureSampleFactoryNode->SetCustomExpressionClassName(ExpressionClassName);

	HandleTextureCoordinates(ShaderNode, TextureSampleFactoryNode);
}

void UInterchangeGenericMaterialPipeline::HandleTextureCoordinates(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	float UTilingValue = 1.f;
	bool bHasUTiling = ShaderNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::TextureSample::Inputs::UTiling.ToString()), UTilingValue);
	bHasUTiling = bHasUTiling && (UTilingValue != GetDefault<UMaterialExpressionTextureCoordinate>()->UTiling);

	float VTilingValue = 1.f;
	bool bHasVTiling = ShaderNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::TextureSample::Inputs::VTiling.ToString()), VTilingValue);
	bHasVTiling = bHasVTiling && (VTilingValue != GetDefault<UMaterialExpressionTextureCoordinate>()->VTiling);

	if (bHasUTiling || bHasVTiling)
	{
		UInterchangeMaterialExpressionFactoryNode* TextureCoordinate = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer);
		const FString TextureCoordinateUid = TextureSampleFactoryNode->GetUniqueID() + TEXT("Coordinate");

		TextureCoordinate->InitializeNode(TextureCoordinateUid, ShaderNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
		BaseNodeContainer->AddNode(TextureCoordinate);
		BaseNodeContainer->SetNodeParentUid(TextureCoordinateUid, TextureSampleFactoryNode->GetUniqueID());

		TextureCoordinate->SetCustomExpressionClassName(UMaterialExpressionTextureCoordinate::StaticClass()->GetName());

		TextureCoordinate->AddFloatAttribute(GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, UTiling).ToString(), UTilingValue);
		TextureCoordinate->AddFloatAttribute(GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, VTiling).ToString(), VTilingValue);

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(TextureSampleFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, Coordinates).ToString(), TextureCoordinate->GetUniqueID());
	}
}

void UInterchangeGenericMaterialPipeline::HandleLerpNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* LerpFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	LerpFactoryNode->SetCustomExpressionClassName(UMaterialExpressionLinearInterpolate::StaticClass()->GetName());

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorAExpression =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::A.ToString(), LerpFactoryNode->GetUniqueID());

	if (ColorAExpression.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInput(LerpFactoryNode, Nodes::Lerp::Inputs::A.ToString(), ColorAExpression.Get<0>()->GetUniqueID(), ColorAExpression.Get<1>());
	}

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorBExpression =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::B.ToString(), LerpFactoryNode->GetUniqueID());

	if (ColorBExpression.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInput(LerpFactoryNode, Nodes::Lerp::Inputs::B.ToString(), ColorBExpression.Get<0>()->GetUniqueID(), ColorBExpression.Get<1>());
	}

	float LerpFactor = 0.5f;
	ShaderNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::Lerp::Inputs::Factor.ToString()), LerpFactor);

	const FName ConstAlphaMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, ConstAlpha);
	LerpFactoryNode->AddFloatAttribute(ConstAlphaMemberName.ToString(), LerpFactor);
	LerpFactoryNode->AddApplyAndFillDelegates<float>(ConstAlphaMemberName.ToString(), UMaterialExpressionLinearInterpolate::StaticClass(), ConstAlphaMemberName);
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialExpressionForShaderNode(UInterchangeMaterialFactoryNode* MaterialFactoryNode,
	const UInterchangeShaderNode* ShaderNode, const FString& ParentUid)
{
	using namespace UE::Interchange::Materials::Standard;

	// If we recognize the shader node type
	// - Create material expression for specific node type
	//
	// If we don't recognize the shader node type
	// - Create material expression by trying to match the node type to a material expression class name

	const FString MaterialExpressionUid = TEXT("Factory_") + ShaderNode->GetUniqueID();

	UInterchangeMaterialExpressionFactoryNode* MaterialExpression = Cast<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer->GetNode(MaterialExpressionUid));
	if (MaterialExpression != nullptr)
	{
		return MaterialExpression;
	}

	MaterialExpression = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer);
	MaterialExpression->InitializeNode(MaterialExpressionUid, ShaderNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
	BaseNodeContainer->AddNode(MaterialExpression);

	FString ShaderType;
	ShaderNode->GetCustomShaderType(ShaderType);

	if (*ShaderType == Nodes::TextureSample::Name)
	{
		HandleTextureSampleNode(ShaderNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::Lerp::Name)
	{
		HandleLerpNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else
	{
		const FString ExpressionClassName = TEXT("MaterialExpression") + ShaderType;
		MaterialExpression->SetCustomExpressionClassName(ExpressionClassName);

		TArray<FString> Inputs;
		UInterchangeShaderPortsAPI::GatherInputs(ShaderNode, Inputs);

		for (const FString& InputName : Inputs)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> InputExpression =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, InputName, MaterialExpressionUid);

			if (InputExpression.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInput(MaterialExpression, InputName, InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
			}
		}
	}

	if (!ParentUid.IsEmpty())
	{
		BaseNodeContainer->SetNodeParentUid(MaterialExpressionUid, ParentUid);
	}

	MaterialExpression->AddTargetNodeUid(ShaderNode->GetUniqueID());

	if (*ShaderType == Nodes::TextureSample::Name)
	{
		FString TextureUid;
		ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::TextureSample::Inputs::Texture.ToString()), TextureUid);

		// Make the material factory node have a dependency on the texture factory node so that the texture asset gets created first
		if (UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
		{
			TArray<FString> TextureNodeTargets;
			TextureNode->GetTargetNodeUids(TextureNodeTargets);

			if (TextureNodeTargets.Num() > 0)
			{
				FString TextureFactoryNodeUid = TextureNodeTargets[0];

				if (BaseNodeContainer->IsNodeUidValid(TextureFactoryNodeUid))
				{
					TArray<FString> FactoryDependencies;
					MaterialFactoryNode->GetFactoryDependencies(FactoryDependencies);
					if (!FactoryDependencies.Contains(TextureFactoryNodeUid))
					{
						MaterialFactoryNode->AddFactoryDependencyUid(TextureFactoryNodeUid);
					}
				}
			}
		}
	}

	return MaterialExpression;
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateExpressionNode(const FString& ExpressionName, const FString& ParentUid, UClass* MaterialExpressionClass)
{
	const FString MaterialExpressionUid = ParentUid + TEXT("\\") + ExpressionName;

	UInterchangeMaterialExpressionFactoryNode* MaterialExpressionFactoryNode = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer);
	MaterialExpressionFactoryNode->SetCustomExpressionClassName(MaterialExpressionClass->GetName());
	MaterialExpressionFactoryNode->InitializeNode(MaterialExpressionUid, ExpressionName, EInterchangeNodeContainerType::FactoryData);
	BaseNodeContainer->AddNode(MaterialExpressionFactoryNode);
	BaseNodeContainer->SetNodeParentUid(MaterialExpressionUid, ParentUid);

	return MaterialExpressionFactoryNode;
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateScalarParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
{
	UInterchangeMaterialExpressionFactoryNode* MaterialExpressionFactoryNode = CreateExpressionNode(InputName, ParentUid, UMaterialExpressionScalarParameter::StaticClass());

	float InputValue;
	if (ShaderNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue))
	{
		const FName DefaultValueMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionScalarParameter, DefaultValue);
		MaterialExpressionFactoryNode->AddFloatAttribute(DefaultValueMemberName.ToString(), InputValue);
		MaterialExpressionFactoryNode->AddApplyAndFillDelegates<float>(DefaultValueMemberName.ToString(), UMaterialExpressionScalarParameter::StaticClass(), DefaultValueMemberName);
	}

	return MaterialExpressionFactoryNode;
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateVectorParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
{
	UInterchangeMaterialExpressionFactoryNode* MaterialExpressionFactoryNode = CreateExpressionNode(InputName, ParentUid, UMaterialExpressionVectorParameter::StaticClass());

	FLinearColor InputValue;
	if (ShaderNode->GetLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue))
	{
		const FName DefaultValueMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorParameter, DefaultValue);
		MaterialExpressionFactoryNode->AddLinearColorAttribute(DefaultValueMemberName.ToString(), InputValue);
		MaterialExpressionFactoryNode->AddApplyAndFillDelegates<FLinearColor>(DefaultValueMemberName.ToString(), UMaterialExpressionVectorParameter::StaticClass(), DefaultValueMemberName);
	}

	return MaterialExpressionFactoryNode;
}

TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> UInterchangeGenericMaterialPipeline::CreateMaterialExpressionForInput(UInterchangeMaterialFactoryNode* MaterialFactoryNode, const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
{
	// If we have a connection
	// - Create material expression for connected shader node
	// - Connected material expression to something?
	//
	// If we don't have a connection
	// - Create material expression for the input value

	UInterchangeMaterialExpressionFactoryNode* MaterialExpressionFactoryNode = nullptr;

	FString ConnectedShaderNodeUid;
	FString OutputName;
	if (UInterchangeShaderPortsAPI::GetInputConnection(ShaderNode, InputName, ConnectedShaderNodeUid, OutputName))
	{
		if (UInterchangeShaderNode* ConnectedShaderNode = Cast<UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
		{
			MaterialExpressionFactoryNode = CreateMaterialExpressionForShaderNode(MaterialFactoryNode, ConnectedShaderNode, ParentUid);
		}
	}
	else
	{
		FString MaterialExpressionUid = ShaderNode->GetUniqueID() + TEXT(".") + InputName;
		switch(UInterchangeShaderPortsAPI::GetInputType(ShaderNode, InputName))
		{
		case UE::Interchange::EAttributeTypes::Float:
			MaterialExpressionFactoryNode = CreateScalarParameterExpression(ShaderNode, InputName, ParentUid);
			break;
		case UE::Interchange::EAttributeTypes::LinearColor:
			MaterialExpressionFactoryNode = CreateVectorParameterExpression(ShaderNode, InputName, ParentUid);
			break;
		}
	}

	return TTuple<UInterchangeMaterialExpressionFactoryNode*, FString>{MaterialExpressionFactoryNode, OutputName};
}

UInterchangeMaterialFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode)
{
	UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>( CreateBaseMaterialFactoryNode(ShaderGraphNode, UInterchangeMaterialFactoryNode::StaticClass()) );

	if (!HandlePhongModel(ShaderGraphNode, MaterialFactoryNode))
	{
		if (!HandleLambertModel(ShaderGraphNode, MaterialFactoryNode))
		{
			HandlePBRModel(ShaderGraphNode, MaterialFactoryNode);
		}
	}

	HandleCommonParameters(ShaderGraphNode, MaterialFactoryNode);

	return MaterialFactoryNode;
}

