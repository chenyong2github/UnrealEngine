// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericMaterialPipeline.h"

#include "CoreMinimal.h"

#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSourceData.h"

#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	else if (MaterialImport == EInterchangeMaterialImportOption::ImportAsMaterialInstances)
	{
		for (const UInterchangeShaderGraphNode* ShaderGraphNode : MaterialNodes)
		{
			if (UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode = CreateMaterialInstanceFactoryNode(ShaderGraphNode))
			{
				//By default we do not create the materials, every node with mesh attribute can enable them. So we wont create unused materials.
				MaterialInstanceFactoryNode->SetEnabled(false);
			}
		}
	}
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

bool UInterchangeGenericMaterialPipeline::IsClearCoatModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::ClearCoat;

	const bool bHasClearCoatInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::ClearCoat);

	return bHasClearCoatInput;
}

bool UInterchangeGenericMaterialPipeline::IsSheenModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::Sheen;

	const bool bHasSheenColorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SheenColor);

	return bHasSheenColorInput;
}

bool UInterchangeGenericMaterialPipeline::IsThinTranslucentModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::ThinTranslucent;

	const bool bHasTransmissionColorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::TransmissionColor);

	return bHasTransmissionColorInput;
}

bool UInterchangeGenericMaterialPipeline::IsPBRModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::PBR;

	const bool bHasBaseColorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::BaseColor);

	return bHasBaseColorInput;
}

bool UInterchangeGenericMaterialPipeline::IsPhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::Phong;

	const bool bHasDiffuseInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::DiffuseColor);
	const bool bHasSpecularInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SpecularColor);

	return bHasDiffuseInput && bHasSpecularInput;
}

bool UInterchangeGenericMaterialPipeline::IsLambertModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::Lambert;

	const bool bHasDiffuseInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::DiffuseColor);

	return bHasDiffuseInput;
}

bool UInterchangeGenericMaterialPipeline::HandlePhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Phong;

	if (IsPhongModel(ShaderGraphNode))
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
				UInterchangeShaderPortsAPI::ConnectOuputToInput(FunctionCallExpression, Parameters::DiffuseColor.ToString(),
					DiffuseExpressionFactoryNode.Get<0>()->GetUniqueID(), DiffuseExpressionFactoryNode.Get<1>());
			}
		}

		// Specular
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> SpecularExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SpecularColor.ToString(), FunctionCallExpression->GetUniqueID());

			if (SpecularExpressionFactoryNode.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInput(FunctionCallExpression, Parameters::SpecularColor.ToString(),
					SpecularExpressionFactoryNode.Get<0>()->GetUniqueID(), SpecularExpressionFactoryNode.Get<1>());
			}
		}
		
		// Shininess
		{
			const bool bHasShininessInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Shininess);
			if (bHasShininessInput)
			{
				TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

				TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ShininessExpressionFactoryNode =
					CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Shininess.ToString(), MaterialFactoryNode->GetUniqueID());

				if (ShininessExpressionFactoryNode.Get<0>())
				{
					UInterchangeMaterialExpressionFactoryNode* DivideShininessNode =
						CreateExpressionNode(TEXT("DivideShininess"), ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), UMaterialExpressionDivide::StaticClass());

					const float ShininessScale = 100.f; // Divide shininess by 100 to bring it into a 0-1 range for roughness.
					const FString ShininessScaleParameterName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, ConstB).ToString();
					DivideShininessNode->AddFloatAttribute(ShininessScaleParameterName, ShininessScale);
					DivideShininessNode->AddApplyAndFillDelegates<float>(ShininessScaleParameterName, UMaterialExpressionDivide::StaticClass(),  GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, ConstB));

					// Connect Shininess to Divide
					UInterchangeShaderPortsAPI::ConnectOuputToInput(DivideShininessNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, A).ToString(),
						ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), ShininessExpressionFactoryNode.Get<1>());

					UInterchangeMaterialExpressionFactoryNode* InverseShininessNode =
						CreateExpressionNode(TEXT("InverseShininess"), ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), UMaterialExpressionOneMinus::StaticClass());

					// Connect Divide to Inverse
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(InverseShininessNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionOneMinus, Input).ToString(),
						DivideShininessNode->GetUniqueID());

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

	if (IsLambertModel(ShaderGraphNode))
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

	bool bShadingModelHandled = false;

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

			bShadingModelHandled = true;
		}
	}

	// Metallic
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Metallic);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Metallic.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToMetallic(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Specular
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Specular);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Specular.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToSpecular(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Roughness
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Roughness);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Roughness.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToRoughness(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	return bShadingModelHandled;
}

