// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeMapsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshOcclusionMapEvaluator.h"
#include "Sampling/MeshCurvatureMapEvaluator.h"
#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshResampleImageEvaluator.h"
#include "Util/IndexUtil.h"

#include "SimpleDynamicMeshComponent.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/MeshDescriptionUtil.h"
#include "ModelingObjectsCreationAPI.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargetManager.h"

// required to pass UStaticMesh asset so we can save at same location
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/Engine/StaticMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeMeshAttributeMapsTool"

/*
 * Static init
 */

// Only include the Occlusion bitmask rather than its components
// (AmbientOcclusion | BentNormal). Since the Occlusion baker can
// bake both types in a single pass, only iterating over the Occlusion
// bitmask gives direct access to both types without the need to
// externally track if we've handled the Occlusion evaluator in a prior
// iteration loop.
static constexpr EBakeMapType ALL_BAKE_MAP_TYPES[] =
{
	EBakeMapType::TangentSpaceNormalMap,
	EBakeMapType::Occlusion, // (AmbientOcclusion | BentNormal)
	EBakeMapType::Curvature,
	EBakeMapType::Texture2DImage,
	EBakeMapType::NormalImage,
	EBakeMapType::FaceNormalImage,
	EBakeMapType::PositionImage,
	EBakeMapType::MaterialID,
	EBakeMapType::MultiTexture
};


/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UBakeMeshAttributeMapsToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass(),			// currently only supports StaticMesh targets
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UBakeMeshAttributeMapsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	int32 NumTargets = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements());
	return (NumTargets == 1 || NumTargets == 2);
}

UInteractiveTool* UBakeMeshAttributeMapsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeMeshAttributeMapsTool* NewTool = NewObject<UBakeMeshAttributeMapsTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));

	return NewTool;
}




TArray<FString> UBakeMeshAttributeMapsToolProperties::GetUVLayerNamesFunc()
{
	return UVLayerNamesList;
}


/*
 * Operators
 */

class FMeshMapBakerOp : public TGenericDataOperator<FMeshMapBaker>
{
public:
	// General bake settings
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	UE::Geometry::FDynamicMesh3* BaseMesh;
	TUniquePtr<UE::Geometry::FMeshMapBaker> Baker;
	UBakeMeshAttributeMapsTool::FBakeCacheSettings BakeCacheSettings;
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;

	// Map Type settings
	EBakeMapType Maps;
	UBakeMeshAttributeMapsTool::FNormalMapSettings NormalSettings;
	UBakeMeshAttributeMapsTool::FOcclusionMapSettings OcclusionSettings;
	UBakeMeshAttributeMapsTool::FCurvatureMapSettings CurvatureSettings;
	UBakeMeshAttributeMapsTool::FMeshPropertyMapSettings PropertySettings;
	UBakeMeshAttributeMapsTool::FTexture2DImageSettings TextureSettings;

	// Texture2DImage & MultiTexture settings
	using ImagePtr = TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>;
	const FDynamicMeshUVOverlay* UVOverlay = nullptr;
	ImagePtr TextureImage;
	TMap<int32, ImagePtr> MaterialToTextureImageMap;

	// Begin TGenericDataOperator interface
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Baker = MakeUnique<FMeshMapBaker>();
		Baker->CancelF = [Progress]() {
			return Progress && Progress->Cancelled();
		};
		Baker->SetTargetMesh(BaseMesh);
		Baker->SetDetailMesh(DetailMesh.Get(), DetailSpatial.Get());
		Baker->SetDimensions(BakeCacheSettings.Dimensions);
		Baker->SetUVLayer(BakeCacheSettings.UVLayer);
		Baker->SetThickness(BakeCacheSettings.Thickness);
		Baker->SetMultisampling(BakeCacheSettings.Multisampling);
		Baker->SetTargetMeshTangents(BaseMeshTangents);

