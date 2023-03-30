// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeGenericMaterialPipeline.h"

#include "CoreMinimal.h"

#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTextureBlurNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangeSourceData.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Nodes/InterchangeBaseNode.h"

#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialX/MaterialExpressions/MaterialExpressionTextureSampleParameterBlur.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "Templates/Function.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeGenericMaterialPipeline)

#define LOCTEXT_NAMESPACE "InterchangeGenericMaterialPipeline"

FString LexToString(UInterchangeGenericMaterialPipeline::EMaterialInputType Value)
{
	switch(Value)
	{
	case UInterchangeGenericMaterialPipeline::EMaterialInputType::Unknown:
		return TEXT("Unknown");
	case UInterchangeGenericMaterialPipeline::EMaterialInputType::Color:
		return TEXT("Color");
	case UInterchangeGenericMaterialPipeline::EMaterialInputType::Vector:
		return TEXT("Vector");
	case UInterchangeGenericMaterialPipeline::EMaterialInputType::Scalar:
		return TEXT("Scalar");
	default:
		ensure(false);
		return FString();
	}
}

FMaterialXPipelineSettings::FMaterialXPipelineSettings()
{
	SurfaceShader.FindOrAdd(EInterchangeMaterialXShaders::StandardSurface, FSoftObjectPath{ TEXT("MaterialFunction'/Interchange/Functions/MX_StandardSurface.MX_StandardSurface'") });
	SurfaceShader.FindOrAdd(EInterchangeMaterialXShaders::StandardSurfaceTransmission, FSoftObjectPath{ TEXT("MaterialFunction'/Interchange/Functions/MX_TransmissionSurface.MX_TransmissionSurface'") });
	SurfaceShader.FindOrAdd(EInterchangeMaterialXShaders::SurfaceUnlit, FSoftObjectPath{ TEXT("MaterialFunction'/Interchange/Functions/MX_SurfaceUnlit.MX_SurfaceUnlit'") });
}

bool FMaterialXPipelineSettings::AreRequiredPackagesLoaded()
{
	auto ArePackagesLoaded = [&](const TMap<EInterchangeMaterialXShaders, FSoftObjectPath>& ObjectPaths)
	{
		bool bAllLoaded = true;

		for(const TPair<EInterchangeMaterialXShaders, FSoftObjectPath>& Pair : ObjectPaths)
		{
			const FSoftObjectPath& ObjectPath = Pair.Get<1>();

			if(!ObjectPath.ResolveObject())
			{
				FString PackagePath = ObjectPath.GetLongPackageName();
				if(FPackageName::DoesPackageExist(PackagePath))
				{
					UObject* Asset = ObjectPath.TryLoad();
					if(!Asset)
					{
						UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't load %s"), *PackagePath);
						bAllLoaded = false;
					}
#if WITH_EDITOR
					else
					{
						if(Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurface)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), StandardSurfaceInputs, StandardSurfaceOutputs);
						}
						else if(Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurfaceTransmission)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), TransmissionSurfaceInputs, TransmissionSurfaceOutputs);
						}
						else if(Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::SurfaceUnlit)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), SurfaceUnlitInputs, SurfaceUnlitOutputs);
						}

					}
#endif // WITH_EDITOR
				}
				else
				{
					UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't find %s"), *PackagePath);
					bAllLoaded = false;
				}
			}
		}

		return bAllLoaded;
	};

	return ArePackagesLoaded(SurfaceShader);
}

FString FMaterialXPipelineSettings::GetAssetPathString(EInterchangeMaterialXShaders ShaderType) const
{
	FString AssetPath;
	if(const FSoftObjectPath* ObjectPath = SurfaceShader.Find(ShaderType))
	{
		AssetPath = ObjectPath->GetAssetPathString();
	}

	return AssetPath;
}

#if WITH_EDITOR
TSet<FName> FMaterialXPipelineSettings::StandardSurfaceInputs
{
	UE::Interchange::Materials::StandardSurface::Parameters::Base,
	UE::Interchange::Materials::StandardSurface::Parameters::BaseColor,
	UE::Interchange::Materials::StandardSurface::Parameters::DiffuseRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::Metalness,
	UE::Interchange::Materials::StandardSurface::Parameters::Specular,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularIOR,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularAnisotropy,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularRotation,
	UE::Interchange::Materials::StandardSurface::Parameters::Subsurface,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceColor,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceRadius,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceScale,
	UE::Interchange::Materials::StandardSurface::Parameters::Sheen,
	UE::Interchange::Materials::StandardSurface::Parameters::SheenColor,
	UE::Interchange::Materials::StandardSurface::Parameters::SheenRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::Coat,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatColor,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatNormal,
	UE::Interchange::Materials::StandardSurface::Parameters::ThinFilmThickness,
	UE::Interchange::Materials::StandardSurface::Parameters::Emission,
	UE::Interchange::Materials::StandardSurface::Parameters::EmissionColor,
	UE::Interchange::Materials::StandardSurface::Parameters::Normal,
	UE::Interchange::Materials::StandardSurface::Parameters::Tangent
};

TSet<FName> FMaterialXPipelineSettings::StandardSurfaceOutputs
{
	TEXT("Base Color"), // MX_StandardSurface has BaseColor with a whitespace, this should be fixed in further release
	UE::Interchange::Materials::PBR::Parameters::Metallic,
	UE::Interchange::Materials::PBR::Parameters::Specular,
	UE::Interchange::Materials::PBR::Parameters::Roughness,
	UE::Interchange::Materials::PBR::Parameters::Anisotropy,
	UE::Interchange::Materials::PBR::Parameters::EmissiveColor,
	UE::Interchange::Materials::PBR::Parameters::Opacity,
	UE::Interchange::Materials::PBR::Parameters::Normal,
	UE::Interchange::Materials::PBR::Parameters::Tangent,
	UE::Interchange::Materials::Sheen::Parameters::SheenRoughness,
	UE::Interchange::Materials::Sheen::Parameters::SheenColor,
	UE::Interchange::Materials::Subsurface::Parameters::SubsurfaceColor,
	UE::Interchange::Materials::ClearCoat::Parameters::ClearCoat,
	UE::Interchange::Materials::ClearCoat::Parameters::ClearCoatRoughness,
	UE::Interchange::Materials::ClearCoat::Parameters::ClearCoatNormal
};

TSet<FName> FMaterialXPipelineSettings::TransmissionSurfaceInputs
{
	UE::Interchange::Materials::StandardSurface::Parameters::Base,
	UE::Interchange::Materials::StandardSurface::Parameters::BaseColor,
	UE::Interchange::Materials::StandardSurface::Parameters::DiffuseRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::Metalness,
	UE::Interchange::Materials::StandardSurface::Parameters::Specular,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularIOR,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularAnisotropy,
	UE::Interchange::Materials::StandardSurface::Parameters::SpecularRotation,
	UE::Interchange::Materials::StandardSurface::Parameters::Transmission,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionColor,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionDepth,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionScatter,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionScatterAnisotropy,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionDispersion,
	UE::Interchange::Materials::StandardSurface::Parameters::TransmissionExtraRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::Subsurface,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceColor,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceRadius,
	UE::Interchange::Materials::StandardSurface::Parameters::SubsurfaceScale,
	UE::Interchange::Materials::StandardSurface::Parameters::Sheen,
	UE::Interchange::Materials::StandardSurface::Parameters::SheenColor,
	UE::Interchange::Materials::StandardSurface::Parameters::SheenRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::Coat,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatColor,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatRoughness,
	UE::Interchange::Materials::StandardSurface::Parameters::CoatNormal,
	UE::Interchange::Materials::StandardSurface::Parameters::ThinFilmThickness,
	UE::Interchange::Materials::StandardSurface::Parameters::Emission,
	UE::Interchange::Materials::StandardSurface::Parameters::EmissionColor,
	UE::Interchange::Materials::StandardSurface::Parameters::Normal,
};

TSet<FName> FMaterialXPipelineSettings::TransmissionSurfaceOutputs
{
	UE::Interchange::Materials::PBR::Parameters::BaseColor,
	UE::Interchange::Materials::PBR::Parameters::Metallic,
	UE::Interchange::Materials::PBR::Parameters::Specular,
	UE::Interchange::Materials::PBR::Parameters::Roughness,
	UE::Interchange::Materials::PBR::Parameters::Anisotropy,
	UE::Interchange::Materials::PBR::Parameters::EmissiveColor,
	UE::Interchange::Materials::PBR::Parameters::Opacity,
	UE::Interchange::Materials::PBR::Parameters::Normal,
	UE::Interchange::Materials::PBR::Parameters::Tangent,
	UE::Interchange::Materials::PBR::Parameters::Refraction,
	UE::Interchange::Materials::ThinTranslucent::Parameters::TransmissionColor
};

TSet<FName> FMaterialXPipelineSettings::SurfaceUnlitInputs
{
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Emission,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::EmissionColor,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Transmission,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::TransmissionColor,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Opacity
};

TSet<FName> FMaterialXPipelineSettings::SurfaceUnlitOutputs
{
	UE::Interchange::Materials::Common::Parameters::EmissiveColor,
	UE::Interchange::Materials::Common::Parameters::Opacity,
	TEXT("OpacityMask")
};

