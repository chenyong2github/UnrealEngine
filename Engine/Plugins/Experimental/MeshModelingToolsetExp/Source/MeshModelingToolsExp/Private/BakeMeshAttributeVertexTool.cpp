// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeVertexTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "DynamicMesh/MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshOcclusionMapEvaluator.h"
#include "Sampling/MeshCurvatureMapEvaluator.h"
#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshResampleImageEvaluator.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"
#include "ModelingToolTargetUtil.h"
#include "AssetUtils/Texture2DUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeMeshAttributeVertexTool"

/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UBakeMeshAttributeVertexToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionProvider::StaticClass(),
		UMeshDescriptionCommitter::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UMaterialProvider::StaticClass()
	});
	return TypeRequirements;
}

bool UBakeMeshAttributeVertexToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	const int32 NumTargets = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements());
	return (NumTargets == 1 || NumTargets == 2);
}

UInteractiveTool* UBakeMeshAttributeVertexToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeMeshAttributeVertexTool* NewTool = NewObject<UBakeMeshAttributeVertexTool>(SceneState.ToolManager);
	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(
		SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}


/*
 * Operators
 */

class FMeshVertexBakerOp : public TGenericDataOperator<FMeshVertexBaker>
{
public:
	// General bake settings
	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	UE::Geometry::FDynamicMesh3* BaseMesh;
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	TUniquePtr<UE::Geometry::FMeshVertexBaker> Baker;

	UBakeMeshAttributeVertexTool::FBakeSettings BakeSettings;
	UBakeMeshAttributeVertexTool::FBakeColorSettings ColorSettings;
	UBakeMeshAttributeVertexTool::FBakeChannelSettings ChannelSettings;
	FOcclusionMapSettings OcclusionSettings;
	FCurvatureMapSettings CurvatureSettings;
	FTexture2DImageSettings TextureSettings;

	// Texture2DImage & MultiTexture settings
	using ImagePtr = TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>;
	const FDynamicMeshUVOverlay* UVOverlay = nullptr;
	ImagePtr TextureImage;
	TMap<int32, ImagePtr> MaterialToTextureImageMap;

	// Begin TGenericDataOperator interface
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Baker = MakeUnique<FMeshVertexBaker>();
		Baker->CancelF = [Progress]()
		{
			return Progress && Progress->Cancelled();
		};
		Baker->SetTargetMesh(BaseMesh);
		Baker->SetDetailMesh(DetailMesh.Get(), DetailSpatial.Get());
		Baker->SetTargetMeshTangents(BaseMeshTangents);
		Baker->SetThickness(BakeSettings.Thickness);
		Baker->BakeMode = BakeSettings.VertexMode == EBakeVertexMode::Color ? FMeshVertexBaker::EBakeMode::Color : FMeshVertexBaker::EBakeMode::Channel;

		auto InitOcclusionEvaluator = [this] (FMeshOcclusionMapEvaluator* OcclusionEval, const EMeshOcclusionMapType OcclusionType)
		{
			OcclusionEval->OcclusionType = OcclusionType;
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
		};

