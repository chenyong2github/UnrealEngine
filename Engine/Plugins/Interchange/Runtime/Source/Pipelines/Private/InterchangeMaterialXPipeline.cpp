// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeMaterialXPipeline.h"

#include "InterchangePipelineLog.h"

#include "InterchangeManager.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialFactoryNode.h"

#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"

#if WITH_EDITOR
namespace UE::Interchange::InterchangeMaterialXPipeline::Private
{
	namespace StandardSurface
	{
		const FName Name = TEXT("standard_surface");

		namespace Parameters
		{
			const FName Base = TEXT("base");
			const FName BaseColor = TEXT("base_color");
			const FName DiffuseRoughness = TEXT("diffuse_roughness");
			const FName Metalness = TEXT("metalness");
			const FName Specular = TEXT("specular");
			const FName SpecularColor = TEXT("specular_color");
			const FName SpecularRoughness = TEXT("specular_roughness");
			const FName SpecularIOR = TEXT("specular_IOR");
			const FName SpecularAnisotropy = TEXT("specular_anisotropy");
			const FName SpecularRotation = TEXT("specular_rotation");
			const FName Transmission = TEXT("transmission");
			const FName TransmissionColor = TEXT("transmission_color");
			const FName TransmissionDepth = TEXT("transmission_depth");
			const FName TransmissionScatter = TEXT("transmission_scatter");
			const FName TransmissionScatterAnisotropy = TEXT("transmission_scatter_anisotropy");
			const FName TransmissionDispersion = TEXT("transmission_dispersion");
			const FName TransmissionExtraRoughness = TEXT("transmission_extra_roughness");
			const FName Subsurface = TEXT("subsurface");
			const FName SubsurfaceColor = TEXT("subsurface_color");
			const FName SubsurfaceRadius = TEXT("subsurface_radius");
			const FName SubsurfaceScale = TEXT("subsurface_scale");
			const FName SubsurfaceAnisotropy = TEXT("subsurface_anisotropy");
			const FName Sheen = TEXT("sheen");
			const FName SheenColor = TEXT("sheen_color");
			const FName SheenRoughness = TEXT("sheen_roughness");
			const FName Coat = TEXT("coat");
			const FName CoatColor = TEXT("coat_color");
			const FName CoatRoughness = TEXT("coat_roughness");
			const FName CoatAnisotropy = TEXT("coat_anisotropy");
			const FName CoatRotation = TEXT("coat_rotation");
			const FName CoatIOR = TEXT("coat_IOR");
			const FName CoatNormal = TEXT("coat_normal");
			const FName CoatAffectColor = TEXT("coat_affect_color");
			const FName CoatAffectRoughness = TEXT("coat_affect_roughness");
			const FName ThinFilmThickness = TEXT("thin_film_thickness");
			const FName ThinFilmIOR = TEXT("thin_film_IOR");
			const FName Emission = TEXT("emission");
			const FName EmissionColor = TEXT("emission_color");
			const FName Opacity = TEXT("opacity");
			const FName ThinWalled = TEXT("thin_walled");
			const FName Normal = TEXT("normal");
			const FName Tangent = TEXT("tangent");
		}
	}

	namespace SurfaceUnlit
	{
		const FName Name = TEXT("surface_unlit");

		namespace Parameters
		{
			const FName Emission = TEXT("emission");
			const FName EmissionColor = TEXT("emission_color");
			const FName Transmission = TEXT("transmission");
			const FName TransmissionColor = TEXT("transmission_color");
			const FName Opacity = TEXT("opacity");
		}
	}

	TSet<FName> StandardSurfaceInputs
	{
		StandardSurface::Parameters::Base,
		StandardSurface::Parameters::BaseColor,
		StandardSurface::Parameters::DiffuseRoughness,
		StandardSurface::Parameters::Metalness,
		StandardSurface::Parameters::Specular,
		StandardSurface::Parameters::SpecularRoughness,
		StandardSurface::Parameters::SpecularIOR,
		StandardSurface::Parameters::SpecularAnisotropy,
		StandardSurface::Parameters::SpecularRotation,
		StandardSurface::Parameters::Subsurface,
		StandardSurface::Parameters::SubsurfaceColor,
		StandardSurface::Parameters::SubsurfaceRadius,
		StandardSurface::Parameters::SubsurfaceScale,
		StandardSurface::Parameters::Sheen,
		StandardSurface::Parameters::SheenColor,
		StandardSurface::Parameters::SheenRoughness,
		StandardSurface::Parameters::Coat,
		StandardSurface::Parameters::CoatColor,
		StandardSurface::Parameters::CoatRoughness,
		StandardSurface::Parameters::CoatNormal,
		StandardSurface::Parameters::ThinFilmThickness,
		StandardSurface::Parameters::Emission,
		StandardSurface::Parameters::EmissionColor,
		StandardSurface::Parameters::Normal,
		StandardSurface::Parameters::Tangent
	};