bool FMaterialXPipelineSettings::ShouldFilterAssets(UMaterialFunction* Asset, const TSet<FName>& Inputs, const TSet<FName>& Outputs)
{
	int32 InputMatches = 0;
	int32 OutputMatches = 0;

	if(Asset != nullptr)
	{
		TArray<FFunctionExpressionInput> ExpressionInputs;
		TArray<FFunctionExpressionOutput> ExpressionOutputs;
		Asset->GetInputsAndOutputs(ExpressionInputs, ExpressionOutputs);

		for(const FFunctionExpressionInput& ExpressionInput : ExpressionInputs)
		{
			if(Inputs.Find(ExpressionInput.Input.InputName))
			{
				InputMatches++;
			}
		}

		for(const FFunctionExpressionOutput& ExpressionOutput : ExpressionOutputs)
		{
			if(Outputs.Find(ExpressionOutput.Output.OutputName))
			{
				OutputMatches++;
			}
		}
	}

	// we allow at least one input of the same name, but we should have exactly the same outputs
	return !(InputMatches > 0 && OutputMatches == Outputs.Num());
}
#endif // WITH_EDITOR

namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private
{
	bool AreRequiredPackagesLoaded()
	{
		auto ArePackagesLoaded = [](const TArray<FString>& PackagePaths) -> bool
		{
			bool bAllLoaded = true;

			for (const FString& PackagePath : PackagePaths)
			{
				const FString ObjectPath(FPackageName::ExportTextPathToObjectPath(PackagePath));

				if (FPackageName::DoesPackageExist(ObjectPath))
				{
					if (FSoftObjectPath(ObjectPath).TryLoad())
					{
						continue;
					}
					else
					{
						UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't load %s"), *PackagePath);
					}
				}
				else
				{
					UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't find %s"), *PackagePath);
				}

				bAllLoaded = false;
			}

			return bAllLoaded;
		};

		TArray<FString> RequiredPackages = {
			TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions01/Shading/ConvertFromDiffSpec.ConvertFromDiffSpec'"),
			TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal'"),
			TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions02/Utility/MakeFloat3.MakeFloat3'"),
			TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions02/Texturing/CustomRotator.CustomRotator'"),
		};

		static const bool bRequiredPackagesLoaded = ArePackagesLoaded(RequiredPackages);

		return bRequiredPackagesLoaded;
	}

	void UpdateBlendModeBasedOnOpacityAttributes(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
	{
		// Opacity Clip Value
		bool bIsMasked = false;
		{
			float OpacityClipValue;
			if (ShaderGraphNode->GetCustomOpacityMaskClipValue(OpacityClipValue))
			{
				MaterialFactoryNode->SetCustomOpacityMaskClipValue(OpacityClipValue);
				bIsMasked = true;
			}
		}

		// Don't change the blend mode if it was already set
		TEnumAsByte<EBlendMode> BlendMode = bIsMasked ? EBlendMode::BLEND_Masked : EBlendMode::BLEND_Translucent;
		if (!MaterialFactoryNode->GetCustomBlendMode(BlendMode))
		{
			MaterialFactoryNode->SetCustomBlendMode(BlendMode);
		}

		// If bland mode is masked or translucent, set lighting mode accordingly without changing it if it was already set
		if(BlendMode == EBlendMode::BLEND_Masked || BlendMode == EBlendMode::BLEND_Translucent)
		{
			TEnumAsByte<ETranslucencyLightingMode> LightingMode = ETranslucencyLightingMode::TLM_Surface;
			if (!MaterialFactoryNode->GetCustomTranslucencyLightingMode(LightingMode))
			{
				MaterialFactoryNode->SetCustomTranslucencyLightingMode(LightingMode);
			}
		}
	}

	void UpdateFunctionCallExpression(UInterchangeMaterialExpressionFactoryNode& FunctionCallExpression, const FString& MaterialFunctionPath)
	{
		const FName MaterialFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialFunctionCall, MaterialFunction);
		UClass* CustomExpressionClass = UMaterialExpressionMaterialFunctionCall::StaticClass();

		FunctionCallExpression.SetCustomExpressionClassName(CustomExpressionClass->GetName());
		
		FunctionCallExpression.AddStringAttribute(MaterialFunctionMemberName, MaterialFunctionPath);
		FunctionCallExpression.AddApplyAndFillDelegates<FString>(MaterialFunctionMemberName, CustomExpressionClass, MaterialFunctionMemberName);
	}

	UInterchangeMaterialExpressionFactoryNode* CreateExpressionWithMaterialFunction(
		UInterchangeBaseNodeContainer* BaseNodeContainer,
		UInterchangeMaterialFactoryNode* MaterialFactoryNode,
		const FString& Label,
		const FString& MaterialFunctionPath)
	{
		const FString UniqueId = MaterialFactoryNode->GetUniqueID() + Label;
		
		UInterchangeMaterialExpressionFactoryNode* Expression = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer, NAME_None);
		if (!Expression)
		{
			return nullptr;
		}

		Expression->InitializeNode(UniqueId, Label, EInterchangeNodeContainerType::FactoryData);

		BaseNodeContainer->AddNode(Expression);
		BaseNodeContainer->SetNodeParentUid(UniqueId, MaterialFactoryNode->GetUniqueID());

		UpdateFunctionCallExpression(*Expression, MaterialFunctionPath);
		
		return Expression;
	}
}

UInterchangeGenericMaterialPipeline::UInterchangeGenericMaterialPipeline()
{
	TexturePipeline = CreateDefaultSubobject<UInterchangeGenericTexturePipeline>("TexturePipeline");
}

void UInterchangeGenericMaterialPipeline::PreDialogCleanup(const FName PipelineStackName)
{
	if (TexturePipeline)
	{
		TexturePipeline->PreDialogCleanup(PipelineStackName);
	}

	SaveSettings(PipelineStackName);
}

bool UInterchangeGenericMaterialPipeline::IsSettingsAreValid(TOptional<FText>& OutInvalidReason) const
{
	if (TexturePipeline && !TexturePipeline->IsSettingsAreValid(OutInvalidReason))
	{
		return false;
	}

	return Super::IsSettingsAreValid(OutInvalidReason);
}

void UInterchangeGenericMaterialPipeline::AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset)
{
	Super::AdjustSettingsForContext(ImportType, ReimportAsset);

	if (TexturePipeline)
	{
		TexturePipeline->AdjustSettingsForContext(ImportType, ReimportAsset);
	}

	TArray<FString> HideCategories;
	bool bIsObjectAMaterial = !ReimportAsset ? false : ReimportAsset->IsA(UMaterialInterface::StaticClass());
	if ((!bIsObjectAMaterial && ImportType == EInterchangePipelineContext::AssetReimport)
		|| ImportType == EInterchangePipelineContext::AssetCustomLODImport
		|| ImportType == EInterchangePipelineContext::AssetCustomLODReimport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningImport
		|| ImportType == EInterchangePipelineContext::AssetAlternateSkinningReimport)
	{
		bImportMaterials = false;
		HideCategories.Add(TEXT("Materials"));
	}

	if (UInterchangePipelineBase* OuterMostPipeline = GetMostPipelineOuter())
	{
		for (const FString& HideCategoryName : HideCategories)
		{
			HidePropertiesOfCategory(OuterMostPipeline, this, HideCategoryName);
		}
	}

	using namespace UE::Interchange;

	if (!InterchangeGenericMaterialPipeline::Private::AreRequiredPackagesLoaded())
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMaterialPipeline: Some required packages are missing. Material import might be wrong"));
	}

	if(!MaterialXSettings.AreRequiredPackagesLoaded())
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMaterialPipeline: Some required packages are missing. Material import might be wrong"));
	}
}

void UInterchangeGenericMaterialPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	if (!InBaseNodeContainer)
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMaterialPipeline: Cannot execute pre-import pipeline because InBaseNodeContrainer is null"));
		return;
	}

	//Set the result container to allow error message
	//The parent Results container should be set at this point
	ensure(Results);
	{
		if (TexturePipeline)
		{
			TexturePipeline->SetResultsContainer(Results);
		}
	}

	BaseNodeContainer = InBaseNodeContainer;
	SourceDatas.Empty(InSourceDatas.Num());
	for (const UInterchangeSourceData* SourceData : InSourceDatas)
	{
		SourceDatas.Add(SourceData);
	}
	
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedExecutePipeline(InBaseNodeContainer, InSourceDatas);
	}

	//Skip Material import if the toggle is off
	if (!bImportMaterials)
	{
		return;
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

	// Check to see whether materials should be created even if unused
	// By default we do not create the materials, every node with mesh attribute can enable them. So we won't create unused materials.
	bool bImportUnusedMaterial = false;
	if (const UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::GetUniqueInstance(BaseNodeContainer))
	{
		SourceNode->GetCustomImportUnusedMaterial(bImportUnusedMaterial);
	}

#if !WITH_EDITOR
	// Can't import materials at runtime, fallback to instances
	if (MaterialImport == EInterchangeMaterialImportOption::ImportAsMaterials)
	{
		MaterialImport = EInterchangeMaterialImportOption::ImportAsMaterialInstances;
	}
#endif // !WITH_EDITOR

	if (MaterialImport == EInterchangeMaterialImportOption::ImportAsMaterials)
	{
		for (const UInterchangeShaderGraphNode* ShaderGraphNode : MaterialNodes)
		{
			UInterchangeBaseMaterialFactoryNode* MaterialBaseFactoryNode = nullptr;

			bool bIsAShaderFunction;
			if (ShaderGraphNode->GetCustomIsAShaderFunction(bIsAShaderFunction) && bIsAShaderFunction)
			{
				MaterialBaseFactoryNode = CreateMaterialFunctionFactoryNode(ShaderGraphNode);
			}
			else
			{
				MaterialBaseFactoryNode = CreateMaterialFactoryNode(ShaderGraphNode);
			}

			if (MaterialBaseFactoryNode)
			{
				MaterialBaseFactoryNode->SetEnabled(bImportUnusedMaterial);
			}
		}
	}
	else if (MaterialImport == EInterchangeMaterialImportOption::ImportAsMaterialInstances)
	{
		for (const UInterchangeShaderGraphNode* ShaderGraphNode : MaterialNodes)
		{
			if (UInterchangeMaterialInstanceFactoryNode* MaterialInstanceFactoryNode = CreateMaterialInstanceFactoryNode(ShaderGraphNode))
			{
				MaterialInstanceFactoryNode->SetEnabled(bImportUnusedMaterial);
			}
		}
	}
}