bool UInterchangeGenericMaterialPipeline::HandleClearCoat(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::ClearCoat;

	bool bShadingModelHandled = false;

	// Clear Coat
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::ClearCoat);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::ClearCoat.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToClearCoat(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Clear Coat Roughness
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::ClearCoatRoughness);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::ClearCoatRoughness.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToClearCoatRoughness(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Clear Coat Normal
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::ClearCoatNormal);

		if (bHasInput)
		{
			TGuardValue<bool> ParsingForNormalInputGuard(bParsingForNormalInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::ClearCoatNormal.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToClearCoatNormal(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	if (bShadingModelHandled)
	{
		MaterialFactoryNode->SetCustomShadingModel(EMaterialShadingModel::MSM_ClearCoat);
	}

	return bShadingModelHandled;
}

bool UInterchangeGenericMaterialPipeline::HandleSheen(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Sheen;

	bool bShadingModelHandled = false;

	// Sheen Color
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SheenColor);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SheenColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToFuzzColor(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Sheen Roughness
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SheenRoughness);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SheenRoughness.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				UInterchangeMaterialExpressionFactoryNode* InverseSheenRoughnessNode =
						CreateExpressionNode(TEXT("InverseSheenRoughness"), ExpressionFactoryNode.Get<0>()->GetUniqueID(), UMaterialExpressionOneMinus::StaticClass());

				UInterchangeShaderPortsAPI::ConnectOuputToInput(InverseSheenRoughnessNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionOneMinus, Input).ToString(),
					ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());

				MaterialFactoryNode->ConnectToCloth(InverseSheenRoughnessNode->GetUniqueID());
			}

			bShadingModelHandled = true;
		}
	}

	if (bShadingModelHandled)
	{
		MaterialFactoryNode->SetCustomShadingModel(EMaterialShadingModel::MSM_Cloth);
	}

	return bShadingModelHandled;
}

bool UInterchangeGenericMaterialPipeline::HandleThinTranslucent(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::ThinTranslucent;

	bool bShadingModelHandled = false;

	// Transmission Color
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::TransmissionColor);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::TransmissionColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToTransmissionColor(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	if (bShadingModelHandled)
	{
		MaterialFactoryNode->SetCustomBlendMode(EBlendMode::BLEND_Translucent);
		MaterialFactoryNode->SetCustomShadingModel(EMaterialShadingModel::MSM_ThinTranslucent);
		MaterialFactoryNode->SetCustomTranslucencyLightingMode(ETranslucencyLightingMode::TLM_SurfacePerPixelLighting);
	}

	return bShadingModelHandled;
}