		if (BakeSettings.VertexMode == EBakeVertexMode::PerChannel)
		{
			for(int ChannelIdx = 0; ChannelIdx < 4; ++ChannelIdx)
			{
				switch(ChannelSettings.BakeType[ChannelIdx])
				{
				case EBakeVertexTypeChannel::AmbientOcclusion:
				{
					TSharedPtr<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe>();
					InitOcclusionEvaluator(OcclusionEval.Get(), EMeshOcclusionMapType::AmbientOcclusion);
					Baker->ChannelEvaluators[ChannelIdx] = OcclusionEval;
					break;				
				}
				case EBakeVertexTypeChannel::Curvature:
				{
					TSharedPtr<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe> CurvatureEval = MakeShared<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe>();
					Baker->ChannelEvaluators[ChannelIdx] = CurvatureEval;
					break;
				}
				default:
				case EBakeVertexTypeChannel::None:
				{
					Baker->ChannelEvaluators[ChannelIdx] = nullptr;
					break;
				}
				}
			}
		}
		else // EBakeVertexMode::Color
		{
			switch (ColorSettings.BakeType)
			{
			case EBakeVertexTypeColor::TangentSpaceNormal:
			{
				Baker->ColorEvaluator = MakeShared<FMeshNormalMapEvaluator, ESPMode::ThreadSafe>();
				break;
			}
			case EBakeVertexTypeColor::AmbientOcclusion:
			{
				TSharedPtr<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe>();
				InitOcclusionEvaluator(OcclusionEval.Get(), EMeshOcclusionMapType::AmbientOcclusion);
				Baker->ColorEvaluator = OcclusionEval;
				break;
			}
			case EBakeVertexTypeColor::BentNormal:
			{
				TSharedPtr<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe> OcclusionEval = MakeShared<FMeshOcclusionMapEvaluator, ESPMode::ThreadSafe>();
				InitOcclusionEvaluator(OcclusionEval.Get(), EMeshOcclusionMapType::BentNormal);
				Baker->ColorEvaluator = OcclusionEval;
				break;
			}
			case EBakeVertexTypeColor::Curvature:
			{
				TSharedPtr<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe> CurvatureBaker = MakeShared<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe>();
				CurvatureBaker->RangeScale = FMathd::Clamp(CurvatureSettings.RangeMultiplier, 0.0001, 1000.0);
				CurvatureBaker->MinRangeScale = FMathd::Clamp(CurvatureSettings.MinRangeMultiplier, 0.0, 1.0);
				CurvatureBaker->UseCurvatureType = (FMeshCurvatureMapEvaluator::ECurvatureType)CurvatureSettings.CurvatureType;
				CurvatureBaker->UseColorMode = (FMeshCurvatureMapEvaluator::EColorMode)CurvatureSettings.ColorMode;
				CurvatureBaker->UseClampMode = (FMeshCurvatureMapEvaluator::EClampMode)CurvatureSettings.ClampMode;
				Baker->ColorEvaluator = CurvatureBaker;
				break;
			}
			case EBakeVertexTypeColor::PositionImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::Position;
				Baker->ColorEvaluator = PropertyBaker;
				break;
			}
			case EBakeVertexTypeColor::NormalImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::Normal;
				Baker->ColorEvaluator = PropertyBaker;
				break;
			}
			case EBakeVertexTypeColor::FaceNormalImage:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::FacetNormal;
				Baker->ColorEvaluator = PropertyBaker;
				break;
			}
			case EBakeVertexTypeColor::MaterialID:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyBaker = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyBaker->Property = EMeshPropertyMapType::MaterialID;
				Baker->ColorEvaluator = PropertyBaker;
				break;
			}
			case EBakeVertexTypeColor::Texture2DImage:
			{
				TSharedPtr<FMeshResampleImageEvaluator, ESPMode::ThreadSafe> TextureBaker = MakeShared<FMeshResampleImageEvaluator, ESPMode::ThreadSafe>();
				TextureBaker->DetailUVOverlay = UVOverlay;
				TextureBaker->SampleFunction = [this](FVector2d UVCoord) {
					return TextureImage->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
				};
				Baker->ColorEvaluator = TextureBaker;
				break;
			}
			case EBakeVertexTypeColor::MultiTexture:
			{
				TSharedPtr<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe> TextureBaker = MakeShared<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe>();
				TextureBaker->DetailUVOverlay = UVOverlay;
				TextureBaker->MultiTextures = MaterialToTextureImageMap;
				Baker->ColorEvaluator = TextureBaker;
				break;
			}
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