void UInterchangeGenericMaterialPipeline::ExecutePostFactoryPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedExecutePostFactoryPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}
}

void UInterchangeGenericMaterialPipeline::ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
{
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedExecutePostImportPipeline(InBaseNodeContainer, NodeKey, CreatedAsset, bIsAReimport);
	}
}

void UInterchangeGenericMaterialPipeline::SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex)
{
	if (TexturePipeline)
	{
		TexturePipeline->ScriptedSetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}
}

UInterchangeBaseMaterialFactoryNode* UInterchangeGenericMaterialPipeline::CreateBaseMaterialFactoryNode(const UInterchangeBaseNode* MaterialNode, TSubclassOf<UInterchangeBaseMaterialFactoryNode> NodeType)
{
	FString DisplayLabel = MaterialNode->GetDisplayLabel();
	const FString NodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(MaterialNode->GetUniqueID());
	UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = nullptr;
	if (BaseNodeContainer->IsNodeUidValid(NodeUid))
	{
		//The node already exist, just return it
		MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(BaseNodeContainer->GetFactoryNode(NodeUid));
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

bool UInterchangeGenericMaterialPipeline::IsSubsurfaceModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::Subsurface;

	const bool bHasSubsurfaceColorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SubsurfaceColor);

	return bHasSubsurfaceColorInput;
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

bool UInterchangeGenericMaterialPipeline::IsStandardSurfaceModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials;
	FString ShaderType;
	ShaderGraphNode->GetCustomShaderType(ShaderType);

	if(ShaderType == StandardSurface::Name.ToString())
	{
		return true;
	}

	return false;
}

bool UInterchangeGenericMaterialPipeline::IsSurfaceUnlitModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials;
	FString ShaderType;
	ShaderGraphNode->GetCustomShaderType(ShaderType);

	if(ShaderType == SurfaceUnlit::Name.ToString())
	{
		return true;
	}

	return false;
}