		for (const EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
		{
			switch (BakeCacheSettings.BakeMapTypes & MapType)
			{
			case EBakeMapType::TangentSpaceNormalMap:
			{
				TSharedPtr<FMeshNormalMapEvaluator, ESPMode::ThreadSafe> NormalEval = MakeShared<FMeshNormalMapEvaluator, ESPMode::ThreadSafe>();
				Baker->AddBaker(NormalEval);
				break;
			}
			case EBakeMapType::AmbientOcclusion:
			case EBakeMapType::BentNormal:
			case EBakeMapType::Occlusion:
			{
				TSharedPtr<FMeshOcclusionMapEvaluator> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator>();
				OcclusionEval->OcclusionType = EMeshOcclusionMapType::None;
				if ((bool)(BakeCacheSettings.BakeMapTypes & EBakeMapType::AmbientOcclusion))
				{
					OcclusionEval->OcclusionType |= EMeshOcclusionMapType::AmbientOcclusion;
				}
				if ((bool)(BakeCacheSettings.BakeMapTypes & EBakeMapType::BentNormal))
				{
					OcclusionEval->OcclusionType |= EMeshOcclusionMapType::BentNormal;
				}
				OcclusionEval->NumOcclusionRays = OcclusionSettings.OcclusionRays;
				OcclusionEval->MaxDistance = OcclusionSettings.MaxDistance;
				OcclusionEval->SpreadAngle = OcclusionSettings.SpreadAngle;
				OcclusionEval->BiasAngleDeg = OcclusionSettings.BiasAngle;

				switch (OcclusionSettings.Distribution)
				{
				case EOcclusionMapDistribution::Cosine:
					OcclusionEval->Distribution = FMeshOcclusionMapEvaluator::EDistribution::Cosine;
					break;
				case EOcclusionMapDistribution::Uniform:
					OcclusionEval->Distribution = FMeshOcclusionMapEvaluator::EDistribution::Uniform;
					break;
				}

				switch (OcclusionSettings.NormalSpace)
				{
				case ENormalMapSpace::Tangent:
					OcclusionEval->NormalSpace = FMeshOcclusionMapEvaluator::ESpace::Tangent;
					break;
				case ENormalMapSpace::Object:
					OcclusionEval->NormalSpace = FMeshOcclusionMapEvaluator::ESpace::Object;
					break;
				}
				Baker->AddBaker(OcclusionEval);
				break;
			}
			case EBakeMapType::Curvature:
			{
				TSharedPtr<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe> CurvatureBaker = MakeShared<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe>();
				CurvatureBaker->RangeScale = FMathd::Clamp(CurvatureSettings.RangeMultiplier, 0.0001, 1000.0);
				CurvatureBaker->MinRangeScale = FMathd::Clamp(CurvatureSettings.MinRangeMultiplier, 0.0, 1.0);
				CurvatureBaker->UseCurvatureType = (FMeshCurvatureMapEvaluator::ECurvatureType)CurvatureSettings.CurvatureType;
				CurvatureBaker->UseColorMode = (FMeshCurvatureMapEvaluator::EColorMode)CurvatureSettings.ColorMode;
				CurvatureBaker->UseClampMode = (FMeshCurvatureMapEvaluator::EClampMode)CurvatureSettings.ClampMode;
				Baker->AddBaker(CurvatureBaker);
				break;
			}
			case EBakeMapType::NormalImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::Normal;
				Baker->AddBaker(PropertyBaker);
				break;
			}
			case EBakeMapType::FaceNormalImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::FacetNormal;
				Baker->AddBaker(PropertyBaker);
				break;
			}
			case EBakeMapType::PositionImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::Position;
				Baker->AddBaker(PropertyBaker);
				break;
			}
			case EBakeMapType::MaterialID:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::MaterialID;
				Baker->AddBaker(PropertyBaker);
				break;
			}
			case EBakeMapType::Texture2DImage:
			{
				TSharedPtr<FMeshResampleImageEvaluator, ESPMode::ThreadSafe> ResampleBaker = MakeShared<FMeshResampleImageEvaluator, ESPMode::ThreadSafe>();
				ResampleBaker->DetailUVOverlay = UVOverlay;
				ResampleBaker->SampleFunction = [this](FVector2d UVCoord) {
					return TextureImage->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
				};
				Baker->AddBaker(ResampleBaker);
				break;
			}
			case EBakeMapType::MultiTexture:
			{
				TSharedPtr<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe> TextureBaker = MakeShared<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe>();
				TextureBaker->DetailUVOverlay = UVOverlay;
				TextureBaker->MultiTextures = MaterialToTextureImageMap;
				Baker->AddBaker(TextureBaker);
				break;
			}
			default:
				break;
			}
		}
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
	// End TGenericDataOperator interface
};

/*
 * Tool
 */