void UBakeMeshAttributeVertexTool::Setup()
{
	UInteractiveTool::Setup();

	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/MeshVertexColorMaterial"));
	check(Material);
	if (Material != nullptr)
	{
		PreviewMaterial = UMaterialInstanceDynamic::Create(Material, GetToolManager());
	}

	UMaterial* AlphaMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/MeshVertexAlphaMaterial"));
	check(AlphaMaterial);
	if (AlphaMaterial != nullptr)
	{
		PreviewAlphaMaterial = UMaterialInstanceDynamic::Create(AlphaMaterial, GetToolManager());
	}

	UMaterial* WorkingMaterial = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/InProgressMaterial"));
	check(WorkingMaterial);
	if (WorkingMaterial != nullptr)
	{
		WorkingPreviewMaterial = UMaterialInstanceDynamic::Create(WorkingMaterial, GetToolManager());
	}

	bIsBakeToSelf = (Targets.Num() == 1);

	UE::ToolTarget::HideSourceObject(Targets[0]);

	const FDynamicMesh3 InputMeshWithTangents = UE::ToolTarget::GetDynamicMeshCopy(Targets[0], true);
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetTransform(static_cast<FTransform>(UE::ToolTarget::GetLocalToWorldTransform(Targets[0])));
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::ExternallyProvided);
	PreviewMesh->ReplaceMesh(InputMeshWithTangents);
	PreviewMesh->SetMaterials(UE::ToolTarget::GetMaterialSet(Targets[0]).Materials);
	PreviewMesh->SetOverrideRenderMaterial(PreviewMaterial);
	PreviewMesh->SetVisible(true);

	PreviewMesh->ProcessMesh([this](const FDynamicMesh3& Mesh)
	{
		BaseMesh.Copy(Mesh);
		BaseSpatial.SetMesh(&BaseMesh, true);
		BaseMeshTangents = MakeShared<FMeshTangentsd, ESPMode::ThreadSafe>(&BaseMesh);
		BaseMeshTangents->CopyTriVertexTangents(Mesh);
	});

	// Setup tool property sets
	Settings = NewObject<UBakeMeshAttributeVertexToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->VertexMode, [this](EBakeVertexMode) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });
	Settings->WatchProperty(Settings->VertexChannelPreview, [this](EBakeVertexChannel) { UpdateVisualization(); });
	Settings->WatchProperty(Settings->Thickness, [this](float) { OpState = EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->bUseWorldSpace, [this](bool)	{ bDetailMeshValid = false; OpState = EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->bSplitAtNormalSeams, [this](bool) { bColorTopologyValid = false; OpState = EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->bSplitAtUVSeams, [this](bool) { bColorTopologyValid = false; OpState = EBakeOpState::Evaluate; });

	ColorSettings = NewObject<UBakeMeshAttributeVertexToolColorProperties>(this);
	ColorSettings->RestoreProperties(this);
	AddToolPropertySource(ColorSettings);
	SetToolPropertySourceEnabled(ColorSettings, false);
	ColorSettings->WatchProperty(ColorSettings->BakeType, [this](EBakeVertexTypeColor) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });

	PerChannelSettings = NewObject<UBakeMeshAttributeVertexToolChannelProperties>(this);
	PerChannelSettings->RestoreProperties(this);
	AddToolPropertySource(PerChannelSettings);
	SetToolPropertySourceEnabled(PerChannelSettings, false);
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeR, [this](EBakeVertexTypeChannel) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeG, [this](EBakeVertexTypeChannel) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeB, [this](EBakeVertexTypeChannel) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeA, [this](EBakeVertexTypeChannel) { OpState = EBakeOpState::Evaluate; UpdateOnModeChange(); });

	OcclusionSettings = NewObject<UBakedOcclusionMapToolProperties>(this);
	OcclusionSettings->RestoreProperties(this);
	AddToolPropertySource(OcclusionSettings);
	SetToolPropertySourceEnabled(OcclusionSettings, false);
	OcclusionSettings->WatchProperty(OcclusionSettings->OcclusionRays, [this](int32) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->MaxDistance, [this](float) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->SpreadAngle, [this](float) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->Distribution, [this](EOcclusionMapDistribution) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->BlurRadius, [this](float) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->bGaussianBlur, [this](float) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->BiasAngle, [this](float) { OpState = EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->NormalSpace, [this](ENormalMapSpace) { OpState = EBakeOpState::Evaluate; });

	CurvatureSettings = NewObject<UBakedCurvatureMapToolProperties>(this);
	CurvatureSettings->RestoreProperties(this);
	AddToolPropertySource(CurvatureSettings);
	SetToolPropertySourceEnabled(CurvatureSettings, false);
	CurvatureSettings->WatchProperty(CurvatureSettings->RangeMultiplier, [this](float) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->MinRangeMultiplier, [this](float) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->CurvatureType, [this](EBakedCurvatureTypeMode) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->ColorMode, [this](EBakedCurvatureColorMode) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->Clamping, [this](EBakedCurvatureClampMode) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->BlurRadius, [this](float) { OpState = EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->bGaussianBlur, [this](float) { OpState = EBakeOpState::Evaluate; });

	TextureSettings = NewObject<UBakedTexture2DImageProperties>(this);
	TextureSettings->RestoreProperties(this);
	AddToolPropertySource(TextureSettings);
	SetToolPropertySourceEnabled(TextureSettings, false);
	TextureSettings->WatchProperty(TextureSettings->UVLayer, [this](float) { OpState = EBakeOpState::Evaluate; });
	TextureSettings->WatchProperty(TextureSettings->SourceTexture, [this](UTexture2D*) { OpState = EBakeOpState::Evaluate; });

	MultiTextureSettings = NewObject<UBakedMultiTexture2DImageProperties>(this);
	MultiTextureSettings->RestoreProperties(this);
	AddToolPropertySource(MultiTextureSettings);
	SetToolPropertySourceEnabled(MultiTextureSettings, false);

	UpdateOnModeChange();

	bDetailMeshValid = false;

	SetToolDisplayName(LOCTEXT("ToolName", "Bake Vertex Colors"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool",
		        "Bake Vertex Colors. Select Bake Mesh (LowPoly) first, then (optionally) Detail Mesh second."),
		EToolMessageLevel::UserNotification);
}

void UBakeMeshAttributeVertexTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	ColorSettings->SaveProperties(this);
	PerChannelSettings->SaveProperties(this);
	OcclusionSettings->SaveProperties(this);
	CurvatureSettings->SaveProperties(this);
	TextureSettings->SaveProperties(this);
	MultiTextureSettings->SaveProperties(this);

	UE::ToolTarget::ShowSourceObject(Targets[0]);

	if (Compute)
	{
		Compute->Shutdown();
	}

	if (PreviewMesh)
	{
		if (ShutdownType == EToolShutdownType::Accept)
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("BakeMeshAttributeVertexToolTransactionName",
			                                               "Bake Mesh Attribute Vertex"));
			FConversionToMeshDescriptionOptions ConvertOptions;
			ConvertOptions.SetToVertexColorsOnly();
			ConvertOptions.bTransformVtxColorsSRGBToLinear = true;
			UE::ToolTarget::CommitDynamicMeshUpdate(
				Targets[0],
				*PreviewMesh->GetMesh(),
				false, // bHaveModifiedTopology
				ConvertOptions);
			GetToolManager()->EndUndoTransaction();
		}

		PreviewMesh->SetVisible(false);
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}
}