bool UInterchangeGenericMaterialPipeline::HandlePhongModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Phong;
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	if (IsPhongModel(ShaderGraphNode))
	{
		// ConvertFromDiffSpec function call
		const FString MaterialFunctionPath = TEXT("MaterialFunction'/Engine/Functions/Engine_MaterialFunctions01/Shading/ConvertFromDiffSpec.ConvertFromDiffSpec'");
		UInterchangeMaterialExpressionFactoryNode* FunctionCallExpression = CreateExpressionWithMaterialFunction(BaseNodeContainer, MaterialFactoryNode, TEXT("DiffSpecFunc"), MaterialFunctionPath);

		const FString FunctionCallExpressionUid = FunctionCallExpression->GetUniqueID();
		MaterialFactoryNode->ConnectOutputToBaseColor(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::BaseColor.ToString());
		MaterialFactoryNode->ConnectOutputToMetallic(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::Metallic.ToString());
		MaterialFactoryNode->ConnectOutputToSpecular(FunctionCallExpressionUid, UE::Interchange::Materials::PBR::Parameters::Specular.ToString());
		
		// Diffuse
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> DiffuseExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::DiffuseColor.ToString(), FunctionCallExpression->GetUniqueID());

			if (DiffuseExpressionFactoryNode.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FunctionCallExpression, Parameters::DiffuseColor.ToString(),
					DiffuseExpressionFactoryNode.Get<0>()->GetUniqueID(), DiffuseExpressionFactoryNode.Get<1>());
			}
		}

		// Specular Color
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> SpecularExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SpecularColor.ToString(), FunctionCallExpression->GetUniqueID());

			if (SpecularExpressionFactoryNode.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FunctionCallExpression, Parameters::SpecularColor.ToString(),
					SpecularExpressionFactoryNode.Get<0>()->GetUniqueID(), SpecularExpressionFactoryNode.Get<1>());
			}
		}
		
		// Shininess
		{
			const bool bHasShininessInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Shininess);
			if (bHasShininessInput)
			{
				TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

				TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ShininessExpressionFactoryNode =
					CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Shininess.ToString(), MaterialFactoryNode->GetUniqueID());

				if (ShininessExpressionFactoryNode.Get<0>())
				{
					UInterchangeMaterialExpressionFactoryNode* DivideShininessNode =
						CreateExpressionNode(TEXT("DivideShininess"), ShininessExpressionFactoryNode.Get<0>()->GetUniqueID(), UMaterialExpressionDivide::StaticClass());

					const float ShininessScale = 100.f; // Divide shininess by 100 to bring it into a 0-1 range for roughness.
					const FName ShininessScaleParameterName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, ConstB);
					DivideShininessNode->AddFloatAttribute(ShininessScaleParameterName, ShininessScale);
					DivideShininessNode->AddApplyAndFillDelegates<float>(ShininessScaleParameterName, UMaterialExpressionDivide::StaticClass(),  GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, ConstB));

					// Connect Shininess to Divide
					UInterchangeShaderPortsAPI::ConnectOuputToInputByName(DivideShininessNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionDivide, A).ToString(),
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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Roughness.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToRoughness(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	// Anisotropy
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Anisotropy);

		if(bHasInput)
		{
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Anisotropy.ToString(), MaterialFactoryNode->GetUniqueID());

			if(ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToAnisotropy(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Vector);

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

bool UInterchangeGenericMaterialPipeline::HandleSubsurface(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials::Subsurface;

	bool bShadingModelHandled = false;

	// Subsurface Color
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::SubsurfaceColor);

		if(bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::SubsurfaceColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if(ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToSubsurface(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			bShadingModelHandled = true;
		}
	}

	if(bShadingModelHandled)
	{
		MaterialFactoryNode->SetCustomShadingModel(EMaterialShadingModel::MSM_Subsurface);
		MaterialFactoryNode->SetCustomBlendMode(EBlendMode::BLEND_Opaque); // Opacity in Subsurface doesn't mean Translucency, according to UE doc
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
				MaterialFactoryNode->ConnectOutputToCloth(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
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

	// Two sidedness (ignored for thin translucency as it looks wrong)
	if (!IsThinTranslucentModel(ShaderGraphNode))
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
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Vector);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Normal.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToNormal(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}
		}
	}

	// Tangent
	{
		const bool bHasNormalInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Tangent);

		if(bHasNormalInput)
		{
			TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Vector);

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Tangent.ToString(), MaterialFactoryNode->GetUniqueID());

			if(ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToTangent(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}
		}
	}

	// Opacity
	{
		TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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
				}

				using namespace UE::Interchange::InterchangeGenericMaterialPipeline;

				Private::UpdateBlendModeBasedOnOpacityAttributes(ShaderGraphNode, MaterialFactoryNode);
			}
		}
	}

	// Ambient Occlusion
	{
		TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

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

	// Refraction
	// probably unlikely that someone will use both at same time but to keep backwards compability IndexOfRefraction will override this one
	{
		TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

		const bool bHasIorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::Refraction);

		if(bHasIorInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::Refraction.ToString(), MaterialFactoryNode->GetUniqueID());

			if(ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->SetCustomRefractionMethod(ERefractionMode::RM_IndexOfRefraction);
				MaterialFactoryNode->ConnectOutputToRefraction(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}
		}
	}

	// Index of Refraction (IOR)
	// We'll lerp between Air IOR (1) and the IOR from the shader graph based on a fresnel, as per UE doc on refraction.
	{
		TGuardValue<EMaterialInputType> InputTypeBeingProcessedGuard(MaterialCreationContext.InputTypeBeingProcessed, EMaterialInputType::Scalar);

		const bool bHasIorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::IndexOfRefraction);

		if (bHasIorInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Parameters::IndexOfRefraction.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->SetCustomRefractionMethod(ERefractionMode::RM_IndexOfRefraction);
				UInterchangeMaterialExpressionFactoryNode* IORLerp = CreateExpressionNode(TEXT("IORLerp"), ShaderGraphNode->GetUniqueID(), UMaterialExpressionLinearInterpolate::StaticClass());

				const float AirIOR = 1.f;
				const FName ConstAMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, ConstA);
				IORLerp->AddFloatAttribute(ConstAMemberName, AirIOR);
				IORLerp->AddApplyAndFillDelegates<float>(ConstAMemberName, UMaterialExpressionLinearInterpolate::StaticClass(), ConstAMemberName);

				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(IORLerp, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, B).ToString(),
					ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());

				UInterchangeMaterialExpressionFactoryNode* IORFresnel = CreateExpressionNode(TEXT("IORFresnel"), ShaderGraphNode->GetUniqueID(), UMaterialExpressionFresnel::StaticClass());

				UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(IORLerp, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, Alpha).ToString(), IORFresnel->GetUniqueID());

				MaterialFactoryNode->ConnectToRefraction(IORLerp->GetUniqueID());
			}
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleFlattenNormalNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode,
	UInterchangeMaterialExpressionFactoryNode* FlattenNormalFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::FlattenNormal;
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	if (!FlattenNormalFactoryNode)
	{
		return;
	}

	const FString MaterialFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal");
	UpdateFunctionCallExpression(*FlattenNormalFactoryNode, MaterialFunctionPath);

	// Normal
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> NormalExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Normal.ToString(), FlattenNormalFactoryNode->GetUniqueID());

		if (NormalExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FlattenNormalFactoryNode, TEXT("Normal"),
				NormalExpression.Get<0>()->GetUniqueID(), NormalExpression.Get<1>());
		}
	}

	// Flatness
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> FlatnessExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Flatness.ToString(), FlattenNormalFactoryNode->GetUniqueID());

		if (FlatnessExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FlattenNormalFactoryNode, TEXT("Flatness"),
				FlatnessExpression.Get<0>()->GetUniqueID(), FlatnessExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleNormalFromHeightMapNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* NormalFromHeightMapFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::NormalFromHeightMap;
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	if (!NormalFromHeightMapFactoryNode)
	{
		return;
	}

	const FString MaterialFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions03/Procedurals/NormalFromHeightmap.NormalFromHeightmap");
	UpdateFunctionCallExpression(*NormalFromHeightMapFactoryNode, MaterialFunctionPath);

	// Heightmap
	{
		const FString HeightMapInput = Inputs::HeightMap.ToString();
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> HeightMapExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, HeightMapInput, NormalFromHeightMapFactoryNode->GetUniqueID());

		if(HeightMapExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NormalFromHeightMapFactoryNode, HeightMapInput, HeightMapExpression.Get<0>()->GetUniqueID(), HeightMapExpression.Get<1>());
		}
	}

	// Intensity
	{
		const FString Intensity = Inputs::Intensity.ToString();
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> IntensityExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Intensity, NormalFromHeightMapFactoryNode->GetUniqueID());

		if(IntensityExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NormalFromHeightMapFactoryNode, Intensity, IntensityExpression.Get<0>()->GetUniqueID(), IntensityExpression.Get<1>());
		}
	}

	// Offset
	{
		const FString Offset = Inputs::Offset.ToString();
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> OffsetExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Offset, NormalFromHeightMapFactoryNode->GetUniqueID());

		if(OffsetExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NormalFromHeightMapFactoryNode, Offset, OffsetExpression.Get<0>()->GetUniqueID(), OffsetExpression.Get<1>());
		}
	}

	// Coordinates
	{
		const FString Coordinates = Inputs::Coordinates.ToString();
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> CoordinatesExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Coordinates, NormalFromHeightMapFactoryNode->GetUniqueID());

		if(CoordinatesExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NormalFromHeightMapFactoryNode, Coordinates, CoordinatesExpression.Get<0>()->GetUniqueID(), CoordinatesExpression.Get<1>());
		}
	}

	// Channel
	{
		const FString Channel = Inputs::Channel.ToString();
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ChannelExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Channel, NormalFromHeightMapFactoryNode->GetUniqueID());

		if(ChannelExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NormalFromHeightMapFactoryNode, Channel, ChannelExpression.Get<0>()->GetUniqueID(), ChannelExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleMakeFloat3Node(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode,
	UInterchangeMaterialExpressionFactoryNode* MakeFloat3FactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::MakeFloat3;
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	if (!MakeFloat3FactoryNode)
	{
		return;
	}

	const FString MaterialFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions02/Utility/MakeFloat3.MakeFloat3");
	UpdateFunctionCallExpression(*MakeFloat3FactoryNode, MaterialFunctionPath);

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> RedChannelExpression =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::X.ToString(), MakeFloat3FactoryNode->GetUniqueID());
	if (RedChannelExpression.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MakeFloat3FactoryNode, TEXT("X"),
			RedChannelExpression.Get<0>()->GetUniqueID(), RedChannelExpression.Get<1>());
	}

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> GreenChannelExpression =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Y.ToString(), MakeFloat3FactoryNode->GetUniqueID());
	if (GreenChannelExpression.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MakeFloat3FactoryNode, TEXT("Y"),
			GreenChannelExpression.Get<0>()->GetUniqueID(), GreenChannelExpression.Get<1>());
	}

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> BlueChannelExpression =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Z.ToString(), MakeFloat3FactoryNode->GetUniqueID());
	if (BlueChannelExpression.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MakeFloat3FactoryNode, TEXT("Z"),
			BlueChannelExpression.Get<0>()->GetUniqueID(), BlueChannelExpression.Get<1>());
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureNode(const UInterchangeTextureNode* TextureNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureBaseFactoryNode, const FString & ExpressionClassName)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::TextureSample;

	FString TextureFactoryUid;
	TArray<FString> TextureTargetNodes;
	TextureNode->GetTargetNodeUids(TextureTargetNodes);

	if(TextureTargetNodes.Num() > 0)
	{
		TextureFactoryUid = TextureTargetNodes[0];
	}

	TextureBaseFactoryNode->SetCustomExpressionClassName(ExpressionClassName);
	TextureBaseFactoryNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureFactoryUid);

	if(UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(BaseNodeContainer->GetFactoryNode(TextureFactoryUid)))
	{
		EMaterialInputType TextureUsage = EMaterialInputType::Unknown;
		TextureFactoryNode->GetAttribute(TEXT("TextureUsage"), TextureUsage);

		const bool bIsOutputLinear = MaterialExpressionCreationContextStack.Top().OutputName.Equals(Outputs::A.ToString());
		const EMaterialInputType DesiredTextureUsage = MaterialCreationContext.InputTypeBeingProcessed == EMaterialInputType::Scalar && bIsOutputLinear ?
			EMaterialInputType::Unknown : // Alpha channels are always in linear space so ignore them when determining texture usage
			MaterialCreationContext.InputTypeBeingProcessed;

		if(TextureUsage == EMaterialInputType::Unknown)
		{
			if(DesiredTextureUsage == EMaterialInputType::Vector)
			{
				TextureFactoryNode->SetCustomCompressionSettings(TextureCompressionSettings::TC_Normalmap);
			}
			else if(DesiredTextureUsage == EMaterialInputType::Scalar)
			{
				bool bSRGB;
				if(!TextureNode->GetCustomSRGB(bSRGB))
				{
					// Only set CustomSRGB if it wasn't set by the InterchangeGenericTexturePipeline before
					TextureFactoryNode->SetCustomSRGB(false);
				}
			}

			TextureFactoryNode->SetAttribute(TEXT("TextureUsage"), DesiredTextureUsage);
		}
		else if(TextureUsage != DesiredTextureUsage && DesiredTextureUsage != EMaterialInputType::Unknown)
		{
			UInterchangeResultWarning_Generic* TextureUsageWarning = AddMessage<UInterchangeResultWarning_Generic>();
			TextureUsageWarning->DestinationAssetName = TextureFactoryNode->GetAssetName();
			TextureUsageWarning->AssetType = TextureFactoryNode->GetObjectClass();

			TextureUsageWarning->Text = FText::Format(LOCTEXT("TextureUsageMismatch", "{0} is being used as both {1} and {2} which aren't compatible."),
													  FText::FromString(TextureFactoryNode->GetAssetName()), FText::FromString(LexToString(TextureUsage)), FText::FromString(LexToString(DesiredTextureUsage)));

			// Flipping the green channel only makes sense for vector data as it's used to compensate for different handedness.
			// Clear it if we're not gonna be used only as a vector map. This normally happens when a normal map is also used as a color map.
			bool bFlipGreenChannel;
			if(TextureFactoryNode->GetCustombFlipGreenChannel(bFlipGreenChannel))
			{
				TextureFactoryNode->SetCustombFlipGreenChannel(false);
			}
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureObjectNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureObjectFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::TextureObject;

	FString TextureUid;
	ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureUid);
	FString ExpressionClassName;
	FString TextureFactoryUid;

	if(const UInterchangeTextureNode* TextureNode = Cast<const UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
	{
		HandleTextureNode(TextureNode, MaterialFactoryNode, TextureObjectFactoryNode, UMaterialExpressionTextureObject::StaticClass()->GetName());
	}
	else
	{
		TextureObjectFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTextureObject::StaticClass()->GetName());
		TextureObjectFactoryNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureFactoryUid);
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureSampleNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes::TextureSample;

	FString TextureUid;
	ShaderNode->GetStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureUid);

	FString ExpressionClassName;
	FString TextureFactoryUid;

	if (const UInterchangeTextureNode* TextureNode = Cast<const UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
	{
		if (TextureNode->IsA<UInterchangeTextureCubeNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameterCube::StaticClass()->GetName();
		}
		else if (TextureNode->IsA<UInterchangeTexture2DArrayNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2DArray::StaticClass()->GetName();
		}
		else if(TextureNode->IsA<UInterchangeTextureBlurNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameterBlur::StaticClass()->GetName();
		}
		else if (TextureNode->IsA<UInterchangeTexture2DNode>())
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2D::StaticClass()->GetName();
		}
		else
		{
			ExpressionClassName = UMaterialExpressionTextureSampleParameter2D::StaticClass()->GetName();
		}

		HandleTextureNode(TextureNode, MaterialFactoryNode, TextureSampleFactoryNode, ExpressionClassName);
	}
	else
	{
		TextureSampleFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTextureSampleParameter2D::StaticClass()->GetName());
		TextureSampleFactoryNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Inputs::Texture.ToString()), TextureFactoryUid);
	}

	// Coordinates
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> CoordinatesExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Inputs::Coordinates.ToString(), TextureSampleFactoryNode->GetUniqueID());

		if (CoordinatesExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(TextureSampleFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, Coordinates).ToString(),
				CoordinatesExpression.Get<0>()->GetUniqueID(), CoordinatesExpression.Get<1>());
		}
	}

	if(ExpressionClassName == UMaterialExpressionTextureSampleParameterBlur::StaticClass()->GetName())
	{
		HandleTextureSampleBlurNode(ShaderNode, MaterialFactoryNode, TextureSampleFactoryNode);
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureSampleBlurNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TextureSampleFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	// KernelSize
	if(float KernelSize; ShaderNode->GetFloatAttribute(TextureSampleBlur::Attributes::KernelSize, KernelSize))
	{
		const FName KernelSizeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSampleParameterBlur, KernelSize);
		TextureSampleFactoryNode->AddFloatAttribute(KernelSizeMemberName, KernelSize);
		TextureSampleFactoryNode->AddApplyAndFillDelegates<float>(KernelSizeMemberName, UMaterialExpressionTextureSampleParameterBlur::StaticClass(), KernelSizeMemberName);
	}

	// FilterSize
	if(float FilterSize; ShaderNode->GetFloatAttribute(TextureSampleBlur::Attributes::FilterSize, FilterSize))
	{
		const FName FilterSizeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSampleParameterBlur, FilterSize);
		TextureSampleFactoryNode->AddFloatAttribute(FilterSizeMemberName, FilterSize);
		TextureSampleFactoryNode->AddApplyAndFillDelegates<float>(FilterSizeMemberName, UMaterialExpressionTextureSampleParameterBlur::StaticClass(), FilterSizeMemberName);
	}

	// FilterOffset
	if(float FilterOffset; ShaderNode->GetFloatAttribute(TextureSampleBlur::Attributes::FilterOffset, FilterOffset))
	{
		const FName FilterOffsetMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSampleParameterBlur, FilterOffset);
		TextureSampleFactoryNode->AddFloatAttribute(FilterOffsetMemberName, FilterOffset);
		TextureSampleFactoryNode->AddApplyAndFillDelegates<float>(FilterOffsetMemberName, UMaterialExpressionTextureSampleParameterBlur::StaticClass(), FilterOffsetMemberName);
	}

	// Filter
	if(int32 Filter; ShaderNode->GetInt32Attribute(TextureSampleBlur::Attributes::Filter, Filter))
	{
		const FName FilterMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSampleParameterBlur, Filter);
		TextureSampleFactoryNode->AddInt32Attribute(FilterMemberName, Filter);
		TextureSampleFactoryNode->AddApplyAndFillDelegates<int>(FilterMemberName, UMaterialExpressionTextureSampleParameterBlur::StaticClass(), FilterMemberName);
	}
}