void UBakeMeshAttributeMapsTool::Setup()
{
	UInteractiveTool::Setup();

	IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(0);
	IMeshDescriptionProvider* MeshProvider = TargetMeshProviderInterface(0);
	IMaterialProvider* MaterialProvider = TargetMaterialInterface(0);

	// copy input MeshDescription and make sure it has initialized normals/tangents
	BaseMeshDescription = MakeShared<FMeshDescription, ESPMode::ThreadSafe>(*MeshProvider->GetMeshDescription());
	UE::MeshDescription::InitializeAutoGeneratedAttributes(*BaseMeshDescription, TargetComponent->GetOwnerComponent(), 0);

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(TargetComponent->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(TargetComponent->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(TargetComponent->GetWorldTransform());

	// transfer materials
	FComponentMaterialSet MaterialSet;
	MaterialProvider->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::ExternallyCalculated;
	DynamicMeshComponent->InitializeMesh(BaseMeshDescription.Get());
	
	BaseMesh.Copy(*DynamicMeshComponent->GetMesh());
	BaseSpatial.SetMesh(&BaseMesh, true);
	BaseMeshTangents = MakeShared<FMeshTangentsd, ESPMode::ThreadSafe>(&BaseMesh);
	BaseMeshTangents->CopyTriVertexTangents(*DynamicMeshComponent->GetTangents());

	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/BakePreviewMaterial"));
	check(Material);
	if (Material != nullptr)
	{
		PreviewMaterial = UMaterialInstanceDynamic::Create(Material, GetToolManager());
		DynamicMeshComponent->SetOverrideRenderMaterial(PreviewMaterial);
	}
	UMaterial* BentNormalMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/BakeBentNormalPreviewMaterial"));
	check(BentNormalMaterial);
	if (BentNormalMaterial != nullptr)
	{
		BentNormalPreviewMaterial = UMaterialInstanceDynamic::Create(BentNormalMaterial, GetToolManager());
	}
	UMaterial* WorkingMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/InProgressMaterial"));
	check(WorkingMaterial);
	if (WorkingMaterial != nullptr)
	{
		WorkingPreviewMaterial = UMaterialInstanceDynamic::Create(WorkingMaterial, GetToolManager());
	}

	bIsBakeToSelf = (Targets.Num() == 1);

	// hide input StaticMeshComponent
	TargetComponent->SetOwnerVisibility(false);


	Settings = NewObject<UBakeMeshAttributeMapsToolProperties>(this);
	Settings->RestoreProperties(this);
	Settings->UVLayerNamesList.Reset();
	int32 FoundIndex = -1;
	for (int32 k = 0; k < BaseMesh.Attributes()->NumUVLayers(); ++k)
	{
		Settings->UVLayerNamesList.Add(FString::FromInt(k));
		if (Settings->UVLayer == Settings->UVLayerNamesList.Last())
		{
			FoundIndex = k;
		}
	}
	if (FoundIndex == -1)
	{
		Settings->UVLayer = Settings->UVLayerNamesList[0];
	}
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->MapTypes, [this](int32) { bInputsDirty = true; UpdateOnModeChange(); });
	Settings->WatchProperty(Settings->MapPreview, [this](int32) { UpdateVisualization(); GetToolManager()->PostInvalidation(); });
	Settings->WatchProperty(Settings->Resolution, [this](EBakeTextureResolution) { bInputsDirty = true; });
	Settings->WatchProperty(Settings->UVLayer, [this](FString) { bInputsDirty = true; });
	Settings->WatchProperty(Settings->bUseWorldSpace, [this](bool) { bDetailMeshValid = false; bInputsDirty = true; });
	Settings->WatchProperty(Settings->Thickness, [this](float) { bInputsDirty = true; });
	Settings->WatchProperty(Settings->Multisampling, [this](EBakeMultisampling) { bInputsDirty = true; });

	NormalMapProps = NewObject<UBakedNormalMapToolProperties>(this);
	NormalMapProps->RestoreProperties(this);
	AddToolPropertySource(NormalMapProps);
	SetToolPropertySourceEnabled(NormalMapProps, false);


	OcclusionMapProps = NewObject<UBakedOcclusionMapToolProperties>(this);
	OcclusionMapProps->RestoreProperties(this);
	AddToolPropertySource(OcclusionMapProps);
	SetToolPropertySourceEnabled(OcclusionMapProps, false);
	OcclusionMapProps->WatchProperty(OcclusionMapProps->OcclusionRays, [this](int32) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->MaxDistance, [this](float) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->SpreadAngle, [this](float) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->Distribution, [this](EOcclusionMapDistribution) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->BlurRadius, [this](float) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->bGaussianBlur, [this](float) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->BiasAngle, [this](float) { bInputsDirty = true; });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->NormalSpace, [this](ENormalMapSpace) { bInputsDirty = true; });


	CurvatureMapProps = NewObject<UBakedCurvatureMapToolProperties>(this);
	CurvatureMapProps->RestoreProperties(this);
	AddToolPropertySource(CurvatureMapProps);
	SetToolPropertySourceEnabled(CurvatureMapProps, false);
	CurvatureMapProps->WatchProperty(CurvatureMapProps->RangeMultiplier, [this](float) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->MinRangeMultiplier, [this](float) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->CurvatureType, [this](EBakedCurvatureTypeMode) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->ColorMode, [this](EBakedCurvatureColorMode) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->Clamping, [this](EBakedCurvatureClampMode) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->BlurRadius, [this](float) { bInputsDirty = true; });
	CurvatureMapProps->WatchProperty(CurvatureMapProps->bGaussianBlur, [this](float) { bInputsDirty = true; });


	Texture2DProps = NewObject<UBakedTexture2DImageProperties>(this);
	Texture2DProps->RestoreProperties(this);
	AddToolPropertySource(Texture2DProps);
	SetToolPropertySourceEnabled(Texture2DProps, false);
	Texture2DProps->WatchProperty(Texture2DProps->UVLayer, [this](float) { bInputsDirty = true; });
	Texture2DProps->WatchProperty(Texture2DProps->SourceTexture, [this](UTexture2D*) { bInputsDirty = true; });

	MultiTextureProps = NewObject<UBakedMultiTexture2DImageProperties>(this);
	MultiTextureProps->RestoreProperties(this);
	AddToolPropertySource(MultiTextureProps);
	SetToolPropertySourceEnabled(MultiTextureProps, false);

	auto SetDirtyCallback = [this](TMap<int32, UTexture2D*>) { bInputsDirty = true; };
	auto NotEqualsCallback = [](const TMap<int32, UTexture2D*>& A, const TMap<int32, UTexture2D*>& B) -> bool { return !(A.OrderIndependentCompareEqual(B)); };
	MultiTextureProps->WatchProperty(MultiTextureProps->MaterialIDSourceTextureMap, SetDirtyCallback, NotEqualsCallback);
	MultiTextureProps->WatchProperty(MultiTextureProps->UVLayer, [this](float) { bInputsDirty = true; });

	VisualizationProps = NewObject<UBakedOcclusionMapVisualizationProperties>(this);
	VisualizationProps->RestoreProperties(this);
	AddToolPropertySource(VisualizationProps);

	InitializeEmptyMaps();
	UpdateOnModeChange();

	bInputsDirty = true;
	bDetailMeshValid = false;

	SetToolDisplayName(LOCTEXT("ToolName", "Bake Textures"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Bake Maps. Select Bake Mesh (LowPoly) first, then (optionally) Detail Mesh second. Texture Assets will be created on Accept. "),
		EToolMessageLevel::UserNotification);
}




bool UBakeMeshAttributeMapsTool::CanAccept() const
{
	bool bCanAccept = Compute ? Compute->HaveValidResult() : false;
	if (bCanAccept)
	{
		// Allow Accept if all non-None types have valid results.
		int NumResults = Settings->Result.Num();
		for (int ResultIdx = 0; ResultIdx < NumResults; ++ResultIdx)
		{
			bCanAccept = bCanAccept && Settings->Result[ResultIdx];
		}
	}
	return bCanAccept;
}


TUniquePtr<UE::Geometry::TGenericDataOperator<FMeshMapBaker>> UBakeMeshAttributeMapsTool::MakeNewOperator()
{
	TUniquePtr<FMeshMapBakerOp> Op = MakeUnique<FMeshMapBakerOp>();
	Op->DetailMesh = DetailMesh;
	Op->DetailSpatial = DetailSpatial;
	Op->BaseMesh = &BaseMesh;
	Op->BakeCacheSettings = CachedBakeCacheSettings;

	const EBakeMapType RequiresTangents = EBakeMapType::TangentSpaceNormalMap | EBakeMapType::BentNormal;
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & RequiresTangents))
	{
		Op->BaseMeshTangents = BaseMeshTangents;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::TangentSpaceNormalMap))
	{
		Op->NormalSettings = CachedNormalMapSettings;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::AmbientOcclusion) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::BentNormal))
	{
		Op->OcclusionSettings = CachedOcclusionMapSettings;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Curvature))
	{
		Op->CurvatureSettings = CachedCurvatureMapSettings;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::NormalImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::FaceNormalImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::PositionImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::MaterialID))
	{
		Op->PropertySettings = CachedMeshPropertyMapSettings;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Texture2DImage))
	{
		Op->TextureSettings = CachedTexture2DImageSettings;
		Op->TextureImage = CachedTextureImage;
		Op->UVOverlay = DetailMesh->Attributes()->GetUVLayer(CachedTexture2DImageSettings.UVLayer);
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::MultiTexture))
	{
		Op->TextureSettings = CachedTexture2DImageSettings;
		Op->MaterialToTextureImageMap = CachedMultiTextures;
		Op->UVOverlay = DetailMesh->Attributes()->GetUVLayer(CachedTexture2DImageSettings.UVLayer);
	}

	return Op;
}