void UBakeMeshAttributeVertexTool::OnTick(float DeltaTime)
{
	if (Compute)
	{
		Compute->Tick(DeltaTime);

		const float ElapsedComputeTime = Compute->GetElapsedComputeTime();
		if (!CanAccept() && ElapsedComputeTime > SecondsBeforeWorkingMaterial)
		{
			PreviewMesh->SetOverrideRenderMaterial(WorkingPreviewMaterial);
		}
	}
}

void UBakeMeshAttributeVertexTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();
}

bool UBakeMeshAttributeVertexTool::CanAccept() const
{
	const bool bValidOp = (OpState & EBakeOpState::Invalid) != EBakeOpState::Invalid;
	return Compute ? bValidOp && Compute->HaveValidResult() : false;
}

TUniquePtr<UE::Geometry::TGenericDataOperator<FMeshVertexBaker>> UBakeMeshAttributeVertexTool::MakeNewOperator()
{
	TUniquePtr<FMeshVertexBakerOp> Op = MakeUnique<FMeshVertexBakerOp>();
	Op->DetailMesh = DetailMesh;
	Op->DetailSpatial = DetailSpatial;
	Op->BaseMesh = &BaseMesh;
	Op->BaseMeshTangents = BaseMeshTangents;
	Op->BakeSettings = CachedBakeSettings;
	Op->ColorSettings = CachedColorSettings;
	Op->ChannelSettings = CachedChannelSettings;
	Op->OcclusionSettings = CachedOcclusionMapSettings;
	Op->CurvatureSettings = CachedCurvatureMapSettings;
	Op->TextureSettings = CachedTexture2DImageSettings;

	// Texture2DImage & MultiTexture settings
	Op->TextureImage = CachedTextureImage;
	Op->MaterialToTextureImageMap = CachedMultiTextures;
	Op->UVOverlay = DetailMesh->Attributes()->GetUVLayer(CachedTexture2DImageSettings.UVLayer);
	return Op;
}