void UInterchangeGenericMaterialPipeline::HandleCommonParameters(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Common;

	// Two sidedness
	{
		bool bTwoSided = false;
		ShaderGraphNode->GetCustomTwoSided(bTwoSided);
		MaterialFactoryNode->SetCustomTwoSided(bTwoSided);
	}

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
		}
	}

	// Normal
	{
		const bool bHasNormalInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Normal);

		if (bHasNormalInput)
		{
			TGuardValue<bool> ParsingForNormalInputGuard(bParsingForNormalInput, true);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Normal.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToNormal(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}
		}
	}

	// Opacity
	{
		TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

		const bool bHasOpacityInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Opacity);

		if (bHasOpacityInput)
		{
			bool bHasSomeTransparency = true;

			float OpacityValue;
			if (ShaderGraphNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Parameters::Opacity.ToString()), OpacityValue))
			{
				bHasSomeTransparency = !FMath::IsNearlyEqual(OpacityValue, 1.f);
			}

			if (bHasSomeTransparency)
			{
				TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> OpacityExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Opacity.ToString(), MaterialFactoryNode->GetUniqueID());

				if (OpacityExpressionFactoryNode.Get<0>())
				{
					MaterialFactoryNode->ConnectOutputToOpacity(OpacityExpressionFactoryNode.Get<0>()->GetUniqueID(), OpacityExpressionFactoryNode.Get<1>());

					// Don't change the blend mode or the lighting mode if they were already set
					TEnumAsByte<EBlendMode> BlendMode = EBlendMode::BLEND_Translucent;
					if (!MaterialFactoryNode->GetCustomBlendMode(BlendMode))
					{
						MaterialFactoryNode->SetCustomBlendMode(BlendMode);

						TEnumAsByte<ETranslucencyLightingMode> LightingMode = ETranslucencyLightingMode::TLM_Surface;
						if (!MaterialFactoryNode->GetCustomTranslucencyLightingMode(LightingMode))
						{
							MaterialFactoryNode->SetCustomTranslucencyLightingMode(LightingMode);
						}
					}
				}
			}
		}
	}

	// Ambient Occlusion
	{
		TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

		const bool bHasOcclusionInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Occlusion);

		if (bHasOcclusionInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Occlusion.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToOcclusion(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}
		}
	}

	// Index of Refraction (IOR)
	// We'll lerp between Air IOR (1) and the IOR from the shader graph based on a fresnel, as per UE doc on refraction.
	{
		TGuardValue<bool> ParsingForLinearInputGuard(bParsingForLinearInput, true);

		const bool bHasIorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::IndexOfRefraction);

		if (bHasIorInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::IndexOfRefraction.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				UInterchangeMaterialExpressionFactoryNode* IORLerp = CreateExpressionNode(TEXT("IORLerp"), ShaderGraphNode->GetUniqueID(), UMaterialExpressionLinearInterpolate::StaticClass());

				const float AirIOR = 1.f;
				const FString ConstAMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, ConstA).ToString();
				IORLerp->AddFloatAttribute(ConstAMemberName, AirIOR);
				IORLerp->AddApplyAndFillDelegates<float>(ConstAMemberName, UMaterialExpressionLinearInterpolate::StaticClass(), *ConstAMemberName);

				UInterchangeShaderPortsAPI::ConnectOuputToInput(IORLerp, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, B).ToString(),
					ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());

				UInterchangeMaterialExpressionFactoryNode* IORFresnel = CreateExpressionNode(TEXT("IORFresnel"), ShaderGraphNode->GetUniqueID(), UMaterialExpressionFresnel::StaticClass());

				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(IORLerp, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, Alpha).ToString(), IORFresnel->GetUniqueID());

				MaterialFactoryNode->ConnectToRefraction(IORLerp->GetUniqueID());
			}
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleFlattenNormalNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode,
	UInterchangeMaterialExpressionFactoryNode* FlattenNormalFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::FlattenNormal;

	FlattenNormalFactoryNode->SetCustomExpressionClassName(UMaterialExpressionMaterialFunctionCall::StaticClass()->GetName());

	const FString MaterialFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialFunctionCall, MaterialFunction).ToString();
	FlattenNormalFactoryNode->AddStringAttribute(MaterialFunctionMemberName, TEXT("/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal"));
	FlattenNormalFactoryNode->AddApplyAndFillDelegates<FString>(MaterialFunctionMemberName, UMaterialExpressionMaterialFunctionCall::StaticClass(), *MaterialFunctionMemberName);

	// Normal
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> NormalExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Normal.ToString(), FlattenNormalFactoryNode->GetUniqueID());

		if (NormalExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(FlattenNormalFactoryNode, TEXT("Normal"),
				NormalExpression.Get<0>()->GetUniqueID(), NormalExpression.Get<1>());
		}
	}

	// Flatness
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> FlatnessExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Flatness.ToString(), FlattenNormalFactoryNode->GetUniqueID());

		if (FlatnessExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(FlattenNormalFactoryNode, TEXT("Flatness"),
				FlatnessExpression.Get<0>()->GetUniqueID(), FlatnessExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureSampleNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::TextureSample;

	FString TextureUid;
	ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureUid);

	FString ExpressionClassName;
	FString TextureFactoryUid;

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

		TArray<FString> TextureTargetNodes;
		TextureNode->GetTargetNodeUids(TextureTargetNodes);

		if (TextureTargetNodes.Num() > 0)
		{
			TextureFactoryUid = TextureTargetNodes[0];
		}
	}

	TextureSampleFactoryNode->SetCustomExpressionClassName(ExpressionClassName);
	TextureSampleFactoryNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureFactoryUid);

	if (bParsingForNormalInput)
	{
		if (UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(TextureFactoryUid)))
		{
			TextureFactoryNode->SetCustomCompressionSettings(TextureCompressionSettings::TC_Normalmap);
		}
	}
	else if (bParsingForLinearInput)
	{
		if (UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetNode(TextureFactoryUid)))
		{
			TextureFactoryNode->SetCustomSRGB(false);
		}
	}

	// Coordinates
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> CoordinatesExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Coordinates.ToString(), TextureSampleFactoryNode->GetUniqueID());

		if (CoordinatesExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(TextureSampleFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, Coordinates).ToString(),
				CoordinatesExpression.Get<0>()->GetUniqueID(), CoordinatesExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureCoordinateNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode,
	UInterchangeMaterialExpressionFactoryNode*& TexCoordFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	TexCoordFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTextureCoordinate::StaticClass()->GetName());

	// Index
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> IndexExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::Index.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (IndexExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(TexCoordFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, CoordinateIndex).ToString(),
				IndexExpression.Get<0>()->GetUniqueID(), IndexExpression.Get<1>());
		}
	}

	// U tiling
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> UTilingExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::UTiling.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (UTilingExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(TexCoordFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, UTiling).ToString(),
				UTilingExpression.Get<0>()->GetUniqueID(), UTilingExpression.Get<1>());
		}
	}

	// V tiling
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> VTilingExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::VTiling.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (VTilingExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(TexCoordFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, VTiling).ToString(),
				VTilingExpression.Get<0>()->GetUniqueID(), VTilingExpression.Get<1>());
		}
	}

	// Scale
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ScaleExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::Scale.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (ScaleExpression.Get<0>())
		{
			UInterchangeMaterialExpressionFactoryNode* MultiplyExpression =
				CreateExpressionNode(ScaleExpression.Get<0>()->GetDisplayLabel() + TEXT("_Multiply"), TexCoordFactoryNode->GetUniqueID(), UMaterialExpressionMultiply::StaticClass());

			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(MultiplyExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionMultiply, A).ToString(),
				TexCoordFactoryNode->GetUniqueID());
			UInterchangeShaderPortsAPI::ConnectOuputToInput(MultiplyExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionMultiply, B).ToString(),
				ScaleExpression.Get<0>()->GetUniqueID(), ScaleExpression.Get<1>());

			TexCoordFactoryNode = MultiplyExpression;
		}
	}
	
	// Rotate
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> RotateExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::Rotate.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (RotateExpression.Get<0>())
		{
			UInterchangeMaterialExpressionFactoryNode* CallRotatorExpression =
				CreateExpressionNode(RotateExpression.Get<0>()->GetDisplayLabel() + TEXT("_Rotator"), TexCoordFactoryNode->GetUniqueID(), UMaterialExpressionMaterialFunctionCall::StaticClass());

			const FString MaterialFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialFunctionCall, MaterialFunction).ToString();
			CallRotatorExpression->AddStringAttribute(MaterialFunctionMemberName, TEXT("/Engine/Functions/Engine_MaterialFunctions02/Texturing/CustomRotator.CustomRotator"));
			CallRotatorExpression->AddApplyAndFillDelegates<FString>(MaterialFunctionMemberName, UMaterialExpressionMaterialFunctionCall::StaticClass(), *MaterialFunctionMemberName);

			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(CallRotatorExpression, TEXT("UVs"), TexCoordFactoryNode->GetUniqueID());
			UInterchangeShaderPortsAPI::ConnectOuputToInput(CallRotatorExpression, TEXT("Rotation Angle (0-1)"), RotateExpression.Get<0>()->GetUniqueID(), RotateExpression.Get<1>());

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> RotationCenterExpression =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::RotationCenter.ToString(), TexCoordFactoryNode->GetUniqueID());

			if (RotationCenterExpression.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInput(CallRotatorExpression, TEXT("Rotation Center"), RotationCenterExpression.Get<0>()->GetUniqueID(), RotationCenterExpression.Get<1>());
			}

			TexCoordFactoryNode = CallRotatorExpression;
		}
	}

	// Offset
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> OffsetExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::Offset.ToString(), TexCoordFactoryNode->GetUniqueID());

		if (OffsetExpression.Get<0>())
		{
			UInterchangeMaterialExpressionFactoryNode* AddExpression =
				CreateExpressionNode(OffsetExpression.Get<0>()->GetDisplayLabel() + TEXT("_Add"), TexCoordFactoryNode->GetUniqueID(), UMaterialExpressionAdd::StaticClass());

			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(AddExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionAdd, A).ToString(),
				TexCoordFactoryNode->GetUniqueID());
			UInterchangeShaderPortsAPI::ConnectOuputToInput(AddExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionAdd, B).ToString(),
				OffsetExpression.Get<0>()->GetUniqueID(), OffsetExpression.Get<1>());

			TexCoordFactoryNode = AddExpression;
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleLerpNode(const UInterchangeShaderNode* ShaderNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* LerpFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	LerpFactoryNode->SetCustomExpressionClassName(UMaterialExpressionLinearInterpolate::StaticClass()->GetName());

	// A
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorAExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::A.ToString(), LerpFactoryNode->GetUniqueID());

		if (ColorAExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, A).ToString(),
				ColorAExpression.Get<0>()->GetUniqueID(), ColorAExpression.Get<1>());
		}
	}
	
	// B
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorBExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::B.ToString(), LerpFactoryNode->GetUniqueID());

		if (ColorBExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, B).ToString(),
				ColorBExpression.Get<0>()->GetUniqueID(), ColorBExpression.Get<1>());
		}
	}
	
	// Factor
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> FactorExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::Factor.ToString(), LerpFactoryNode->GetUniqueID());

		if (FactorExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInput(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, Alpha).ToString(),
				FactorExpression.Get<0>()->GetUniqueID(), FactorExpression.Get<1>());
		}
	}
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

	if (*ShaderType == Nodes::FlattenNormal::Name)
	{
		HandleFlattenNormalNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::Lerp::Name)
	{
		HandleLerpNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::TextureCoordinate::Name)
	{
		HandleTextureCoordinateNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::TextureSample::Name)
	{
		HandleTextureSampleNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
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

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateVector2ParameterExpression(const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
{
	FVector2f InputValue;
	if (ShaderNode->GetAttribute<FVector2f>(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue))
	{
		UInterchangeMaterialExpressionFactoryNode* VectorParameterFactoryNode = CreateExpressionNode(InputName, ParentUid, UMaterialExpressionVectorParameter::StaticClass());

		const FName DefaultValueMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorParameter, DefaultValue);
		VectorParameterFactoryNode->AddLinearColorAttribute(DefaultValueMemberName.ToString(), FLinearColor(InputValue.X, InputValue.Y, 0.f));
		VectorParameterFactoryNode->AddApplyAndFillDelegates<FLinearColor>(DefaultValueMemberName.ToString(), UMaterialExpressionVectorParameter::StaticClass(), DefaultValueMemberName);

		// Defaults to R&G
		UInterchangeMaterialExpressionFactoryNode* ComponentMaskFactoryNode = CreateExpressionNode(InputName + TEXT("_Mask"), ParentUid, UMaterialExpressionComponentMask::StaticClass());

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ComponentMaskFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, Input).ToString(),
			VectorParameterFactoryNode->GetUniqueID() );

		return ComponentMaskFactoryNode;
	}

	return nullptr;
}

TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> UInterchangeGenericMaterialPipeline::CreateMaterialExpressionForInput(UInterchangeMaterialFactoryNode* MaterialFactoryNode, const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
{
	// Make sure we don't create an expression for an input if it already has one
	if (UInterchangeShaderPortsAPI::HasInput(MaterialFactoryNode, *InputName))
	{
		return TTuple<UInterchangeMaterialExpressionFactoryNode*, FString>{};
	}

	// If we have a connection
	// - Create material expression for the connected shader node
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
		switch(UInterchangeShaderPortsAPI::GetInputType(ShaderNode, InputName))
		{
		case UE::Interchange::EAttributeTypes::Float:
			MaterialExpressionFactoryNode = CreateScalarParameterExpression(ShaderNode, InputName, ParentUid);
			break;
		case UE::Interchange::EAttributeTypes::LinearColor:
			MaterialExpressionFactoryNode = CreateVectorParameterExpression(ShaderNode, InputName, ParentUid);
			break;
		case UE::Interchange::EAttributeTypes::Vector2f:
			MaterialExpressionFactoryNode = CreateVector2ParameterExpression(ShaderNode, InputName, ParentUid);
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
		HandleLambertModel(ShaderGraphNode, MaterialFactoryNode);
	}

	HandlePBRModel(ShaderGraphNode, MaterialFactoryNode); // Always process the PBR parameters. If they were already assigned from Phong or Lambert, they will be ignored.
	
	if (!HandleClearCoat(ShaderGraphNode, MaterialFactoryNode))
	{
		// Can't have different shading models
		if (!HandleThinTranslucent(ShaderGraphNode, MaterialFactoryNode))
		{
			HandleSheen(ShaderGraphNode, MaterialFactoryNode);
		}
	}

	HandleCommonParameters(ShaderGraphNode, MaterialFactoryNode);

	return MaterialFactoryNode;
}

UInterchangeMaterialInstanceFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialInstanceFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode)
{
	UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode =
		Cast<UInterchangeMaterialInstanceFactoryNode>( CreateBaseMaterialFactoryNode(ShaderGraphNode, UInterchangeMaterialInstanceFactoryNode::StaticClass()) );

	if (UMaterialInterface* ParentMaterialObj = Cast<UMaterialInterface>(ParentMaterial.TryLoad()))
	{
		MaterialInstanceFactoryNode->SetCustomParent(ParentMaterialObj->GetPathName());
	}
	else if (IsThinTranslucentModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/ThinTranslucentMaterial.ThinTranslucentMaterial'"));
	}
	else if (IsClearCoatModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/ClearCoatMaterial.ClearCoatMaterial'"));
	}
	else if (IsSheenModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/SheenMaterial.SheenMaterial'"));
	}
	else if (IsPBRModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/PBRSurfaceMaterial.PBRSurfaceMaterial'"));
	}
	else if (IsPhongModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/PhongSurfaceMaterial.PhongSurfaceMaterial'"));
	}
	else if (IsLambertModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/LambertSurfaceMaterial.LambertSurfaceMaterial'"));
	}
	else
	{
		// Default to PBR
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/PBRSurfaceMaterial.PBRSurfaceMaterial'"));
	}

#if WITH_EDITOR
	MaterialInstanceFactoryNode->SetCustomInstanceClassName(UMaterialInstanceConstant::StaticClass()->GetPathName());
#else
	MaterialInstanceFactoryNode->SetCustomInstanceClassName(UMaterialInstanceDynamic::StaticClass()->GetPathName());
#endif

	TArray<FString> Inputs;
	UInterchangeShaderPortsAPI::GatherInputs(ShaderGraphNode, Inputs);

	for (const FString& InputName : Inputs)
	{
		TVariant<FString, FLinearColor, float> InputValue;

		FString ConnectedShaderNodeUid;
		FString OutputName;
		if (UInterchangeShaderPortsAPI::GetInputConnection(ShaderGraphNode, InputName, ConnectedShaderNodeUid, OutputName))
		{
			if (UInterchangeShaderNode* ConnectedShaderNode = Cast<UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
			{
				InputValue = VisitShaderNode(ConnectedShaderNode);
			}
		}
		else
		{
			switch(UInterchangeShaderPortsAPI::GetInputType(ShaderGraphNode, InputName))
			{
			case UE::Interchange::EAttributeTypes::Float:
				{
					float AttributeValue = 0.f;
					ShaderGraphNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), AttributeValue);
					InputValue.Set<float>(AttributeValue);
				}
				break;
			case UE::Interchange::EAttributeTypes::LinearColor:
				{
					FLinearColor AttributeValue = FLinearColor::White;
					ShaderGraphNode->GetLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), AttributeValue);
					InputValue.Set<FLinearColor>(AttributeValue);
				}
				break;
			}
		}

		if (InputValue.IsType<float>())
		{
			MaterialInstanceFactoryNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue.Get<float>());
		}
		else if (InputValue.IsType<FLinearColor>())
		{
			MaterialInstanceFactoryNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue.Get<FLinearColor>());
		}
		else if (InputValue.IsType<FString>())
		{
			const FString MapName(InputName + TEXT("Map"));
			MaterialInstanceFactoryNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(MapName), InputValue.Get<FString>());

			const FString MapWeightName(MapName + TEXT("Weight"));
			MaterialInstanceFactoryNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(MapWeightName), 1.f);
		}
	}

	return MaterialInstanceFactoryNode;
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitShaderNode(const UInterchangeShaderNode* ShaderNode) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TVariant<FString, FLinearColor, float> Result;

	FString ShaderType;
	if (ShaderNode->GetCustomShaderType(ShaderType))
	{
		if (*ShaderType == TextureSample::Name)
		{
			return VisitTextureSampleNode(ShaderNode);
		}
		else if (*ShaderType == Lerp::Name)
		{
			return VisitLerpNode(ShaderNode);
		}
		else if (*ShaderType == Multiply::Name)
		{
			return VisitMultiplyNode(ShaderNode);
		}
		else if (*ShaderType == OneMinus::Name)
		{
			return VisitOneMinusNode(ShaderNode);
		}
	}

	{
		TArray<FString> Inputs;
		UInterchangeShaderPortsAPI::GatherInputs(ShaderNode, Inputs);

		if (Inputs.Num() > 0)
		{
			const FString& InputName = Inputs[0];
			Result = VisitShaderInput(ShaderNode, InputName);
		}
	}

	return Result;
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitShaderInput(const UInterchangeShaderNode* ShaderNode, const FString& InputName) const
{
	TVariant<FString, FLinearColor, float> Result;

	FString ConnectedShaderNodeUid;
	FString OutputName;
	if (UInterchangeShaderPortsAPI::GetInputConnection(ShaderNode, InputName, ConnectedShaderNodeUid, OutputName))
	{
		if (UInterchangeShaderNode* ConnectedShaderNode = Cast<UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
		{
			Result = VisitShaderNode(ConnectedShaderNode);
		}
	}
	else
	{
		switch(UInterchangeShaderPortsAPI::GetInputType(ShaderNode, InputName))
		{
		case UE::Interchange::EAttributeTypes::Float:
			{
				float InputValue = 0.f;
				ShaderNode->GetFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue);
				Result.Set<float>(InputValue);
			}
			break;
		case UE::Interchange::EAttributeTypes::LinearColor:
			{
				FLinearColor InputValue = FLinearColor::White;
				ShaderNode->GetLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputName), InputValue);
				Result.Set<FLinearColor>(InputValue);
			}
			break;
		}
	}

	return Result;
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitLerpNode(const UInterchangeShaderNode* ShaderNode) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TVariant<FString, FLinearColor, float> ResultA = VisitShaderInput(ShaderNode, Lerp::Inputs::A.ToString());
	TVariant<FString, FLinearColor, float> ResultB = VisitShaderInput(ShaderNode, Lerp::Inputs::B.ToString());

	TVariant<FString, FLinearColor, float> ResultFactor = VisitShaderInput(ShaderNode, Lerp::Inputs::Factor.ToString());

	bool bResultAIsStrongest = true;

	if (ResultFactor.IsType<float>())
	{
		const float Factor = ResultFactor.Get<float>();
		bResultAIsStrongest = (Factor <= 0.5f);

		// Bake the lerp into a single value
		if (!ResultA.IsType<FString>() && !ResultB.IsType<FString>())
		{
			if (ResultA.IsType<float>() && ResultB.IsType<float>())
			{
				const float ValueA = ResultA.Get<float>();
				const float ValueB = ResultB.Get<float>();

				TVariant<FString, FLinearColor, float> Result;
				Result.Set<float>(FMath::Lerp(ValueA, ValueB, Factor));
				return Result;
			}
			else if (ResultA.IsType<FLinearColor>() && ResultB.IsType<FLinearColor>())
			{
				const FLinearColor ValueA = ResultA.Get<FLinearColor>();
				const FLinearColor ValueB = ResultB.Get<FLinearColor>();

				TVariant<FString, FLinearColor, float> Result;
				Result.Set<FLinearColor>(FMath::Lerp(ValueA, ValueB, Factor));
				return Result;
			}
		}
	}

	if (bResultAIsStrongest)
	{
		return ResultA;
	}
	else
	{
		return ResultB;
	}
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitMultiplyNode(const UInterchangeShaderNode* ShaderNode) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TVariant<FString, FLinearColor, float> ResultA = VisitShaderInput(ShaderNode, Lerp::Inputs::A.ToString());
	TVariant<FString, FLinearColor, float> ResultB = VisitShaderInput(ShaderNode, Lerp::Inputs::B.ToString());

	// Bake the multiply into a single value if possible
	if (!ResultA.IsType<FString>() && !ResultB.IsType<FString>())
	{
		if (ResultA.IsType<float>() && ResultB.IsType<float>())
		{
			const float ValueA = ResultA.Get<float>();
			const float ValueB = ResultB.Get<float>();

			TVariant<FString, FLinearColor, float> Result;
			Result.Set<float>(ValueA * ValueB);
			return Result;
		}
		else if (ResultA.IsType<FLinearColor>() && ResultB.IsType<FLinearColor>())
		{
			const FLinearColor ValueA = ResultA.Get<FLinearColor>();
			const FLinearColor ValueB = ResultB.Get<FLinearColor>();

			TVariant<FString, FLinearColor, float> Result;
			Result.Set<FLinearColor>(ValueA * ValueB);
			return Result;
		}
		else if (ResultA.IsType<FLinearColor>() && ResultB.IsType<float>())
		{
			const FLinearColor ValueA = ResultA.Get<FLinearColor>();
			const float ValueB = ResultB.Get<float>();

			TVariant<FString, FLinearColor, float> Result;
			Result.Set<FLinearColor>(ValueA * ValueB);
			return Result;
		}
		else if (ResultA.IsType<float>() && ResultB.IsType<FLinearColor>())
		{
			const float ValueA = ResultA.Get<float>();
			const FLinearColor ValueB = ResultB.Get<FLinearColor>();

			TVariant<FString, FLinearColor, float> Result;
			Result.Set<FLinearColor>(ValueA * ValueB);
			return Result;
		}
	}

	return ResultA;
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitOneMinusNode(const UInterchangeShaderNode* ShaderNode) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TVariant<FString, FLinearColor, float> ResultInput = VisitShaderInput(ShaderNode, OneMinus::Inputs::Input.ToString());

	if (ResultInput.IsType<FLinearColor>())
	{
		const FLinearColor Value = ResultInput.Get<FLinearColor>();

		TVariant<FString, FLinearColor, float> Result;
		Result.Set<FLinearColor>(FLinearColor::White - Value);
		return Result;
	}
	else if (ResultInput.IsType<float>())
	{
		const float Value = ResultInput.Get<float>();

		TVariant<FString, FLinearColor, float> Result;
		Result.Set<float>(1.f - Value);
		return Result;
	}

	return ResultInput;
}

TVariant<FString, FLinearColor, float> UInterchangeGenericMaterialPipeline::VisitTextureSampleNode(const UInterchangeShaderNode* ShaderNode) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TVariant<FString, FLinearColor, float> Result;

	FString TextureUid;
	if (ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureSample::Inputs::Texture.ToString()), TextureUid))
	{
		if (!TextureUid.IsEmpty())
		{
			FString TextureFactoryUid;
			if (UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
			{
				TArray<FString> TextureTargetNodes;
				TextureNode->GetTargetNodeUids(TextureTargetNodes);

				if (TextureTargetNodes.Num() > 0)
				{
					TextureFactoryUid = TextureTargetNodes[0];
				}
			}

			Result.Set<FString>(TextureFactoryUid);
		}
	}

	return Result;
}