void UBakeMeshAttributeMapsTool::Shutdown(EToolShutdownType ShutdownType)
{
	IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(0);

	Settings->SaveProperties(this);
	OcclusionMapProps->SaveProperties(this);
	NormalMapProps->SaveProperties(this);
	CurvatureMapProps->SaveProperties(this);
	Texture2DProps->SaveProperties(this);
	MultiTextureProps->SaveProperties(this);
	VisualizationProps->SaveProperties(this);

	if (Compute)
	{
		Compute->Shutdown();
	}
	if (DynamicMeshComponent != nullptr)
	{
		TargetComponent->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(TargetComponent->GetOwnerComponent());
			UStaticMesh* StaticMeshAsset = StaticMeshComponent->GetStaticMesh();
			check(StaticMeshAsset);
			FString BaseName = TargetComponent->GetOwnerActor()->GetName();

			bool bCreatedAssetOK = true;
			int NumResults = Settings->Result.Num();
			for (int ResultIdx = 0; ResultIdx < NumResults; ++ResultIdx)
			{
				FTexture2DBuilder::ETextureType TexType = FTexture2DBuilder::ETextureType::Color;
				FString TexName;
				switch (ResultTypes[ResultIdx])
				{
				default:
					// Should never reach this case.
					check(false);
					continue;
				case EBakeMapType::TangentSpaceNormalMap:
					TexName = FString::Printf(TEXT("%s_Normals"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::NormalMap;
					break;
				case EBakeMapType::AmbientOcclusion:
					TexName = FString::Printf(TEXT("%s_Occlusion"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::AmbientOcclusion;
					break;
				case EBakeMapType::BentNormal:
					TexName = FString::Printf(TEXT("%s_BentNormal"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::NormalMap;
					break;
				case EBakeMapType::Curvature:
					TexName = FString::Printf(TEXT("%s_Curvature"), *BaseName);
					break;
				case EBakeMapType::NormalImage:
					TexName = FString::Printf(TEXT("%s_NormalImg"), *BaseName);
					break;
				case EBakeMapType::FaceNormalImage:
					TexName = FString::Printf(TEXT("%s_FaceNormalImg"), *BaseName);
					break;
				case EBakeMapType::MaterialID:
					TexName = FString::Printf(TEXT("%s_MaterialIDImg"), *BaseName);
					break;
				case EBakeMapType::PositionImage:
					TexName = FString::Printf(TEXT("%s_PositionImg"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::Color;
					break;
				case EBakeMapType::Texture2DImage:
					TexName = FString::Printf(TEXT("%s_TextureImg"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::Color;
					break;
				case EBakeMapType::MultiTexture:
					TexName = FString::Printf(TEXT("%s_MultiTextureImg"), *BaseName);
					TexType = FTexture2DBuilder::ETextureType::Color;
					break;
				}
				FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[ResultIdx], TexType);
				bCreatedAssetOK = bCreatedAssetOK && UE::Modeling::CreateTextureObject(GetToolManager(), FCreateTextureObjectParams{ 0, StaticMeshAsset->GetWorld(), StaticMeshAsset, TexName, Settings->Result[ResultIdx] }).IsOK();
			}
			ensure(bCreatedAssetOK);
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}


void UBakeMeshAttributeMapsTool::OnTick(float DeltaTime)
{
	if (Compute)
	{
		Compute->Tick(DeltaTime);

		float ElapsedComputeTime = Compute->GetElapsedComputeTime();
		if (!CanAccept() && ElapsedComputeTime > SecondsBeforeWorkingMaterial)
		{
			DynamicMeshComponent->SetOverrideRenderMaterial(WorkingPreviewMaterial);
		}
	}
}


void UBakeMeshAttributeMapsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();

	IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(0);

	float GrayLevel = VisualizationProps->BaseGrayLevel;
	PreviewMaterial->SetVectorParameterValue(TEXT("BaseColor"), FVector(GrayLevel, GrayLevel, GrayLevel) );
	float AOWeight = VisualizationProps->OcclusionMultiplier;
	PreviewMaterial->SetScalarParameterValue(TEXT("AOWeight"), AOWeight );

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = TargetComponent->GetWorldTransform();
}




int SelectTextureToBake(const TArray<UTexture*>& Textures)
{
	TArray<int> TextureVotes;
	TextureVotes.Init(0, Textures.Num());

	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		UTexture* Tex = Textures[TextureIndex];
		UTexture2D* Tex2D = Cast<UTexture2D>(Tex);

		if (Tex2D)
		{
			// Texture uses SRGB
			if (Tex->SRGB != 0)
			{
				++TextureVotes[TextureIndex];
			}

#if WITH_EDITORONLY_DATA
			// Texture has multiple channels
			ETextureSourceFormat Format = Tex->Source.GetFormat();
			if (Format == TSF_BGRA8 || Format == TSF_BGRE8 || Format == TSF_RGBA16 || Format == TSF_RGBA16F)
			{
				++TextureVotes[TextureIndex];
			}
#endif

			// What else? Largest texture? Most layers? Most mipmaps?
		}
	}

	int MaxIndex = -1;
	int MaxVotes = -1;
	for (int TextureIndex = 0; TextureIndex < Textures.Num(); ++TextureIndex)
	{
		if (TextureVotes[TextureIndex] > MaxVotes)
		{
			MaxIndex = TextureIndex;
			MaxVotes = TextureVotes[TextureIndex];
		}
	}

	return MaxIndex;
}


void UBakeMeshAttributeMapsTool::GetTexturesFromDetailMesh(const IPrimitiveComponentBackedTarget* DetailComponent)
{
	constexpr bool bGuessAtTextures = true;

	MultiTextureProps->AllSourceTextures.Reset();
	MultiTextureProps->MaterialIDSourceTextureMap.Reset();

	TArray<UMaterialInterface*> Materials;
	DetailComponent->GetOwnerComponent()->GetUsedMaterials(Materials);
	
	for (int32 MaterialID = 0; MaterialID < Materials.Num(); ++MaterialID)	// TODO: This won't match MaterialIDs on the FDynamicMesh3 in general, will it?
	{
		UMaterialInterface* MaterialInterface = Materials[MaterialID];
		if (MaterialInterface == nullptr)
		{
			continue;
		}

		TArray<UTexture*> Textures;
		MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
		
		for (UTexture* Tex : Textures)
		{
			UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
			if (Tex2D)
			{
				MultiTextureProps->AllSourceTextures.Add(Tex2D);
			}
		}

		if (bGuessAtTextures)
		{
			int SelectedTextureIndex = SelectTextureToBake(Textures);
			if (SelectedTextureIndex >= 0)
			{
				UTexture2D* Tex2D = Cast<UTexture2D>(Textures[SelectedTextureIndex]);

				// if cast fails, this will set the value to nullptr, which is fine
				MultiTextureProps->MaterialIDSourceTextureMap.Add(MaterialID, Tex2D);	
			}
		}
		else
		{
			MultiTextureProps->MaterialIDSourceTextureMap.Add(MaterialID, nullptr);
		}
	}

}


void UBakeMeshAttributeMapsTool::UpdateDetailMesh()
{
	IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(0);
	IPrimitiveComponentBackedTarget* DetailComponent = TargetComponentInterface(bIsBakeToSelf ? 0 : 1);
	IMeshDescriptionProvider* DetailMeshProvider = TargetMeshProviderInterface(bIsBakeToSelf ? 0 : 1);

	DetailMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(DetailMeshProvider->GetMeshDescription(), *DetailMesh);
	if (Settings->bUseWorldSpace && bIsBakeToSelf == false)
	{
		UE::Geometry::FTransform3d DetailToWorld(DetailComponent->GetWorldTransform());
		MeshTransforms::ApplyTransform(*DetailMesh, DetailToWorld);
		UE::Geometry::FTransform3d WorldToBase(TargetComponent->GetWorldTransform());
		MeshTransforms::ApplyTransform(*DetailMesh, WorldToBase.Inverse());
	}
	
	DetailSpatial = MakeShared<FDynamicMeshAABBTree3, ESPMode::ThreadSafe>();
	DetailSpatial->SetMesh(DetailMesh.Get(), true);

	GetTexturesFromDetailMesh(DetailComponent);

	bInputsDirty = true;
	DetailMeshTimestamp++;

}










void UBakeMeshAttributeMapsTool::UpdateResult()
{

	if (bDetailMeshValid == false)
	{
		UpdateDetailMesh();
		bDetailMeshValid = true;
		CachedBakeCacheSettings = FBakeCacheSettings();
	}

	// bInputsDirty ensures that we only validate parameters once per param
	// change. Parameter validation can be expensive (ex. UpdateResult_Texture2DImage).
	if (!bInputsDirty)
	{
		return;
	}

	// clear warning (ugh)
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FBakeCacheSettings BakeCacheSettings;
	BakeCacheSettings.Dimensions = Dimensions;
	BakeCacheSettings.UVLayer = FCString::Atoi(*Settings->UVLayer);
	BakeCacheSettings.DetailTimestamp = this->DetailMeshTimestamp;
	BakeCacheSettings.Thickness = Settings->Thickness;
	BakeCacheSettings.Multisampling = (int32)Settings->Multisampling;

	// process the raw bitfield before caching which may add additional targets.
	BakeCacheSettings.BakeMapTypes = GetMapTypes(Settings->MapTypes);

	// update bake cache settings
	if (!(CachedBakeCacheSettings == BakeCacheSettings))
	{
		CachedBakeCacheSettings = BakeCacheSettings;

		CachedNormalMapSettings = FNormalMapSettings();
		CachedOcclusionMapSettings = FOcclusionMapSettings();
		CachedCurvatureMapSettings = FCurvatureMapSettings();
		CachedMeshPropertyMapSettings = FMeshPropertyMapSettings();
		CachedTexture2DImageSettings = FTexture2DImageSettings();
	}

	// Clear our invalid bitflag to check again for valid inputs.
	OpState = EBakeOpState::Evaluate;

	// Update map type settings
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::TangentSpaceNormalMap))
	{
		OpState |= UpdateResult_Normal();
	}
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::AmbientOcclusion) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::BentNormal))
	{
		OpState |= UpdateResult_Occlusion();
	}
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Curvature))
	{
		OpState |= UpdateResult_Curvature();
	}
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::NormalImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::FaceNormalImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::PositionImage) ||
		(bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::MaterialID))
	{
		OpState |= UpdateResult_MeshProperty();
	}
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Texture2DImage))
	{
		OpState |= UpdateResult_Texture2DImage();
	}
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::MultiTexture))
	{
		OpState |= UpdateResult_MultiTexture();
	}

	// Early exit if op input parameters are invalid.
	if ((bool)(OpState & EBakeOpState::Invalid))
	{
		return;
	}

	// This should be the only point of compute invalidation to
	// minimize synchronization issues.
	bool bInvalidate = bInputsDirty || (bool)(OpState & EBakeOpState::Evaluate);
	if (!Compute)
	{
		Compute = MakeUnique<TGenericDataBackgroundCompute<FMeshMapBaker>>();
		Compute->Setup(this);
		Compute->OnResultUpdated.AddLambda([this](const TUniquePtr<FMeshMapBaker>& NewResult) { OnMapsUpdated(NewResult); });
		Compute->InvalidateResult();
	}
	else if (bInvalidate)
	{
		Compute->InvalidateResult();
	}
	bInputsDirty = false;
}



EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_Normal()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FNormalMapSettings NormalMapSettings;
	NormalMapSettings.Dimensions = Dimensions;

	if (!(CachedNormalMapSettings == NormalMapSettings))
	{
		CachedNormalMapSettings = NormalMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}


EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_Occlusion()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FOcclusionMapSettings OcclusionMapSettings;
	OcclusionMapSettings.Dimensions = Dimensions;
	OcclusionMapSettings.MaxDistance = (OcclusionMapProps->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionMapProps->MaxDistance;
	OcclusionMapSettings.OcclusionRays = OcclusionMapProps->OcclusionRays;
	OcclusionMapSettings.SpreadAngle = OcclusionMapProps->SpreadAngle;
	OcclusionMapSettings.Distribution = OcclusionMapProps->Distribution;
	OcclusionMapSettings.BlurRadius = (OcclusionMapProps->bGaussianBlur) ? OcclusionMapProps->BlurRadius : 0.0;
	OcclusionMapSettings.BiasAngle = OcclusionMapProps->BiasAngle;
	OcclusionMapSettings.NormalSpace = OcclusionMapProps->NormalSpace;

	if ( !(CachedOcclusionMapSettings == OcclusionMapSettings) )
	{
		CachedOcclusionMapSettings = OcclusionMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}



EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_Curvature()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FCurvatureMapSettings CurvatureMapSettings;
	CurvatureMapSettings.Dimensions = Dimensions;
	CurvatureMapSettings.RangeMultiplier = CurvatureMapProps->RangeMultiplier;
	CurvatureMapSettings.MinRangeMultiplier = CurvatureMapProps->MinRangeMultiplier;
	switch (CurvatureMapProps->CurvatureType)
	{
	default:
	case EBakedCurvatureTypeMode::MeanAverage:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapEvaluator::ECurvatureType::Mean;
		break;
	case EBakedCurvatureTypeMode::Gaussian:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapEvaluator::ECurvatureType::Gaussian;
		break;
	case EBakedCurvatureTypeMode::Max:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapEvaluator::ECurvatureType::MaxPrincipal;
		break;
	case EBakedCurvatureTypeMode::Min:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapEvaluator::ECurvatureType::MinPrincipal;
		break;
	}
	switch (CurvatureMapProps->ColorMode)
	{
	default:
	case EBakedCurvatureColorMode::Grayscale:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapEvaluator::EColorMode::BlackGrayWhite;
		break;
	case EBakedCurvatureColorMode::RedBlue:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapEvaluator::EColorMode::RedBlue;
		break;
	case EBakedCurvatureColorMode::RedGreenBlue:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapEvaluator::EColorMode::RedGreenBlue;
		break;
	}
	switch (CurvatureMapProps->Clamping)
	{
	default:
	case EBakedCurvatureClampMode::None:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapEvaluator::EClampMode::FullRange;
		break;
	case EBakedCurvatureClampMode::Positive:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapEvaluator::EClampMode::Positive;
		break;
	case EBakedCurvatureClampMode::Negative:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapEvaluator::EClampMode::Negative;
		break;
	}
	CurvatureMapSettings.BlurRadius = (CurvatureMapProps->bGaussianBlur) ? CurvatureMapProps->BlurRadius : 0.0;

	if (!(CachedCurvatureMapSettings == CurvatureMapSettings))
	{
		CachedCurvatureMapSettings = CurvatureMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}



EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_MeshProperty()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FMeshPropertyMapSettings MeshPropertyMapSettings;
	MeshPropertyMapSettings.Dimensions = Dimensions;

	if (!(CachedMeshPropertyMapSettings == MeshPropertyMapSettings))
	{
		CachedMeshPropertyMapSettings = MeshPropertyMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}








class FTempTextureAccess
{
public:
	FTempTextureAccess(UTexture2D* DisplacementMap)
		: DisplacementMap(DisplacementMap)
	{
		check(DisplacementMap);
		OldCompressionSettings = DisplacementMap->CompressionSettings;
		bOldSRGB = DisplacementMap->SRGB;
#if WITH_EDITOR
		OldMipGenSettings = DisplacementMap->MipGenSettings;
#endif
		DisplacementMap->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
		DisplacementMap->SRGB = false;
#if WITH_EDITOR
		DisplacementMap->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
#endif
		DisplacementMap->UpdateResource();

		FormattedImageData = reinterpret_cast<const FColor*>(DisplacementMap->PlatformData->Mips[0].BulkData.LockReadOnly());
	}
	FTempTextureAccess(const FTempTextureAccess&) = delete;
	FTempTextureAccess(FTempTextureAccess&&) = delete;
	void operator=(const FTempTextureAccess&) = delete;
	void operator=(FTempTextureAccess&&) = delete;

	~FTempTextureAccess()
	{
		DisplacementMap->PlatformData->Mips[0].BulkData.Unlock();

		DisplacementMap->CompressionSettings = OldCompressionSettings;
		DisplacementMap->SRGB = bOldSRGB;
#if WITH_EDITOR
		DisplacementMap->MipGenSettings = OldMipGenSettings;
#endif

		DisplacementMap->UpdateResource();
	}

	bool HasData() const
	{
		return FormattedImageData != nullptr;
	}
	const FColor* GetData() const
	{
		return FormattedImageData;
	}

	FImageDimensions GetDimensions() const
	{
		int32 Width = DisplacementMap->PlatformData->Mips[0].SizeX;
		int32 Height = DisplacementMap->PlatformData->Mips[0].SizeY;
		return FImageDimensions(Width, Height);
	}


	bool CopyTo(TImageBuilder<FVector4f>& DestImage) const
	{
		if (!HasData()) return false;

		FImageDimensions TextureDimensions = GetDimensions();
		if (ensure(DestImage.GetDimensions() == TextureDimensions) == false)
		{
			return false;
		}

		int64 Num = TextureDimensions.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			FColor ByteColor = FormattedImageData[i];
			FLinearColor FloatColor(ByteColor);
			DestImage.SetPixel(i, FVector4f(FloatColor));
		}
		return true;
	}

private:
	UTexture2D* DisplacementMap{ nullptr };
	TextureCompressionSettings OldCompressionSettings{};
	TextureMipGenSettings OldMipGenSettings{};
	bool bOldSRGB{ false };
	const FColor* FormattedImageData{ nullptr };
};







EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_Texture2DImage()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FTexture2DImageSettings NewSettings;
	NewSettings.Dimensions = Dimensions;
	NewSettings.UVLayer = 0;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}
	
	if (Texture2DProps->SourceTexture == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}


	{
		FTempTextureAccess TextureAccess(Texture2DProps->SourceTexture);
		CachedTextureImage = MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
		CachedTextureImage->SetDimensions(TextureAccess.GetDimensions());
		if (!TextureAccess.CopyTo(*CachedTextureImage))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("CannotReadTextureWarning", "Cannot read from the source texture"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}
	}

	if (!(CachedTexture2DImageSettings == NewSettings))
	{
		CachedTexture2DImageSettings = NewSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}


EBakeOpState UBakeMeshAttributeMapsTool::UpdateResult_MultiTexture()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FTexture2DImageSettings NewSettings;
	NewSettings.Dimensions = Dimensions;
	NewSettings.UVLayer = MultiTextureProps->UVLayer;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}

	for (TPair<int32, UTexture2D*>& InputTexture : MultiTextureProps->MaterialIDSourceTextureMap)
	{
		if (InputTexture.Value == nullptr)
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}
	}


	CachedMultiTextures.Reset();

	for ( TPair<int32, UTexture2D*>& InputTexture : MultiTextureProps->MaterialIDSourceTextureMap)
	{
		UTexture2D* Texture = InputTexture.Value;
		if (!ensure(Texture != nullptr))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}

		int32 MaterialID = InputTexture.Key;
		FTempTextureAccess TextureAccess(Texture);
		CachedMultiTextures.Add(MaterialID, MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>());
		CachedMultiTextures[MaterialID]->SetDimensions(TextureAccess.GetDimensions());

		if (!TextureAccess.CopyTo(*CachedMultiTextures[MaterialID]))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("CannotReadTextureWarning", "Cannot read from the source texture"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}
	}
	if (CachedMultiTextures.Num() == 0)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}

	if (!(CachedTexture2DImageSettings == NewSettings))
	{
		CachedTexture2DImageSettings = NewSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}