void UBakeMeshAttributeVertexTool::SetWorld(UWorld* World)
{
	TargetWorld = World;
}

void UBakeMeshAttributeVertexTool::UpdateDetailMesh()
{
	IPrimitiveComponentBackedTarget* TargetComponent = TargetComponentInterface(0);
	IPrimitiveComponentBackedTarget* DetailComponent = TargetComponentInterface(bIsBakeToSelf ? 0 : 1);
	IMeshDescriptionProvider* DetailMeshProvider = TargetMeshProviderInterface(bIsBakeToSelf ? 0 : 1);

	DetailMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(DetailMeshProvider->GetMeshDescription(), *DetailMesh);
	if (Settings->bUseWorldSpace && bIsBakeToSelf == false)
	{
		const UE::Geometry::FTransform3d DetailToWorld(DetailComponent->GetWorldTransform());
		MeshTransforms::ApplyTransform(*DetailMesh, DetailToWorld);
		const UE::Geometry::FTransform3d WorldToBase(TargetComponent->GetWorldTransform());
		MeshTransforms::ApplyTransform(*DetailMesh, WorldToBase.Inverse());
	}

	DetailSpatial = MakeShared<FDynamicMeshAABBTree3, ESPMode::ThreadSafe>();
	DetailSpatial->SetMesh(DetailMesh.Get(), true);

	OpState = EBakeOpState::Evaluate;
	DetailMeshTimestamp++;
}

void UBakeMeshAttributeVertexTool::UpdateOnModeChange()
{
	const bool bIsColorMode = Settings->VertexMode == EBakeVertexMode::Color;
	SetToolPropertySourceEnabled(ColorSettings, bIsColorMode);
	SetToolPropertySourceEnabled(PerChannelSettings, !bIsColorMode);
	SetToolPropertySourceEnabled(OcclusionSettings, false);
	SetToolPropertySourceEnabled(CurvatureSettings, false);
	SetToolPropertySourceEnabled(TextureSettings, false);
	SetToolPropertySourceEnabled(MultiTextureSettings, false);

	if (Settings->VertexMode == EBakeVertexMode::Color)
	{
		switch (ColorSettings->BakeType)
		{
			case EBakeVertexTypeColor::AmbientOcclusion:
			case EBakeVertexTypeColor::BentNormal:
				SetToolPropertySourceEnabled(OcclusionSettings, true);
				break;
			case EBakeVertexTypeColor::Curvature:
				SetToolPropertySourceEnabled(CurvatureSettings, true);
				break;
			case EBakeVertexTypeColor::Texture2DImage:
				SetToolPropertySourceEnabled(TextureSettings, true);
				break;
			case EBakeVertexTypeColor::MultiTexture:
				SetToolPropertySourceEnabled(MultiTextureSettings, true);
				break;
			default:
				// No property sets to show.
				break;
		}
	}
	else // Settings->VertexMode == EBakeVertexMode::PerChannel
	{
		EBakeVertexTypeChannel PerChannelTypes[4] = {
			PerChannelSettings->BakeTypeR,
			PerChannelSettings->BakeTypeG,
			PerChannelSettings->BakeTypeB,
			PerChannelSettings->BakeTypeA
		};
		for(int Idx = 0; Idx < 4; ++Idx)
		{
			switch(PerChannelTypes[Idx])
			{
				case EBakeVertexTypeChannel::AmbientOcclusion:
					SetToolPropertySourceEnabled(OcclusionSettings, true);
					break;
				case EBakeVertexTypeChannel::Curvature:
					SetToolPropertySourceEnabled(CurvatureSettings, true);
					break;
				case EBakeVertexTypeChannel::None:
				default:
					break;
			}
		}
	}
}