void UInterchangeGenericMaterialPipeline::HandleTextureCoordinateNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode,
	UInterchangeMaterialExpressionFactoryNode*& TexCoordFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	TexCoordFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTextureCoordinate::StaticClass()->GetName());

	// Index
	{
		int32 CoordIndex;
		if (ShaderNode->GetInt32Attribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Nodes::TextureCoordinate::Inputs::Index.ToString()), CoordIndex))
		{
			const FName CoordinateIndexMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, CoordinateIndex);
			TexCoordFactoryNode->AddInt32Attribute(CoordinateIndexMemberName, CoordIndex);
			TexCoordFactoryNode->AddApplyAndFillDelegates<int32>(CoordinateIndexMemberName, UMaterialExpressionTextureCoordinate::StaticClass(), CoordinateIndexMemberName);
		}
	}

	// U tiling
	{
		TVariant<FString, FLinearColor, float> UTilingValue = VisitShaderInput(ShaderNode, Nodes::TextureCoordinate::Inputs::UTiling.ToString());

		if (UTilingValue.IsType<float>())
		{
			const FName UTilingMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, UTiling);
			TexCoordFactoryNode->AddFloatAttribute(UTilingMemberName, UTilingValue.Get<float>());
			TexCoordFactoryNode->AddApplyAndFillDelegates<float>(UTilingMemberName, UMaterialExpressionTextureCoordinate::StaticClass(), UTilingMemberName);
		}
	}

	// V tiling
	{
		TVariant<FString, FLinearColor, float> VTilingValue = VisitShaderInput(ShaderNode, Nodes::TextureCoordinate::Inputs::VTiling.ToString());

		if (VTilingValue.IsType<float>())
		{
			const FName VTilingMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureCoordinate, VTiling);
			TexCoordFactoryNode->AddFloatAttribute(VTilingMemberName, VTilingValue.Get<float>());
			TexCoordFactoryNode->AddApplyAndFillDelegates<float>(VTilingMemberName, UMaterialExpressionTextureCoordinate::StaticClass(), VTilingMemberName);
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
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MultiplyExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionMultiply, B).ToString(),
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

			const FString MaterialFunctionPath = TEXT("/Engine/Functions/Engine_MaterialFunctions02/Texturing/CustomRotator.CustomRotator");
			UpdateFunctionCallExpression(*CallRotatorExpression, MaterialFunctionPath);

			UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(CallRotatorExpression, TEXT("UVs"), TexCoordFactoryNode->GetUniqueID());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(CallRotatorExpression, TEXT("Rotation Angle (0-1)"), RotateExpression.Get<0>()->GetUniqueID(), RotateExpression.Get<1>());

			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> RotationCenterExpression =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::TextureCoordinate::Inputs::RotationCenter.ToString(), TexCoordFactoryNode->GetUniqueID());

			if (RotationCenterExpression.Get<0>())
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(CallRotatorExpression, TEXT("Rotation Center"), RotationCenterExpression.Get<0>()->GetUniqueID(), RotationCenterExpression.Get<1>());
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
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(AddExpression, GET_MEMBER_NAME_CHECKED(UMaterialExpressionAdd, B).ToString(),
				OffsetExpression.Get<0>()->GetUniqueID(), OffsetExpression.Get<1>());

			TexCoordFactoryNode = AddExpression;
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleLerpNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* LerpFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard;

	LerpFactoryNode->SetCustomExpressionClassName(UMaterialExpressionLinearInterpolate::StaticClass()->GetName());

	// A
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorAExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::A.ToString(), LerpFactoryNode->GetUniqueID());

		if (ColorAExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, A).ToString(),
				ColorAExpression.Get<0>()->GetUniqueID(), ColorAExpression.Get<1>());
		}
	}
	
	// B
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ColorBExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::B.ToString(), LerpFactoryNode->GetUniqueID());

		if (ColorBExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, B).ToString(),
				ColorBExpression.Get<0>()->GetUniqueID(), ColorBExpression.Get<1>());
		}
	}
	
	// Factor
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> FactorExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Nodes::Lerp::Inputs::Factor.ToString(), LerpFactoryNode->GetUniqueID());

		if (FactorExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(LerpFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionLinearInterpolate, Alpha).ToString(),
				FactorExpression.Get<0>()->GetUniqueID(), FactorExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleMaskNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* MaskFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	MaskFactoryNode->SetCustomExpressionClassName(UMaterialExpressionComponentMask::StaticClass()->GetName());

	bool bRChannel = false;
	ShaderNode->GetBooleanAttribute(Mask::Attributes::R, bRChannel);
	bool bGChannel = false;
	ShaderNode->GetBooleanAttribute(Mask::Attributes::G, bGChannel);
	bool bBChannel = false;
	ShaderNode->GetBooleanAttribute(Mask::Attributes::B, bBChannel);
	bool bAChannel = false;
	ShaderNode->GetBooleanAttribute(Mask::Attributes::A, bAChannel);
	bool bIsAnyMaskChannelSet = bRChannel || bGChannel || bBChannel || bAChannel;

	if(bIsAnyMaskChannelSet)
	{
		// R
		{
			const FName RMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, R);
			MaskFactoryNode->AddBooleanAttribute(RMemberName, bRChannel);
			MaskFactoryNode->AddApplyAndFillDelegates<bool>(RMemberName, UMaterialExpressionComponentMask::StaticClass(), RMemberName);
		}

		// G
		{
			const FName GMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, G);
			MaskFactoryNode->AddBooleanAttribute(GMemberName, bGChannel);
			MaskFactoryNode->AddApplyAndFillDelegates<bool>(GMemberName, UMaterialExpressionComponentMask::StaticClass(), GMemberName);
		}

		// B
		{
			const FName BMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, B);
			MaskFactoryNode->AddBooleanAttribute(BMemberName, bBChannel);
			MaskFactoryNode->AddApplyAndFillDelegates<bool>(BMemberName, UMaterialExpressionComponentMask::StaticClass(), BMemberName);
		}

		// A
		{
			const FName AMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, A);
			MaskFactoryNode->AddBooleanAttribute(AMemberName, bAChannel);
			MaskFactoryNode->AddApplyAndFillDelegates<bool>(AMemberName, UMaterialExpressionComponentMask::StaticClass(), AMemberName);
		}
	}

	// Input
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> InputExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Mask::Inputs::Input.ToString(), MaskFactoryNode->GetUniqueID());

		if(InputExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MaskFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, Input).ToString(),
				InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
		}
	}
}