	TSet<FName> StandardSurfaceOutputs
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

	TSet<FName> TransmissionSurfaceInputs
	{
		StandardSurface::Parameters::Base,
		StandardSurface::Parameters::BaseColor,
		StandardSurface::Parameters::DiffuseRoughness,
		StandardSurface::Parameters::Metalness,
		StandardSurface::Parameters::Specular,
		StandardSurface::Parameters::SpecularRoughness,
		StandardSurface::Parameters::SpecularIOR,
		StandardSurface::Parameters::SpecularAnisotropy,
		StandardSurface::Parameters::SpecularRotation,
		StandardSurface::Parameters::Transmission,
		StandardSurface::Parameters::TransmissionColor,
		StandardSurface::Parameters::TransmissionDepth,
		StandardSurface::Parameters::TransmissionScatter,
		StandardSurface::Parameters::TransmissionScatterAnisotropy,
		StandardSurface::Parameters::TransmissionDispersion,
		StandardSurface::Parameters::TransmissionExtraRoughness,
		StandardSurface::Parameters::Subsurface,
		StandardSurface::Parameters::SubsurfaceColor,
		StandardSurface::Parameters::SubsurfaceRadius,
		StandardSurface::Parameters::SubsurfaceScale,
		StandardSurface::Parameters::Sheen,
		StandardSurface::Parameters::SheenColor,
		StandardSurface::Parameters::SheenRoughness,
		StandardSurface::Parameters::Coat,
		StandardSurface::Parameters::CoatColor,
		StandardSurface::Parameters::CoatRoughness,
		StandardSurface::Parameters::CoatNormal,
		StandardSurface::Parameters::ThinFilmThickness,
		StandardSurface::Parameters::Emission,
		StandardSurface::Parameters::EmissionColor,
		StandardSurface::Parameters::Normal,
	};

	TSet<FName> TransmissionSurfaceOutputs
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

	TSet<FName> SurfaceUnlitInputs
	{
		SurfaceUnlit::Parameters::Emission,
		SurfaceUnlit::Parameters::EmissionColor,
		SurfaceUnlit::Parameters::Transmission,
		SurfaceUnlit::Parameters::TransmissionColor,
		SurfaceUnlit::Parameters::Opacity
	};

	TSet<FName> SurfaceUnlitOutputs
	{
		UE::Interchange::Materials::Common::Parameters::EmissiveColor,
		UE::Interchange::Materials::Common::Parameters::Opacity,
	};

	bool ShouldFilterAssets(UMaterialFunction* Asset, const TSet<FName>& Inputs, const TSet<FName>& Outputs)
	{
		int32 InputMatches = 0;
		int32 OutputMatches = 0;

		if (Asset != nullptr)
		{
			TArray<FFunctionExpressionInput> ExpressionInputs;
			TArray<FFunctionExpressionOutput> ExpressionOutputs;
			Asset->GetInputsAndOutputs(ExpressionInputs, ExpressionOutputs);

			for (const FFunctionExpressionInput& ExpressionInput : ExpressionInputs)
			{
				if (Inputs.Find(ExpressionInput.Input.InputName))
				{
					InputMatches++;
				}
			}

			for (const FFunctionExpressionOutput& ExpressionOutput : ExpressionOutputs)
			{
				if (Outputs.Find(ExpressionOutput.Output.OutputName))
				{
					OutputMatches++;
				}
			}
		}

		// we allow at least one input of the same name, but we should have exactly the same outputs
		return !(InputMatches > 0 && OutputMatches == Outputs.Num());
	}