void UBakeMeshAttributeVertexTool::UpdateVisualization()
{
	if (Settings->VertexChannelPreview == EBakeVertexChannel::A)
	{
		PreviewMesh->SetOverrideRenderMaterial(PreviewAlphaMaterial);
	}
	else
	{
		FLinearColor Mask(FLinearColor::Black);
		switch(Settings->VertexChannelPreview)
		{
		case EBakeVertexChannel::R:
			Mask.R = 1.0f;
			break;
		case EBakeVertexChannel::G:
			Mask.G = 1.0f;
			break;
		case EBakeVertexChannel::B:
			Mask.B = 1.0f;
			break;
		case EBakeVertexChannel::RGBA:
		default:
			Mask = FLinearColor::White;
			break;
		}
		PreviewMaterial->SetVectorParameterValue("VertexColorMask", Mask);
		PreviewMesh->SetOverrideRenderMaterial(PreviewMaterial);
	}
}

void UBakeMeshAttributeVertexTool::UpdateColorTopology()
{
	// Update PreviewMesh color topology
	PreviewMesh->EditMesh([this](FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->DisablePrimaryColors();
		Mesh.Attributes()->EnablePrimaryColors();

		FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		Mesh.Attributes()->PrimaryColors()->CreateFromPredicate(
			[&](int ParentVID, int TriIDA, int TriIDB) -> bool
			{
				auto OverlayCanShare = [&] (auto Overlay) -> bool
				{
					return Overlay ? Overlay->AreTrianglesConnected(TriIDA, TriIDB) : true;
				};
				
				bool bCanShare = true;
				if (Settings->bSplitAtNormalSeams)
				{
					bCanShare = bCanShare && OverlayCanShare(NormalOverlay);
				}
				if (Settings->bSplitAtUVSeams)
				{
					bCanShare = bCanShare && OverlayCanShare(UVOverlay);
				}
				return bCanShare;
			}, 0.0f);
	});

	// Update BaseMesh color topology.
	BaseMesh.EnableAttributes();
	BaseMesh.Attributes()->DisablePrimaryColors();
	BaseMesh.Attributes()->EnablePrimaryColors();
	PreviewMesh->ProcessMesh([this](const FDynamicMesh3& Mesh)
	{
		BaseMesh.Attributes()->PrimaryColors()->Copy(*Mesh.Attributes()->PrimaryColors());
	});
}