void UBakeMeshAttributeMapsTool::UpdateVisualization()
{
	DynamicMeshComponent->SetOverrideRenderMaterial(PreviewMaterial);

	// Map CachedMaps to Settings->Result
	int NumResults = Settings->Result.Num();
	for (int ResultIdx = 0; ResultIdx < NumResults; ResultIdx++)
	{
		Settings->Result[ResultIdx] = CachedMaps[CachedMapIndices[ResultTypes[ResultIdx]]];
	}

	// Set the preview material according to the preview index.
	if (Settings->MapPreview >= 0 && Settings->MapPreview < Settings->Result.Num())
	{
		const EBakeMapType& PreviewMapType = ResultTypes[Settings->MapPreview];
		if (PreviewMapType != EBakeMapType::None)
		{
			UTexture2D* PreviewMap = CachedMaps[CachedMapIndices[PreviewMapType]];
			switch (PreviewMapType)
			{
			default:
				PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
				PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
				PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
				break;
			case EBakeMapType::TangentSpaceNormalMap:
				PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), PreviewMap);
				PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
				PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
				break;
			case EBakeMapType::AmbientOcclusion:
				PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
				PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), PreviewMap);
				PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
				break;
			case EBakeMapType::BentNormal:
				BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
				if (CachedMapIndices.Contains(EBakeMapType::AmbientOcclusion))
				{
					BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), CachedMaps[CachedMapIndices[EBakeMapType::AmbientOcclusion]]);
				}
				else
				{
					BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
				}
				BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
				BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("BentNormalMap"), PreviewMap);
				DynamicMeshComponent->SetOverrideRenderMaterial(BentNormalPreviewMaterial);
				break;
			case EBakeMapType::Curvature:
			case EBakeMapType::NormalImage:
			case EBakeMapType::FaceNormalImage:
			case EBakeMapType::PositionImage:
			case EBakeMapType::MaterialID:
			case EBakeMapType::Texture2DImage:
			case EBakeMapType::MultiTexture:
				PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
				PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
				PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), PreviewMap);
				break;
			}
		}
	}
}