	bool AreRequiredPackagesLoaded(TMap<EInterchangeMaterialXShaders, FSoftObjectPath>& SurfaceShaderMapping)
	{
		auto ArePackagesLoaded = [&](const TMap<EInterchangeMaterialXShaders, FSoftObjectPath>& ObjectPaths)
		{
			bool bAllLoaded = true;

			for (const TPair<EInterchangeMaterialXShaders, FSoftObjectPath>& Pair : ObjectPaths)
			{
				const FSoftObjectPath& ObjectPath = Pair.Get<1>();

				if (!ObjectPath.ResolveObject())
				{
					FString PackagePath = ObjectPath.GetLongPackageName();
					if (FPackageName::DoesPackageExist(PackagePath))
					{
						UObject* Asset = ObjectPath.TryLoad();
						if (!Asset)
						{
							UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't load %s"), *PackagePath);
							bAllLoaded = false;
						}
#if WITH_EDITOR
						else
						{
							if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurface)
							{
								bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), StandardSurfaceInputs, StandardSurfaceOutputs);
							}
							else if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurfaceTransmission)
							{
								bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), TransmissionSurfaceInputs, TransmissionSurfaceOutputs);
							}
							else if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::SurfaceUnlit)
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

		return ArePackagesLoaded(SurfaceShaderMapping);
	}
}
#endif

UInterchangeMaterialXPipeline::UInterchangeMaterialXPipeline()
	: MaterialXSettings(UMaterialXPipelineSettings::StaticClass()->GetDefaultObject< UMaterialXPipelineSettings>())
{
	for (const TPair<EInterchangeMaterialXShaders, FSoftObjectPath>& Entry : MaterialXSettings->PredefinedSurfaceShaders)
	{
		PathToEnumMapping.FindOrAdd(Entry.Value.GetAssetPathString(), Entry.Key);
	}
}

void UInterchangeMaterialXPipeline::AdjustSettingsForContext(EInterchangePipelineContext ImportType, TObjectPtr<UObject> ReimportAsset)
{
	Super::AdjustSettingsForContext(ImportType, ReimportAsset);

	if (!MaterialXSettings->AreRequiredPackagesLoaded())
	{
		UE_LOG(LogInterchangePipeline, Warning, TEXT("UInterchangeGenericMaterialPipeline: Some required packages are missing. Material import might be wrong"));
	}
}

void UInterchangeMaterialXPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* NodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	Super::ExecutePipeline(NodeContainer, InSourceDatas);

#if WITH_EDITOR
	auto UpdateMaterialXNodes = [this](const FString& NodeUid, UInterchangeMaterialFunctionCallExpressionFactoryNode* FactorNode)
	{
		const FString MaterialFunctionMemberName = GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialFunctionCall, MaterialFunction).ToString();

		FString MaterialFunctionPath;
		if (FactorNode->GetStringAttribute(MaterialFunctionMemberName, MaterialFunctionPath))
		{
			if (const EInterchangeMaterialXShaders* EnumPtr = PathToEnumMapping.Find(MaterialFunctionPath))
			{
				FactorNode->AddStringAttribute(MaterialFunctionMemberName, MaterialXSettings->GetAssetPathString(*EnumPtr));
			}
		}
	};

	//Find all translated node we need for this pipeline
	NodeContainer->IterateNodesOfType<UInterchangeMaterialFunctionCallExpressionFactoryNode>(UpdateMaterialXNodes);
#endif
}

bool UMaterialXPipelineSettings::AreRequiredPackagesLoaded()
{
	auto ArePackagesLoaded = [&](const TMap<EInterchangeMaterialXShaders, FSoftObjectPath>& ObjectPaths)
	{
		bool bAllLoaded = true;

		for (const TPair<EInterchangeMaterialXShaders, FSoftObjectPath>& Pair : ObjectPaths)
		{
			const FSoftObjectPath& ObjectPath = Pair.Get<1>();

			if (!ObjectPath.ResolveObject())
			{
				FString PackagePath = ObjectPath.GetLongPackageName();
				if (FPackageName::DoesPackageExist(PackagePath))
				{
					UObject* Asset = ObjectPath.TryLoad();
					if (!Asset)
					{
						UE_LOG(LogInterchangePipeline, Warning, TEXT("Couldn't load %s"), *PackagePath);
						bAllLoaded = false;
					}
#if WITH_EDITOR
					else
					{
						if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurface)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), StandardSurfaceInputs, StandardSurfaceOutputs);
						}
						else if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::StandardSurfaceTransmission)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), TransmissionSurfaceInputs, TransmissionSurfaceOutputs);
						}
						else if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::SurfaceUnlit)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), SurfaceUnlitInputs, SurfaceUnlitOutputs);
						}
						else if (Pair.Get<EInterchangeMaterialXShaders>() == EInterchangeMaterialXShaders::UsdPreviewSurface)
						{
							bAllLoaded = !ShouldFilterAssets(Cast<UMaterialFunction>(Asset), UsdPreviewSurfaceInputs, UsdPreviewSurfaceOutputs);
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

	return ArePackagesLoaded(PredefinedSurfaceShaders);
}