void UBakeMeshAttributeVertexTool::UpdateResult()
{
	if (!bDetailMeshValid)
	{
		UpdateDetailMesh();
		bDetailMeshValid = true;
	}

	if (!bColorTopologyValid)
	{
		UpdateColorTopology();
		bColorTopologyValid = true;
	}

	if (OpState == EBakeOpState::Complete)
	{
		return;
	}

	// clear warning (ugh)
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);

	FBakeSettings BakeSettings;
	BakeSettings.VertexMode = Settings->VertexMode;
	BakeSettings.bUseWorldSpace = Settings->bUseWorldSpace;
	BakeSettings.Thickness = Settings->Thickness;
	if (!(BakeSettings == CachedBakeSettings))
	{
		CachedBakeSettings = BakeSettings;

		CachedColorSettings = FBakeColorSettings();
		CachedChannelSettings = FBakeChannelSettings();
	}

	FBakeColorSettings BakeColorSettings;
	BakeColorSettings.BakeType = ColorSettings->BakeType;
	if (!(BakeColorSettings == CachedColorSettings))
	{
		CachedColorSettings = BakeColorSettings;
	}

	FBakeChannelSettings BakeChannelSettings;
	BakeChannelSettings.BakeType[0] = PerChannelSettings->BakeTypeR;
	BakeChannelSettings.BakeType[1] = PerChannelSettings->BakeTypeG;
	BakeChannelSettings.BakeType[2] = PerChannelSettings->BakeTypeB;
	BakeChannelSettings.BakeType[3] = PerChannelSettings->BakeTypeA;
	if (!(BakeChannelSettings == CachedChannelSettings))
	{
		CachedChannelSettings = BakeChannelSettings;
	}

	// Clear our invalid bitflag to check again for valid inputs.
	OpState = EBakeOpState::Evaluate;

	// Validate bake inputs
	if (CachedBakeSettings.VertexMode == EBakeVertexMode::Color)
	{
		switch(CachedColorSettings.BakeType)
		{
		case EBakeVertexTypeColor::TangentSpaceNormal:
			OpState = UpdateResult_Normal();
			break;
		case EBakeVertexTypeColor::AmbientOcclusion:
		case EBakeVertexTypeColor::BentNormal:
			OpState = UpdateResult_Occlusion();
			break;
		case EBakeVertexTypeColor::Curvature:
			OpState = UpdateResult_Curvature();
			break;
		case EBakeVertexTypeColor::NormalImage:
		case EBakeVertexTypeColor::FaceNormalImage:
		case EBakeVertexTypeColor::PositionImage:
		case EBakeVertexTypeColor::MaterialID:
			OpState = UpdateResult_MeshProperty();
			break;
		case EBakeVertexTypeColor::Texture2DImage:
			OpState = UpdateResult_Texture2DImage();
			break;
		case EBakeVertexTypeColor::MultiTexture:
			OpState = UpdateResult_MultiTexture();
			break;
		}
	}
	else // CachedBakeSettings.VertexMode == EBakeVertexMode::PerChannel
	{
		// The enabled state of these settings are precomputed in UpdateOnModeChange().
		if (OcclusionSettings->IsPropertySetEnabled())
		{
			OpState |= UpdateResult_Occlusion();
		}
		if (CurvatureSettings->IsPropertySetEnabled())
		{
			OpState |= UpdateResult_Curvature();
		}
	}

	// Early exit if op input parameters are invalid.
	if ((bool)(OpState & EBakeOpState::Invalid))
	{
		return;
	}
	else // ((bool)(OpState & EBakeOpState::Evaluate))
	{
		if (!Compute)
		{
			Compute = MakeUnique<TGenericDataBackgroundCompute<FMeshVertexBaker>>();
			Compute->Setup(this);
			Compute->OnResultUpdated.AddLambda([this](const TUniquePtr<FMeshVertexBaker>& NewResult)
			{
				OnResultUpdated(NewResult);
			});
		}
		Compute->InvalidateResult();
	}
}