void UInterchangeGenericMaterialPipeline::HandleTimeNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TimeFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TimeFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTime::StaticClass()->GetName());

	// IgnorePause
	if(bool bIgnorePause; ShaderNode->GetBooleanAttribute(Time::Attributes::IgnorePause, bIgnorePause))
	{
		const FName IgnorePauseMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTime, bIgnorePause);
		TimeFactoryNode->AddBooleanAttribute(IgnorePauseMemberName, bIgnorePause);
		TimeFactoryNode->AddApplyAndFillDelegates<bool>(IgnorePauseMemberName, UMaterialExpressionTime::StaticClass(), IgnorePauseMemberName);
	}

	// OverridePeriod
	if(bool bOverridePeriod; ShaderNode->GetBooleanAttribute(Time::Attributes::OverridePeriod, bOverridePeriod))
	{
		const FName OverridePeriodMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTime, bOverride_Period);
		TimeFactoryNode->AddBooleanAttribute(OverridePeriodMemberName, bOverridePeriod);
		TimeFactoryNode->AddApplyAndFillDelegates<bool>(OverridePeriodMemberName, UMaterialExpressionTime::StaticClass(), OverridePeriodMemberName);
	}

	// Period
	if(float Period; ShaderNode->GetFloatAttribute(Time::Attributes::Period, Period))
	{
		const FName PeriodMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTime, Period);
		TimeFactoryNode->AddFloatAttribute(PeriodMemberName, Period);
		TimeFactoryNode->AddApplyAndFillDelegates<float>(PeriodMemberName, UMaterialExpressionTime::StaticClass(), PeriodMemberName);
	}
}

void UInterchangeGenericMaterialPipeline::HandleTransformPositionNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TransformPositionFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TransformPositionFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTransformPosition::StaticClass()->GetName());

	// Input
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> InputExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, TransformPosition::Inputs::Input.ToString(), TransformPositionFactoryNode->GetUniqueID());

		if(InputExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(TransformPositionFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransformPosition, Input).ToString(),
															InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
		}
	}

	// TransformSourceType
	if(int32 TransformSourceType; ShaderNode->GetInt32Attribute(TransformPosition::Attributes::TransformSourceType, TransformSourceType))
	{
		const FName TransformSourceTypeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransformPosition, TransformSourceType);
		TransformPositionFactoryNode->AddInt32Attribute(TransformSourceTypeMemberName,TransformSourceType);
		TransformPositionFactoryNode->AddApplyAndFillDelegates<int32>(TransformSourceTypeMemberName, UMaterialExpressionTransformPosition::StaticClass(), TransformSourceTypeMemberName);
	}

	// TransformType
	if(int32 TransformType; ShaderNode->GetInt32Attribute(TransformPosition::Attributes::TransformType, TransformType))
	{
		const FName TransformTypeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransformPosition, TransformType);
		TransformPositionFactoryNode->AddInt32Attribute(TransformTypeMemberName, TransformType);
		TransformPositionFactoryNode->AddApplyAndFillDelegates<int32>(TransformTypeMemberName, UMaterialExpressionTransformPosition::StaticClass(), TransformTypeMemberName);
	}
}

void UInterchangeGenericMaterialPipeline::HandleTransformVectorNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* TransformVectorFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	TransformVectorFactoryNode->SetCustomExpressionClassName(UMaterialExpressionTransform::StaticClass()->GetName());

	// Input
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> InputExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, TransformVector::Inputs::Input.ToString(), TransformVectorFactoryNode->GetUniqueID());

		if(InputExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(TransformVectorFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransform, Input).ToString(),
															InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
		}
	}

	// TransformSourceType
	if(int32 TransformSourceType; ShaderNode->GetInt32Attribute(TransformVector::Attributes::TransformSourceType, TransformSourceType))
	{
		const FName TransformSourceTypeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransform, TransformSourceType);
		TransformVectorFactoryNode->AddInt32Attribute(TransformSourceTypeMemberName, TransformSourceType);
		TransformVectorFactoryNode->AddApplyAndFillDelegates<int32>(TransformSourceTypeMemberName, UMaterialExpressionTransform::StaticClass(), TransformSourceTypeMemberName);
	}

	// TransformType
	if(int32 TransformType; ShaderNode->GetInt32Attribute(TransformVector::Attributes::TransformType, TransformType))
	{
		const FName TransformTypeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionTransform, TransformType);
		TransformVectorFactoryNode->AddInt32Attribute(TransformTypeMemberName, TransformType);
		TransformVectorFactoryNode->AddApplyAndFillDelegates<int32>(TransformTypeMemberName, UMaterialExpressionTransform::StaticClass(), TransformTypeMemberName);
	}
}

void UInterchangeGenericMaterialPipeline::HandleNoiseNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* NoiseFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	NoiseFactoryNode->SetCustomExpressionClassName(UMaterialExpressionNoise::StaticClass()->GetName());

	// Position
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> PositionExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Noise::Inputs::Position.ToString(), NoiseFactoryNode->GetUniqueID());

		if(PositionExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NoiseFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, Position).ToString(),
															PositionExpression.Get<0>()->GetUniqueID(), PositionExpression.Get<1>());
		}
	}

	// FilterWidth
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> FilterWidthExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, Noise::Inputs::FilterWidth.ToString(), NoiseFactoryNode->GetUniqueID());

		if(FilterWidthExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NoiseFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, FilterWidth).ToString(),
															FilterWidthExpression.Get<0>()->GetUniqueID(), FilterWidthExpression.Get<1>());
		}
	}

	// Scale
	if(float Scale; ShaderNode->GetFloatAttribute(Noise::Attributes::Scale, Scale))
	{
		const FName ScaleMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, Scale);
		NoiseFactoryNode->AddFloatAttribute(ScaleMemberName, Scale);
		NoiseFactoryNode->AddApplyAndFillDelegates<float>(ScaleMemberName, UMaterialExpressionNoise::StaticClass(), ScaleMemberName);
	}

	// Quality
	if(int32 Quality; ShaderNode->GetInt32Attribute(Noise::Attributes::Quality, Quality))
	{
		const FName QualityMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, Quality);
		NoiseFactoryNode->AddInt32Attribute(QualityMemberName, Quality);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(QualityMemberName, UMaterialExpressionNoise::StaticClass(), QualityMemberName);
	}

	// Noise Function
	if(int32 NoiseFunction; ShaderNode->GetInt32Attribute(Noise::Attributes::Function, NoiseFunction))
	{
		const FName NoiseFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, NoiseFunction);
		NoiseFactoryNode->AddInt32Attribute(NoiseFunctionMemberName, NoiseFunction);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(NoiseFunctionMemberName, UMaterialExpressionNoise::StaticClass(), NoiseFunctionMemberName);
	}

	// Turbulence
	if(bool bTurbulence; ShaderNode->GetBooleanAttribute(Noise::Attributes::Turbulence, bTurbulence))
	{
		const FName TurbulenceMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, bTurbulence);
		NoiseFactoryNode->AddBooleanAttribute(TurbulenceMemberName, bTurbulence);
		NoiseFactoryNode->AddApplyAndFillDelegates<bool>(TurbulenceMemberName, UMaterialExpressionNoise::StaticClass(), TurbulenceMemberName);
	}

	// Levels
	if(int32 Levels; ShaderNode->GetInt32Attribute(Noise::Attributes::Levels, Levels))
	{
		const FName LevelsMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, Levels);
		NoiseFactoryNode->AddInt32Attribute(LevelsMemberName, Levels);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(LevelsMemberName, UMaterialExpressionNoise::StaticClass(), LevelsMemberName);
	}

	// Output Min
	if(float OutputMin; ShaderNode->GetFloatAttribute(Noise::Attributes::OutputMin, OutputMin))
	{
		const FName OutputMinMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, OutputMin);
		NoiseFactoryNode->AddFloatAttribute(OutputMinMemberName, OutputMin);
		NoiseFactoryNode->AddApplyAndFillDelegates<float>(OutputMinMemberName, UMaterialExpressionNoise::StaticClass(), OutputMinMemberName);
	}

	// Output Max
	if(float OutputMax; ShaderNode->GetFloatAttribute(Noise::Attributes::OutputMax, OutputMax))
	{
		const FName OutputMaxMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, OutputMax);
		NoiseFactoryNode->AddFloatAttribute(OutputMaxMemberName, OutputMax);
		NoiseFactoryNode->AddApplyAndFillDelegates<float>(OutputMaxMemberName, UMaterialExpressionNoise::StaticClass(), OutputMaxMemberName);
	}

	// Level Scale
	if(float LevelScale; ShaderNode->GetFloatAttribute(Noise::Attributes::LevelScale, LevelScale))
	{
		const FName LevelScaleMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, LevelScale);
		NoiseFactoryNode->AddFloatAttribute(LevelScaleMemberName, LevelScale);
		NoiseFactoryNode->AddApplyAndFillDelegates<float>(LevelScaleMemberName, UMaterialExpressionNoise::StaticClass(), LevelScaleMemberName);
	}

	// Tiling
	if(bool bTiling; ShaderNode->GetBooleanAttribute(Noise::Attributes::Tiling, bTiling))
	{
		const FName TilingMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, bTiling);
		NoiseFactoryNode->AddBooleanAttribute(TilingMemberName, bTiling);
		NoiseFactoryNode->AddApplyAndFillDelegates<bool>(TilingMemberName, UMaterialExpressionNoise::StaticClass(), TilingMemberName);
	}

	// Levels
	if(int32 RepeatSize; ShaderNode->GetInt32Attribute(Noise::Attributes::RepeatSize, RepeatSize))
	{
		const FName RepeatSizeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, RepeatSize);
		NoiseFactoryNode->AddInt32Attribute(RepeatSizeMemberName, RepeatSize);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(RepeatSizeMemberName, UMaterialExpressionNoise::StaticClass(), RepeatSizeMemberName);
	}
}