FString UMaterialXPipelineSettings::GetAssetPathString(EInterchangeMaterialXShaders ShaderType) const
{
	if (const FSoftObjectPath* ObjectPath = PredefinedSurfaceShaders.Find(ShaderType))
	{
		return ObjectPath->GetAssetPathString();
	}

	return {};
}

#if WITH_EDITOR
TSet<FName> UMaterialXPipelineSettings::StandardSurfaceInputs
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

TSet<FName> UMaterialXPipelineSettings::StandardSurfaceOutputs
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

TSet<FName> UMaterialXPipelineSettings::TransmissionSurfaceInputs
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

TSet<FName> UMaterialXPipelineSettings::TransmissionSurfaceOutputs
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

TSet<FName> UMaterialXPipelineSettings::SurfaceUnlitInputs
{
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Emission,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::EmissionColor,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Transmission,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::TransmissionColor,
	UE::Interchange::Materials::SurfaceUnlit::Parameters::Opacity
};

TSet<FName> UMaterialXPipelineSettings::SurfaceUnlitOutputs
{
	UE::Interchange::Materials::Common::Parameters::EmissiveColor,
	UE::Interchange::Materials::Common::Parameters::Opacity,
	TEXT("OpacityMask")
};

TSet<FName> UMaterialXPipelineSettings::UsdPreviewSurfaceInputs
{
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::DiffuseColor,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::EmissiveColor,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::SpecularColor,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Metallic,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Roughness,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Clearcoat,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::ClearcoatRoughness,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Opacity,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::OpacityThreshold,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::IOR,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Normal,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Displacement,
	UE::Interchange::Materials::UsdPreviewSurface::Parameters::Occlusion
};

TSet<FName> UMaterialXPipelineSettings::UsdPreviewSurfaceOutputs
{
	UE::Interchange::Materials::PBR::Parameters::BaseColor,
	UE::Interchange::Materials::PBR::Parameters::Metallic,
	UE::Interchange::Materials::PBR::Parameters::Specular,
	UE::Interchange::Materials::PBR::Parameters::Roughness,
	UE::Interchange::Materials::PBR::Parameters::EmissiveColor,
	UE::Interchange::Materials::PBR::Parameters::Opacity,
	UE::Interchange::Materials::PBR::Parameters::Normal,
	UE::Interchange::Materials::Common::Parameters::Refraction,
	UE::Interchange::Materials::Common::Parameters::Occlusion,
	UE::Interchange::Materials::ClearCoat::Parameters::ClearCoat,
	UE::Interchange::Materials::ClearCoat::Parameters::ClearCoatRoughness,
};

bool UMaterialXPipelineSettings::ShouldFilterAssets(UMaterialFunction* Asset, const TSet<FName>& Inputs, const TSet<FName>& Outputs)
{
	int32 InputMatches = 0;
	int32 OutputMatches = 0;

	if (Asset != nullptr)
	{
		TArray<FFunctionExpressionInput> ExpressionInputs;
		TArray<FFunctionExpressionOutput> ExpressionOutputs;
		Asset->GetInputsAndOutputs(ExpressionInputs, ExpressionOutputs);

		for (const FFunctionExpressionInput& ExpressionInput : ExpressionInputs)
		{
			if (Inputs.Find(ExpressionInput.Input.InputName))
			{
				InputMatches++;
			}
		}

		for (const FFunctionExpressionOutput& ExpressionOutput : ExpressionOutputs)
		{
			if (Outputs.Find(ExpressionOutput.Output.OutputName))
			{
				OutputMatches++;
			}
		}
	}

	// we allow at least one input of the same name, but we should have exactly the same outputs
	return !(InputMatches > 0 && OutputMatches == Outputs.Num());
}
#endif // WITH_EDITOR