void UBakeMeshAttributeVertexTool::OnResultUpdated(const TUniquePtr<FMeshVertexBaker>& NewResult)
{
	const TImageBuilder<FVector4f>* ImageResult = NewResult->GetBakeResult();
	if (!ImageResult)
	{
		return;
	}

	// TODO: Review how to handle the implicit sRGB conversion in the StaticMesh build.
	PreviewMesh->DeferredEditMesh([this, &ImageResult](FDynamicMesh3& Mesh)
	{
		const int NumColors = Mesh.Attributes()->PrimaryColors()->ElementCount();
		check(NumColors == ImageResult->GetDimensions().GetWidth());
		for (int Idx = 0; Idx < NumColors; ++Idx)
		{
			const FVector4f& Pixel = ImageResult->GetPixel(Idx);
			Mesh.Attributes()->PrimaryColors()->SetElement(Idx, Pixel);
		}
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
	UpdateVisualization();

	OpState = EBakeOpState::Complete;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Normal()
{
	// No settings to configure, always valid to evaluate.
	return EBakeOpState::Evaluate;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Occlusion()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	FOcclusionMapSettings OcclusionMapSettings;
	OcclusionMapSettings.MaxDistance = (OcclusionSettings->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionSettings->MaxDistance;
	OcclusionMapSettings.OcclusionRays = OcclusionSettings->OcclusionRays;
	OcclusionMapSettings.SpreadAngle = OcclusionSettings->SpreadAngle;
	OcclusionMapSettings.Distribution = OcclusionSettings->Distribution;
	OcclusionMapSettings.BlurRadius = (OcclusionSettings->bGaussianBlur) ? OcclusionSettings->BlurRadius : 0.0;
	OcclusionMapSettings.BiasAngle = OcclusionSettings->BiasAngle;
	OcclusionMapSettings.NormalSpace = OcclusionSettings->NormalSpace;

	if (!(CachedOcclusionMapSettings == OcclusionMapSettings))
	{
		CachedOcclusionMapSettings = OcclusionMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Curvature()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	FCurvatureMapSettings CurvatureMapSettings;
	CurvatureMapSettings.RangeMultiplier = CurvatureSettings->RangeMultiplier;
	CurvatureMapSettings.MinRangeMultiplier = CurvatureSettings->MinRangeMultiplier;
	switch (CurvatureSettings->CurvatureType)
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
	switch (CurvatureSettings->ColorMode)
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
	switch (CurvatureSettings->Clamping)
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
	CurvatureMapSettings.BlurRadius = (CurvatureSettings->bGaussianBlur) ? CurvatureSettings->BlurRadius : 0.0;

	if (!(CachedCurvatureMapSettings == CurvatureMapSettings))
	{
		CachedCurvatureMapSettings = CurvatureMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_MeshProperty()
{
	// No settings to configure, always valid to evaluate.
	return EBakeOpState::Evaluate;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Texture2DImage()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	FTexture2DImageSettings NewSettings;
	NewSettings.UVLayer = 0;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}
	
	if (TextureSettings->SourceTexture == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}

	// The read texture data is always in linear space.
	NewSettings.bSRGB = false;

	{
		CachedTextureImage = MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
		if (!UE::AssetUtils::ReadTexture(TextureSettings->SourceTexture, *CachedTextureImage, /*bPreferPlatformData*/ false))
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

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_MultiTexture()
{
	EBakeOpState ResultState = EBakeOpState::Complete;

	FTexture2DImageSettings NewSettings;
	NewSettings.UVLayer = MultiTextureSettings->UVLayer;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EBakeOpState::Invalid;
	}

	for (auto& InputTexture : MultiTextureSettings->MaterialIDSourceTextureMap)
	{
		if (InputTexture.Value == nullptr)
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}
	}

	CachedMultiTextures.Reset();

	// The read texture data is always in linear space.
	NewSettings.bSRGB = false;
	
	for (auto& InputTexture : MultiTextureSettings->MaterialIDSourceTextureMap)
	{
		UTexture2D* Texture = InputTexture.Value;
		if (!ensure(Texture != nullptr))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EBakeOpState::Invalid;
		}

		int32 MaterialID = InputTexture.Key;
		CachedMultiTextures.Add(MaterialID, MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>());
		if (!UE::AssetUtils::ReadTexture(Texture, *CachedMultiTextures[MaterialID], /*bPreferPlatformData*/ false))
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



#undef LOCTEXT_NAMESPACE