void UBakeMeshAttributeMapsTool::UpdateOnModeChange()
{
	SetToolPropertySourceEnabled(NormalMapProps, false);
	SetToolPropertySourceEnabled(OcclusionMapProps, false);
	SetToolPropertySourceEnabled(CurvatureMapProps, false);
	SetToolPropertySourceEnabled(Texture2DProps, false);
	SetToolPropertySourceEnabled(MultiTextureProps, false);

	for (const EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
	{
		switch ((EBakeMapType)Settings->MapTypes & MapType)
		{
		case EBakeMapType::TangentSpaceNormalMap:
			SetToolPropertySourceEnabled(NormalMapProps, true);
			break;
		case EBakeMapType::AmbientOcclusion:
		case EBakeMapType::BentNormal:
		case EBakeMapType::Occlusion:
			SetToolPropertySourceEnabled(OcclusionMapProps, true);
			break;
		case EBakeMapType::Curvature:
			SetToolPropertySourceEnabled(CurvatureMapProps, true);
			break;
		case EBakeMapType::NormalImage:
		case EBakeMapType::FaceNormalImage:
		case EBakeMapType::PositionImage:
		case EBakeMapType::MaterialID:
			break;
		case EBakeMapType::Texture2DImage:
			SetToolPropertySourceEnabled(Texture2DProps, true);
			break;
		case EBakeMapType::MultiTexture:
			SetToolPropertySourceEnabled(MultiTextureProps, true);
			break;
		default:
			break;
		}
	}

	ResultTypes = GetMapTypesArray(Settings->MapTypes);
	Settings->Result.Empty();
	Settings->Result.SetNum(ResultTypes.Num());

	// Generate a map between EBakeMapType and CachedMaps
	CachedMapIndices.Empty();
	int32 CachedMapIdx = 0;

	// Use the processed bitfield which may contain additional targets
	// (ex. AO if BentNormal was requested).
	const EBakeMapType BakeMapTypes = GetMapTypes(Settings->MapTypes);
	for (EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
	{
		if (MapType == EBakeMapType::Occlusion)
		{
			if ((bool)(BakeMapTypes & EBakeMapType::AmbientOcclusion))
			{
				CachedMapIndices.Add(EBakeMapType::AmbientOcclusion, CachedMapIdx++);
			}
			if ((bool)(BakeMapTypes & EBakeMapType::BentNormal))
			{
				CachedMapIndices.Add(EBakeMapType::BentNormal, CachedMapIdx++);
			}
		}
		else if( (bool)(BakeMapTypes & MapType) )
		{
			CachedMapIndices.Add(MapType, CachedMapIdx++);
		}
	}
	CachedMaps.Empty();
	CachedMaps.SetNum(CachedMapIndices.Num());
}


void UBakeMeshAttributeMapsTool::OnMapsUpdated(const TUniquePtr<FMeshMapBaker>& NewResult)
{
	// This method assumes that the bake evaluators were instantiated in the order
	// defined by ALL_BAKE_MAP_TYPES.
	const EBakeMapType& BakeMapTypes = CachedBakeCacheSettings.BakeMapTypes;
	int32 BakerIdx = 0;
	for (EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
	{
		switch (BakeMapTypes & MapType)
		{
		case EBakeMapType::TangentSpaceNormalMap:
		{
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, CachedNormalMapSettings.Dimensions);
			TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx++)[0]);
			TextureBuilder.Commit(false);
			CachedMaps[CachedMapIndices[EBakeMapType::TangentSpaceNormalMap]] = TextureBuilder.GetTexture2D();
			break;
		}
		case EBakeMapType::AmbientOcclusion:
		case EBakeMapType::BentNormal:
		case EBakeMapType::Occlusion:
		{
			int32 OcclusionIdx = 0;
			if ((bool)(BakeMapTypes & EBakeMapType::AmbientOcclusion))
			{
				FTexture2DBuilder TextureBuilder;
				TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::AmbientOcclusion, CachedOcclusionMapSettings.Dimensions);
				TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx)[OcclusionIdx++]);
				TextureBuilder.Commit(false);
				CachedMaps[CachedMapIndices[EBakeMapType::AmbientOcclusion]] = TextureBuilder.GetTexture2D();
			}
			if ((bool)(BakeMapTypes & EBakeMapType::BentNormal))
			{
				FTexture2DBuilder TextureBuilder;
				TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, CachedOcclusionMapSettings.Dimensions);
				TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx)[OcclusionIdx++]);
				TextureBuilder.Commit(false);
				CachedMaps[CachedMapIndices[EBakeMapType::BentNormal]] = TextureBuilder.GetTexture2D();
			}
			++BakerIdx;
			break;
		}
		case EBakeMapType::Curvature:
		{
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedCurvatureMapSettings.Dimensions);
			TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx++)[0]);
			TextureBuilder.Commit(false);
			CachedMaps[CachedMapIndices[BakeMapTypes & MapType]] = TextureBuilder.GetTexture2D();
			break;
		}
		case EBakeMapType::NormalImage:
		case EBakeMapType::FaceNormalImage:
		case EBakeMapType::PositionImage:
		case EBakeMapType::MaterialID:
		{
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedMeshPropertyMapSettings.Dimensions);
			TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx++)[0]);
			TextureBuilder.Commit(false);
			CachedMaps[CachedMapIndices[BakeMapTypes & MapType]] = TextureBuilder.GetTexture2D();
			break;
		}
		case EBakeMapType::Texture2DImage:
		case EBakeMapType::MultiTexture:
		{
			FTexture2DBuilder TextureBuilder;
			TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedTexture2DImageSettings.Dimensions);
			TextureBuilder.Copy(*NewResult->GetBakeResults(BakerIdx++)[0]);
			TextureBuilder.Commit(false);
			CachedMaps[CachedMapIndices[BakeMapTypes & MapType]] = TextureBuilder.GetTexture2D();
			break;
		}
		}
	}

	UpdateVisualization();
	GetToolManager()->PostInvalidation();
}


EBakeMapType UBakeMeshAttributeMapsTool::GetMapTypes(const int32& MapTypes) const
{
	EBakeMapType OutMapTypes = (EBakeMapType)MapTypes & EBakeMapType::All;
	// Force AO bake for BentNormal preview
	if ((bool)(OutMapTypes & EBakeMapType::BentNormal))
	{
		OutMapTypes |= EBakeMapType::AmbientOcclusion;
	}
	return OutMapTypes;
}

TArray<EBakeMapType> UBakeMeshAttributeMapsTool::GetMapTypesArray(const int32& MapTypes) const
{
	TArray<EBakeMapType> OutMapTypes;
	int32 Bitfield = MapTypes & (int32)EBakeMapType::All;
	for (int32 BitIdx = 0; Bitfield; Bitfield >>= 1, ++BitIdx)
	{
		if (Bitfield & 1)
		{
			OutMapTypes.Add((EBakeMapType)(1 << BitIdx));
		}
	}
	return OutMapTypes;
}


void UBakeMeshAttributeMapsTool::InitializeEmptyMaps()
{
	FTexture2DBuilder NormalsBuilder;
	NormalsBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, FImageDimensions(16, 16));
	NormalsBuilder.Commit(false);
	EmptyNormalMap = NormalsBuilder.GetTexture2D();

	FTexture2DBuilder ColorBuilderBlack;
	ColorBuilderBlack.Initialize(FTexture2DBuilder::ETextureType::Color, FImageDimensions(16, 16));
	ColorBuilderBlack.Clear(FColor(0,0,0));
	ColorBuilderBlack.Commit(false);
	EmptyColorMapBlack = ColorBuilderBlack.GetTexture2D();

	FTexture2DBuilder ColorBuilderWhite;
	ColorBuilderWhite.Initialize(FTexture2DBuilder::ETextureType::Color, FImageDimensions(16, 16));
	ColorBuilderWhite.Clear(FColor::White);
	ColorBuilderWhite.Commit(false);
	EmptyColorMapWhite = ColorBuilderWhite.GetTexture2D();
}



#undef LOCTEXT_NAMESPACE