void UInterchangeGenericMaterialPipeline::HandleVectorNoiseNode(const UInterchangeShaderNode* ShaderNode, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, UInterchangeMaterialExpressionFactoryNode* NoiseFactoryNode)
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	NoiseFactoryNode->SetCustomExpressionClassName(UMaterialExpressionVectorNoise::StaticClass()->GetName());

	// Position
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> PositionExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, VectorNoise::Inputs::Position.ToString(), NoiseFactoryNode->GetUniqueID());

		if(PositionExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(NoiseFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, Position).ToString(),
															PositionExpression.Get<0>()->GetUniqueID(), PositionExpression.Get<1>());
		}
	}

	// Noise Function
	if(int32 NoiseFunction; ShaderNode->GetInt32Attribute(VectorNoise::Attributes::Function, NoiseFunction))
	{
		const FName NoiseFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, NoiseFunction);
		NoiseFactoryNode->AddInt32Attribute(NoiseFunctionMemberName, NoiseFunction);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(NoiseFunctionMemberName, UMaterialExpressionVectorNoise::StaticClass(), NoiseFunctionMemberName);
	}

	// Quality
	if(int32 Quality; ShaderNode->GetInt32Attribute(VectorNoise::Attributes::Quality, Quality))
	{
		const FName QualityMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, Quality);
		NoiseFactoryNode->AddInt32Attribute(QualityMemberName, Quality);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(QualityMemberName, UMaterialExpressionVectorNoise::StaticClass(), QualityMemberName);
	}

	// Tiling
	if(bool bTiling; ShaderNode->GetBooleanAttribute(VectorNoise::Attributes::Tiling, bTiling))
	{
		const FName TilingMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, bTiling);
		NoiseFactoryNode->AddBooleanAttribute(TilingMemberName, bTiling);
		NoiseFactoryNode->AddApplyAndFillDelegates<bool>(TilingMemberName, UMaterialExpressionVectorNoise::StaticClass(), TilingMemberName);
	}

	// Tile Size
	if(int32 TileSize; ShaderNode->GetInt32Attribute(VectorNoise::Attributes::Function, TileSize))
	{
		const FName TileSizeMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, TileSize);
		NoiseFactoryNode->AddInt32Attribute(TileSizeMemberName, TileSize);
		NoiseFactoryNode->AddApplyAndFillDelegates<int32>(TileSizeMemberName, UMaterialExpressionVectorNoise::StaticClass(), TileSizeMemberName);
	}
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialExpressionForShaderNode(UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode,
	const UInterchangeShaderNode* ShaderNode, const FString& ParentUid)
{
	using namespace UE::Interchange::Materials::Standard;

	// If we recognize the shader node type
	// - Create material expression for specific node type
	//
	// If we don't recognize the shader node type
	// - Create material expression by trying to match the node type to a material expression class name

	const FString MaterialExpressionUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(ShaderNode->GetUniqueID());

	UInterchangeMaterialExpressionFactoryNode* MaterialExpression = Cast<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer->GetFactoryNode(MaterialExpressionUid));
	if (MaterialExpression != nullptr)
	{
		return MaterialExpression;
	}

	// Create function call expression if applicable
	MaterialExpression = CreateFunctionCallExpression(ShaderNode, MaterialExpressionUid, MaterialFactoryNode);
	if (MaterialExpression)
	{
		return MaterialExpression;
	}

	MaterialExpression = NewObject<UInterchangeMaterialExpressionFactoryNode>(BaseNodeContainer);
	if (!MaterialExpression)
	{
		return nullptr;
	}

	FString ShaderType;
	ShaderNode->GetCustomShaderType(ShaderType);

	MaterialExpression->InitializeNode(MaterialExpressionUid, ShaderNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
	BaseNodeContainer->AddNode(MaterialExpression);

	if (*ShaderType == Nodes::FlattenNormal::Name)
	{
		HandleFlattenNormalNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::MakeFloat3::Name)
	{
		HandleMakeFloat3Node(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::Lerp::Name)
	{
		HandleLerpNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::Mask::Name)
	{
		HandleMaskNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::Noise::Name)
	{
		HandleNoiseNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::NormalFromHeightMap::Name)
	{
		HandleNormalFromHeightMapNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::TextureCoordinate::Name)
	{
		HandleTextureCoordinateNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::TextureObject::Name)
	{
		HandleTextureObjectNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (*ShaderType == Nodes::TextureSample::Name)
	{
		HandleTextureSampleNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::Time::Name)
	{
		HandleTimeNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::TransformPosition::Name)
	{
		HandleTransformPositionNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::TransformVector::Name)
	{
		HandleTransformVectorNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if(*ShaderType == Nodes::VectorNoise::Name)
	{
		HandleVectorNoiseNode(ShaderNode, MaterialFactoryNode, MaterialExpression);
	}
	else if (ensure(!ShaderType.IsEmpty()))
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
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MaterialExpression, InputName, InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
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
		if (const UInterchangeTextureNode* TextureNode = Cast<const UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
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
		MaterialExpressionFactoryNode->AddFloatAttribute(DefaultValueMemberName, InputValue);
		MaterialExpressionFactoryNode->AddApplyAndFillDelegates<float>(DefaultValueMemberName, UMaterialExpressionScalarParameter::StaticClass(), DefaultValueMemberName);
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
		MaterialExpressionFactoryNode->AddLinearColorAttribute(DefaultValueMemberName, InputValue);
		MaterialExpressionFactoryNode->AddApplyAndFillDelegates<FLinearColor>(DefaultValueMemberName, UMaterialExpressionVectorParameter::StaticClass(), DefaultValueMemberName);
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
		VectorParameterFactoryNode->AddLinearColorAttribute(DefaultValueMemberName, FLinearColor(InputValue.X, InputValue.Y, 0.f));
		VectorParameterFactoryNode->AddApplyAndFillDelegates<FLinearColor>(DefaultValueMemberName, UMaterialExpressionVectorParameter::StaticClass(), DefaultValueMemberName);

		// Defaults to R&G
		UInterchangeMaterialExpressionFactoryNode* ComponentMaskFactoryNode = CreateExpressionNode(InputName + TEXT("_Mask"), ParentUid, UMaterialExpressionComponentMask::StaticClass());

		UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ComponentMaskFactoryNode, GET_MEMBER_NAME_CHECKED(UMaterialExpressionComponentMask, Input).ToString(),
			VectorParameterFactoryNode->GetUniqueID() );

		return ComponentMaskFactoryNode;
	}

	return nullptr;
}

TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> UInterchangeGenericMaterialPipeline::CreateMaterialExpressionForInput(UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode, const UInterchangeShaderNode* ShaderNode, const FString& InputName, const FString& ParentUid)
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

	int32 ExpressionContextIndex = MaterialExpressionCreationContextStack.AddDefaulted();

	FString ConnectedShaderNodeUid;
	if (UInterchangeShaderPortsAPI::GetInputConnection(ShaderNode, InputName, ConnectedShaderNodeUid, MaterialExpressionCreationContextStack[ExpressionContextIndex].OutputName))
	{
		if (const UInterchangeShaderNode* ConnectedShaderNode = Cast<const UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
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

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> Result {MaterialExpressionFactoryNode, MaterialExpressionCreationContextStack[ExpressionContextIndex].OutputName};
	MaterialExpressionCreationContextStack.Pop();

	return Result;
}

UInterchangeMaterialFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode)
{
	UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>( CreateBaseMaterialFactoryNode(ShaderGraphNode, UInterchangeMaterialFactoryNode::StaticClass()) );

	// Handle the case where the material will be connected through the material attributes input
	if (HandleBxDFInput(ShaderGraphNode, MaterialFactoryNode))
	{
		// No need to proceed any further
		return MaterialFactoryNode;
	}

	if (HandleUnlitModel(ShaderGraphNode, MaterialFactoryNode))
	{
		// No need to proceed any further
		return MaterialFactoryNode;
	}

	if (!HandlePhongModel(ShaderGraphNode, MaterialFactoryNode))
	{
		HandleLambertModel(ShaderGraphNode, MaterialFactoryNode);
	}

	HandlePBRModel(ShaderGraphNode, MaterialFactoryNode); // Always process the PBR parameters. If they were already assigned from Phong or Lambert, they will be ignored.
	
	// Can't have different shading models
	// Favor translucency over coats (clear coat, sheen, etc.) since it tends to have a bigger impact visually
	if (!HandleThinTranslucent(ShaderGraphNode, MaterialFactoryNode))
	{
		if (!HandleClearCoat(ShaderGraphNode, MaterialFactoryNode))
		{
			if(!HandleSheen(ShaderGraphNode, MaterialFactoryNode))
			{
				HandleSubsurface(ShaderGraphNode, MaterialFactoryNode);
			}
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
	else if(IsSubsurfaceModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/SubsurfaceMaterial.SubsurfaceMaterial'"));
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
	else if (IsUnlitModel(ShaderGraphNode))
	{
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/UnlitMaterial.UnlitMaterial'"));
	}
	else
	{
		// Default to PBR
		MaterialInstanceFactoryNode->SetCustomParent(TEXT("Material'/Interchange/Materials/PBRSurfaceMaterial.PBRSurfaceMaterial'"));
	}

#if WITH_EDITOR
	const UClass* MaterialClass = IsRunningGame() ? UMaterialInstanceDynamic::StaticClass() : UMaterialInstanceConstant::StaticClass();
	MaterialInstanceFactoryNode->SetCustomInstanceClassName(MaterialClass->GetPathName());
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
			if (const UInterchangeShaderNode* ConnectedShaderNode = Cast<const UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
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
		if (const UInterchangeShaderNode* ConnectedShaderNode = Cast<const UInterchangeShaderNode>(BaseNodeContainer->GetNode(ConnectedShaderNodeUid)))
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
			if (const UInterchangeTextureNode* TextureNode = Cast<const UInterchangeTextureNode>(BaseNodeContainer->GetNode(TextureUid)))
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

bool UInterchangeGenericMaterialPipeline::HandleBxDFInput(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials;

	if (!ShaderGraphNode || !UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Common::Parameters::BxDF))
	{
		return false;
	}

	TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
		CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Common::Parameters::BxDF.ToString(), MaterialFactoryNode->GetUniqueID());
	ensure(ExpressionFactoryNode.Get<0>());

	if (ExpressionFactoryNode.Get<0>())
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MaterialFactoryNode, Common::Parameters::BxDF.ToString(), ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
	}

	// Make sure the bUseMaterialAttributes property of the material is set to true
	static const FName UseMaterialAttributesMemberName = GET_MEMBER_NAME_CHECKED(UMaterial, bUseMaterialAttributes);

	MaterialFactoryNode->AddBooleanAttribute(UseMaterialAttributesMemberName, true);
	MaterialFactoryNode->AddApplyAndFillDelegates<FString>(UseMaterialAttributesMemberName, UMaterialExpressionMaterialFunctionCall::StaticClass(), UseMaterialAttributesMemberName);

	return true;
}

UInterchangeMaterialFunctionFactoryNode* UInterchangeGenericMaterialPipeline::CreateMaterialFunctionFactoryNode(const UInterchangeShaderGraphNode* ShaderGraphNode)
{
	UInterchangeMaterialFunctionFactoryNode* FactoryNode = Cast<UInterchangeMaterialFunctionFactoryNode>(CreateBaseMaterialFactoryNode(ShaderGraphNode, UInterchangeMaterialFunctionFactoryNode::StaticClass()));
	
	TArray<FString> InputNames;
	UInterchangeShaderPortsAPI::GatherInputs(ShaderGraphNode, InputNames);

	for (const FString& InputName : InputNames)
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
			CreateMaterialExpressionForInput(FactoryNode, ShaderGraphNode, InputName, FactoryNode->GetUniqueID());

		if (ExpressionFactoryNode.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FactoryNode, InputName, ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
		}
	}

	return FactoryNode;
}

bool UInterchangeGenericMaterialPipeline::IsUnlitModel(const UInterchangeShaderGraphNode* ShaderGraphNode) const
{
	using namespace UE::Interchange::Materials::Unlit;

	const bool bHasUnlitColorInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Parameters::UnlitColor);

	return bHasUnlitColorInput;
}

bool UInterchangeGenericMaterialPipeline::HandleUnlitModel(const UInterchangeShaderGraphNode* ShaderGraphNode, UInterchangeMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::Materials;

	bool bShadingModelHandled = false;

	// Unlit Color
	{
		const bool bHasInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Unlit::Parameters::UnlitColor);

		if (bHasInput)
		{
			TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> ExpressionFactoryNode =
				CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Unlit::Parameters::UnlitColor.ToString(), MaterialFactoryNode->GetUniqueID());

			if (ExpressionFactoryNode.Get<0>())
			{
				MaterialFactoryNode->ConnectOutputToEmissiveColor(ExpressionFactoryNode.Get<0>()->GetUniqueID(), ExpressionFactoryNode.Get<1>());
			}

			//gltf allows unlit color to be also translucent:
			{
				const bool bHasOpacityInput = UInterchangeShaderPortsAPI::HasInput(ShaderGraphNode, Common::Parameters::Opacity);

				if (bHasOpacityInput)
				{
					TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> OpacityExpressionFactoryNode =
						CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderGraphNode, Common::Parameters::Opacity.ToString(), MaterialFactoryNode->GetUniqueID());

					if (OpacityExpressionFactoryNode.Get<0>())
					{
						MaterialFactoryNode->ConnectOutputToOpacity(OpacityExpressionFactoryNode.Get<0>()->GetUniqueID(), OpacityExpressionFactoryNode.Get<1>());
					}

					using namespace UE::Interchange::InterchangeGenericMaterialPipeline;

					Private::UpdateBlendModeBasedOnOpacityAttributes(ShaderGraphNode, MaterialFactoryNode);
				}
			}

			bShadingModelHandled = true;
		}
	}

	if (bShadingModelHandled)
	{
		MaterialFactoryNode->SetCustomShadingModel(EMaterialShadingModel::MSM_Unlit);
	}

	return bShadingModelHandled;
}

UInterchangeMaterialExpressionFactoryNode* UInterchangeGenericMaterialPipeline::CreateFunctionCallExpression(const UInterchangeShaderNode* ShaderNode, const FString& MaterialExpressionUid, UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode)
{
	using namespace UE::Interchange::InterchangeGenericMaterialPipeline::Private;

	const UInterchangeFunctionCallShaderNode* FunctionCallShaderNode = Cast<UInterchangeFunctionCallShaderNode>(ShaderNode);
	if (!FunctionCallShaderNode)
	{
		return nullptr;
	}

	UInterchangeMaterialFunctionCallExpressionFactoryNode* FunctionCallFactoryNode = NewObject<UInterchangeMaterialFunctionCallExpressionFactoryNode>(BaseNodeContainer);
	if (!FunctionCallFactoryNode)
	{
		return nullptr;
	}

	// Check whether the MaterialFunction attribute is valid
	FString MaterialFunctionAttribute;
	if (FunctionCallShaderNode->GetCustomMaterialFunction(MaterialFunctionAttribute))
	{
		if (!BaseNodeContainer->GetNode(MaterialFunctionAttribute))
		{
			if (!FPackageName::IsValidObjectPath(MaterialFunctionAttribute))
			{
				MaterialFunctionAttribute.Empty();
			}
		}
	}

	// Nothing to do if the MaterialFunction attribute is not valid
	if (MaterialFunctionAttribute.IsEmpty())
	{
		// TODO: Log a warning
		return nullptr;
	}

	FunctionCallFactoryNode->InitializeNode(MaterialExpressionUid, ShaderNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);
	BaseNodeContainer->AddNode(FunctionCallFactoryNode);

	if (BaseNodeContainer->GetNode(MaterialFunctionAttribute))
	{
		const FString MaterialFunctionFactoryNodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(MaterialFunctionAttribute);
		FunctionCallFactoryNode->SetCustomMaterialFunctionDependency(MaterialFunctionFactoryNodeUid);
		FunctionCallFactoryNode->AddFactoryDependencyUid(MaterialFunctionFactoryNodeUid);
	}
	else if (FPackageName::IsValidObjectPath(MaterialFunctionAttribute))
	{
		FunctionCallFactoryNode->SetCustomMaterialFunctionDependency(MaterialFunctionAttribute);
		UpdateFunctionCallExpression(*FunctionCallFactoryNode, MaterialFunctionAttribute);
	}

	TArray<FString> Inputs;
	UInterchangeShaderPortsAPI::GatherInputs(ShaderNode, Inputs);

	for (const FString& InputName : Inputs)
	{
		TTuple<UInterchangeMaterialExpressionFactoryNode*, FString> InputExpression =
			CreateMaterialExpressionForInput(MaterialFactoryNode, ShaderNode, InputName, MaterialExpressionUid);

		if (InputExpression.Get<0>())
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(FunctionCallFactoryNode, InputName, InputExpression.Get<0>()->GetUniqueID(), InputExpression.Get<1>());
		}
	}

	return FunctionCallFactoryNode;
}

#undef LOCTEXT_NAMESPACE
