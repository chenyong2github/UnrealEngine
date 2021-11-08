// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeVertexTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"

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

#include "EngineAnalytics.h"

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
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	FDynamicMesh3* BaseMesh;
	TSharedPtr<TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	TUniquePtr<FMeshVertexBaker> Baker;

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
		Baker->SetTargetMeshTangents(BaseMeshTangents);
		Baker->SetThickness(BakeSettings.Thickness);
		Baker->BakeMode = BakeSettings.VertexMode == EBakeVertexMode::Color ? FMeshVertexBaker::EBakeMode::Color : FMeshVertexBaker::EBakeMode::Channel;
		
		FMeshBakerDynamicMeshSampler DetailSampler(DetailMesh.Get(), DetailSpatial.Get());
		Baker->SetDetailSampler(&DetailSampler);

		auto InitOcclusionEvaluator = [this] (FMeshOcclusionMapEvaluator* OcclusionEval, const EMeshOcclusionMapType OcclusionType)
		{
			OcclusionEval->OcclusionType = OcclusionType;
			OcclusionEval->NumOcclusionRays = OcclusionSettings.OcclusionRays;
			OcclusionEval->MaxDistance = OcclusionSettings.MaxDistance;
			OcclusionEval->SpreadAngle = OcclusionSettings.SpreadAngle;
			OcclusionEval->BiasAngleDeg = OcclusionSettings.BiasAngle;
		};

		auto InitCurvatureEvaluator = [this] (FMeshCurvatureMapEvaluator* CurvatureEval)
		{
			CurvatureEval->RangeScale = FMathd::Clamp(CurvatureSettings.RangeMultiplier, 0.0001, 1000.0);
			CurvatureEval->MinRangeScale = FMathd::Clamp(CurvatureSettings.MinRangeMultiplier, 0.0, 1.0);
			CurvatureEval->UseCurvatureType = static_cast<FMeshCurvatureMapEvaluator::ECurvatureType>(CurvatureSettings.CurvatureType);
			CurvatureEval->UseColorMode = static_cast<FMeshCurvatureMapEvaluator::EColorMode>(CurvatureSettings.ColorMode);
			CurvatureEval->UseClampMode = static_cast<FMeshCurvatureMapEvaluator::EClampMode>(CurvatureSettings.ClampMode);
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
					InitCurvatureEvaluator(CurvatureEval.Get());
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
				TSharedPtr<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe> CurvatureEval = MakeShared<FMeshCurvatureMapEvaluator, ESPMode::ThreadSafe>();
				InitCurvatureEvaluator(CurvatureEval.Get());
				Baker->ColorEvaluator = CurvatureEval;
				break;
			}
			case EBakeVertexTypeColor::Position:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyEval = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyEval->Property = EMeshPropertyMapType::Position;
				Baker->ColorEvaluator = PropertyEval;
				break;
			}
			case EBakeVertexTypeColor::ObjectSpaceNormal:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyEval = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyEval->Property = EMeshPropertyMapType::Normal;
				Baker->ColorEvaluator = PropertyEval;
				break;
			}
			case EBakeVertexTypeColor::FaceNormal:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyEval = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyEval->Property = EMeshPropertyMapType::FacetNormal;
				Baker->ColorEvaluator = PropertyEval;
				break;
			}
			case EBakeVertexTypeColor::MaterialID:
			{
				TSharedPtr<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe> PropertyEval = MakeShared<FMeshPropertyMapEvaluator, ESPMode::ThreadSafe>();
				PropertyEval->Property = EMeshPropertyMapType::MaterialID;
				Baker->ColorEvaluator = PropertyEval;
				break;
			}
			case EBakeVertexTypeColor::Texture:
			{
				TSharedPtr<FMeshResampleImageEvaluator, ESPMode::ThreadSafe> TextureEval = MakeShared<FMeshResampleImageEvaluator, ESPMode::ThreadSafe>();
				DetailSampler.SetColorMap(DetailMesh.Get(), IMeshBakerDetailSampler::FBakeDetailTexture(TextureImage.Get(), TextureSettings.UVLayer));
				Baker->ColorEvaluator = TextureEval;
				break;
			}
			case EBakeVertexTypeColor::MultiTexture:
			{
				TSharedPtr<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe> TextureEval = MakeShared<FMeshMultiResampleImageEvaluator, ESPMode::ThreadSafe>();
				TextureEval->DetailUVLayer = TextureSettings.UVLayer;
				TextureEval->MultiTextures = MaterialToTextureImageMap;
				Baker->ColorEvaluator = TextureEval;
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
	Super::Setup();

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

	bIsBakeToSelf = (Targets.Num() == 1);

	UE::ToolTarget::HideSourceObject(Targets[0]);

	const FDynamicMesh3 InputMeshWithTangents = UE::ToolTarget::GetDynamicMeshCopy(Targets[0], true);
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(PreviewMesh, nullptr);
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

	Settings->WatchProperty(Settings->VertexMode, [this](EBakeVertexMode) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });
	Settings->WatchProperty(Settings->VertexChannelPreview, [this](EBakeVertexChannel) { UpdateVisualization(); });
	Settings->WatchProperty(Settings->Thickness, [this](float) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->bUseWorldSpace, [this](bool)	{ OpState |= EBakeOpState::EvaluateDetailMesh; });
	Settings->WatchProperty(Settings->bSplitAtNormalSeams, [this](bool) { bColorTopologyValid = false; OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->bSplitAtUVSeams, [this](bool) { bColorTopologyValid = false; OpState |= EBakeOpState::Evaluate; });

	ColorSettings = NewObject<UBakeMeshAttributeVertexToolColorProperties>(this);
	ColorSettings->RestoreProperties(this);
	AddToolPropertySource(ColorSettings);
	SetToolPropertySourceEnabled(ColorSettings, false);
	ColorSettings->WatchProperty(ColorSettings->BakeType, [this](EBakeVertexTypeColor) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });

	PerChannelSettings = NewObject<UBakeMeshAttributeVertexToolChannelProperties>(this);
	PerChannelSettings->RestoreProperties(this);
	AddToolPropertySource(PerChannelSettings);
	SetToolPropertySourceEnabled(PerChannelSettings, false);
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeR, [this](EBakeVertexTypeChannel) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeG, [this](EBakeVertexTypeChannel) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeB, [this](EBakeVertexTypeChannel) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });
	PerChannelSettings->WatchProperty(PerChannelSettings->BakeTypeA, [this](EBakeVertexTypeChannel) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });

	OcclusionSettings = NewObject<UBakedOcclusionMapToolProperties>(this);
	OcclusionSettings->RestoreProperties(this);
	AddToolPropertySource(OcclusionSettings);
	SetToolPropertySourceEnabled(OcclusionSettings, false);
	OcclusionSettings->WatchProperty(OcclusionSettings->OcclusionRays, [this](int32) { OpState |= EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->MaxDistance, [this](float) { OpState |= EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->SpreadAngle, [this](float) { OpState |= EBakeOpState::Evaluate; });
	OcclusionSettings->WatchProperty(OcclusionSettings->BiasAngle, [this](float) { OpState |= EBakeOpState::Evaluate; });

	CurvatureSettings = NewObject<UBakedCurvatureMapToolProperties>(this);
	CurvatureSettings->RestoreProperties(this);
	AddToolPropertySource(CurvatureSettings);
	SetToolPropertySourceEnabled(CurvatureSettings, false);
	CurvatureSettings->WatchProperty(CurvatureSettings->RangeMultiplier, [this](float) { OpState |= EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->MinRangeMultiplier, [this](float) { OpState |= EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->CurvatureType, [this](EBakedCurvatureTypeMode) { OpState |= EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->ColorMode, [this](EBakedCurvatureColorMode) { OpState |= EBakeOpState::Evaluate; });
	CurvatureSettings->WatchProperty(CurvatureSettings->Clamping, [this](EBakedCurvatureClampMode) { OpState |= EBakeOpState::Evaluate; });

	TextureSettings = NewObject<UBakedTexture2DImageProperties>(this);
	TextureSettings->RestoreProperties(this);
	AddToolPropertySource(TextureSettings);
	SetToolPropertySourceEnabled(TextureSettings, false);
	TextureSettings->WatchProperty(TextureSettings->UVLayer, [this](float) { OpState |= EBakeOpState::Evaluate; });
	TextureSettings->WatchProperty(TextureSettings->SourceTexture, [this](UTexture2D*) { OpState |= EBakeOpState::Evaluate; });

	MultiTextureSettings = NewObject<UBakedMultiTexture2DImageProperties>(this);
	MultiTextureSettings->RestoreProperties(this);
	AddToolPropertySource(MultiTextureSettings);
	SetToolPropertySourceEnabled(MultiTextureSettings, false);
	auto SetDirtyCallback = [this](decltype(MultiTextureSettings->MaterialIDSourceTextureMap)) { OpState |= EBakeOpState::Evaluate; };
	auto NotEqualsCallback = [](const decltype(MultiTextureSettings->MaterialIDSourceTextureMap)& A, const decltype(MultiTextureSettings->MaterialIDSourceTextureMap)& B) -> bool { return !(A.OrderIndependentCompareEqual(B)); };
	MultiTextureSettings->WatchProperty(MultiTextureSettings->MaterialIDSourceTextureMap, SetDirtyCallback, NotEqualsCallback);
	MultiTextureSettings->WatchProperty(MultiTextureSettings->UVLayer, [this](float) { OpState |= EBakeOpState::Evaluate; });

	UpdateOnModeChange();

	UpdateDetailMesh();

	SetToolDisplayName(LOCTEXT("ToolName", "Bake Vertex Colors"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool",
		        "Bake Vertex Colors. Select Bake Mesh (LowPoly) first, then (optionally) Detail Mesh second."),
		EToolMessageLevel::UserNotification);

	GatherAnalytics(BakeAnalytics.MeshSettings);
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

	RecordAnalytics(BakeAnalytics, TEXT("BakeVertex"));
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

	UToolTarget* DetailTarget = Targets[bIsBakeToSelf ? 0 : 1];
	ProcessComponentTextures(UE::ToolTarget::GetTargetComponent(DetailTarget), [this](const int MaterialID, const TArray<UTexture*>& Textures)
	{
		for (UTexture* Tex : Textures)
		{
			UTexture2D* Tex2D = Cast<UTexture2D>(Tex);
			if (Tex2D)
			{
				MultiTextureSettings->AllSourceTextures.Add(Tex2D);
			}
		}

		constexpr bool bGuessAtTextures = true;
		if (bGuessAtTextures)
		{
			const int SelectedTextureIndex = SelectColorTextureToBake(Textures);
			if (SelectedTextureIndex >= 0)
			{
				UTexture2D* Tex2D = Cast<UTexture2D>(Textures[SelectedTextureIndex]);

				// if cast fails, this will set the value to nullptr, which is fine
				MultiTextureSettings->MaterialIDSourceTextureMap.Add(MaterialID, Tex2D);	
			}
		}
		else
		{
			MultiTextureSettings->MaterialIDSourceTextureMap.Add(MaterialID, nullptr);
		}
	});

	OpState &= ~EBakeOpState::EvaluateDetailMesh;
	OpState |= EBakeOpState::Evaluate;
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
			case EBakeVertexTypeColor::Texture:
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

	bColorTopologyValid = true;
}

void UBakeMeshAttributeVertexTool::UpdateResult()
{
	if (static_cast<bool>(OpState & EBakeOpState::EvaluateDetailMesh))
	{
		UpdateDetailMesh();
	}

	if (!bColorTopologyValid)
	{
		UpdateColorTopology();
	}

	if (OpState == EBakeOpState::Clean)
	{
		return;
	}

	// clear warning (ugh)
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);

	FBakeSettings BakeSettings;
	BakeSettings.VertexMode = Settings->VertexMode;
	BakeSettings.bSplitAtNormalSeams = Settings->bSplitAtNormalSeams;
	BakeSettings.bSplitAtUVSeams = Settings->bSplitAtUVSeams;
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
	OpState &= ~EBakeOpState::Invalid;

	// Validate bake inputs
	if (CachedBakeSettings.VertexMode == EBakeVertexMode::Color)
	{
		switch(CachedColorSettings.BakeType)
		{
		case EBakeVertexTypeColor::TangentSpaceNormal:
			OpState |= UpdateResult_Normal();
			break;
		case EBakeVertexTypeColor::AmbientOcclusion:
		case EBakeVertexTypeColor::BentNormal:
			OpState |= UpdateResult_Occlusion();
			break;
		case EBakeVertexTypeColor::Curvature:
			OpState |= UpdateResult_Curvature();
			break;
		case EBakeVertexTypeColor::ObjectSpaceNormal:
		case EBakeVertexTypeColor::FaceNormal:
		case EBakeVertexTypeColor::Position:
		case EBakeVertexTypeColor::MaterialID:
			OpState |= UpdateResult_MeshProperty();
			break;
		case EBakeVertexTypeColor::Texture:
			OpState |= UpdateResult_Texture2DImage();
			break;
		case EBakeVertexTypeColor::MultiTexture:
			OpState |= UpdateResult_MultiTexture();
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
	OpState = EBakeOpState::Clean;
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

	GatherAnalytics(*NewResult, CachedBakeSettings, CachedColorSettings, CachedChannelSettings, BakeAnalytics);
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Normal()
{
	// No settings to configure, always valid to evaluate.
	return EBakeOpState::Evaluate;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Occlusion()
{
	EBakeOpState ResultState = EBakeOpState::Clean;

	FOcclusionMapSettings OcclusionMapSettings;
	OcclusionMapSettings.MaxDistance = (OcclusionSettings->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionSettings->MaxDistance;
	OcclusionMapSettings.OcclusionRays = OcclusionSettings->OcclusionRays;
	OcclusionMapSettings.SpreadAngle = OcclusionSettings->SpreadAngle;
	OcclusionMapSettings.BiasAngle = OcclusionSettings->BiasAngle;

	if (!(CachedOcclusionMapSettings == OcclusionMapSettings))
	{
		CachedOcclusionMapSettings = OcclusionMapSettings;
		ResultState = EBakeOpState::Evaluate;
	}
	return ResultState;
}

EBakeOpState UBakeMeshAttributeVertexTool::UpdateResult_Curvature()
{
	EBakeOpState ResultState = EBakeOpState::Clean;

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
	EBakeOpState ResultState = EBakeOpState::Clean;

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


	{
		CachedTextureImage = MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
		if (!UE::AssetUtils::ReadTexture(TextureSettings->SourceTexture, *CachedTextureImage, bPreferPlatformData))
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
	EBakeOpState ResultState = EBakeOpState::Clean;

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
		if (!UE::AssetUtils::ReadTexture(Texture, *CachedMultiTextures[MaterialID], bPreferPlatformData))
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


void UBakeMeshAttributeVertexTool::GatherAnalytics(FBakeAnalytics::FMeshSettings& Data)
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}
	
	Data.NumTargetMeshVerts = BaseMesh.VertexCount();
	Data.NumTargetMeshTris = BaseMesh.TriangleCount();
	Data.NumDetailMesh = 1;
	Data.NumDetailMeshTris = DetailMesh->TriangleCount();
}


void UBakeMeshAttributeVertexTool::GatherAnalytics(
	const FMeshVertexBaker& Result,
	const FBakeSettings& Settings,
	const FBakeColorSettings& ColorSettings,
	const FBakeChannelSettings& ChannelSettings,
	FBakeAnalytics& Data)
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}
	
	Data.TotalBakeDuration = Result.TotalBakeDuration;
	Data.BakeSettings = Settings;
	Data.BakeColorSettings = ColorSettings;
	Data.BakeChannelSettings = ChannelSettings;

	auto GatherEvaluatorData = [&Data](const FMeshMapEvaluator* Eval)
	{
		if (Eval)
		{
			switch(Eval->Type())
			{
			case EMeshMapEvaluatorType::Occlusion:
			{
				const FMeshOcclusionMapEvaluator* OcclusionEval = static_cast<const FMeshOcclusionMapEvaluator*>(Eval);
				Data.OcclusionSettings.OcclusionRays = OcclusionEval->NumOcclusionRays;
				Data.OcclusionSettings.MaxDistance = OcclusionEval->MaxDistance;
				Data.OcclusionSettings.SpreadAngle = OcclusionEval->SpreadAngle;
				Data.OcclusionSettings.BiasAngle = OcclusionEval->BiasAngleDeg;
				break;
			}
			case EMeshMapEvaluatorType::Curvature:
			{
				const FMeshCurvatureMapEvaluator* CurvatureEval = static_cast<const FMeshCurvatureMapEvaluator*>(Eval);
				Data.CurvatureSettings.CurvatureType = static_cast<int>(CurvatureEval->UseCurvatureType);
				Data.CurvatureSettings.RangeMultiplier = CurvatureEval->RangeScale;
				Data.CurvatureSettings.MinRangeMultiplier = CurvatureEval->MinRangeScale;
				Data.CurvatureSettings.ColorMode = static_cast<int>(CurvatureEval->UseColorMode);
				Data.CurvatureSettings.ClampMode = static_cast<int>(CurvatureEval->UseClampMode);
				break;
			}
			default:
				break;
			};
		}
	};

	if (Result.BakeMode == FMeshVertexBaker::EBakeMode::Color)
	{
		GatherEvaluatorData(Result.ColorEvaluator.Get());
	}
	else // Result.BakeMode == FMeshVertexBaker::EBakeMode::Channel
	{
		for (int EvalId = 0; EvalId < 4; ++EvalId)
		{
			GatherEvaluatorData(Result.ChannelEvaluators[EvalId].Get());
		}
	}
}


void UBakeMeshAttributeVertexTool::RecordAnalytics(const FBakeAnalytics& Data, const FString& EventName)
{
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}
	
	TArray<FAnalyticsEventAttribute> Attributes;

	// General
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Bake.Duration.Total.Seconds"), Data.TotalBakeDuration));

	// Mesh data
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Input.TargetMesh.NumTriangles"), Data.MeshSettings.NumTargetMeshTris));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Input.TargetMesh.NumVertices"), Data.MeshSettings.NumTargetMeshVerts));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Input.DetailMesh.NumMeshes"), Data.MeshSettings.NumDetailMesh));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Input.DetailMesh.NumTriangles"), Data.MeshSettings.NumDetailMeshTris));

	// Bake settings
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Thickness"), Data.BakeSettings.Thickness));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Split.NormalSeams"), Data.BakeSettings.bSplitAtNormalSeams));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Split.UVSeams"), Data.BakeSettings.bSplitAtUVSeams));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Thickness"), Data.BakeSettings.Thickness));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.UseWorldSpace"), Data.BakeSettings.bUseWorldSpace));

	const FString OutputType = Data.BakeSettings.VertexMode == EBakeVertexMode::Color ? TEXT("RGBA") : TEXT("PerChannel");
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Settings.Output.Type"), OutputType));

	auto RecordAmbientOcclusionSettings = [&Attributes, &Data](const FString& ModeName)
	{
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.AmbientOcclusion.OcclusionRays"), *ModeName), Data.OcclusionSettings.OcclusionRays));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.AmbientOcclusion.MaxDistance"), *ModeName), Data.OcclusionSettings.MaxDistance));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.AmbientOcclusion.SpreadAngle"), *ModeName), Data.OcclusionSettings.SpreadAngle));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.AmbientOcclusion.BiasAngle"), *ModeName), Data.OcclusionSettings.BiasAngle));
	};

	auto RecordBentNormalSettings = [&Attributes, &Data](const FString& ModeName)
	{
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.BentNormal.OcclusionRays"), *ModeName), Data.OcclusionSettings.OcclusionRays));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.BentNormal.MaxDistance"), *ModeName), Data.OcclusionSettings.MaxDistance));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.BentNormal.SpreadAngle"), *ModeName), Data.OcclusionSettings.SpreadAngle));
	};

	auto RecordCurvatureSettings = [&Attributes, &Data](const FString& ModeName)
	{
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Curvature.CurvatureType"), *ModeName), Data.CurvatureSettings.CurvatureType));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Curvature.RangeMultiplier"), *ModeName), Data.CurvatureSettings.RangeMultiplier));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Curvature.MinRangeMultiplier"), *ModeName), Data.CurvatureSettings.MinRangeMultiplier));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Curvature.ClampMode"), *ModeName), Data.CurvatureSettings.ClampMode));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Curvature.ColorMode"), *ModeName), Data.CurvatureSettings.ColorMode));
	};

	if (Data.BakeSettings.VertexMode == EBakeVertexMode::Color)
	{
		const FString OutputName(TEXT("RGBA"));

		FString OutputTypeName = StaticEnum<EBakeVertexTypeColor>()->GetNameStringByIndex(static_cast<int>(Data.BakeColorSettings.BakeType));
		Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Type"), *OutputName), OutputTypeName));

		switch (Data.BakeColorSettings.BakeType)
		{
		case EBakeVertexTypeColor::AmbientOcclusion:
			RecordAmbientOcclusionSettings(OutputName);
			break;
		case EBakeVertexTypeColor::BentNormal:
			RecordBentNormalSettings(OutputName);
			break;
		case EBakeVertexTypeColor::Curvature:
			RecordCurvatureSettings(OutputName);
			break;
		default:
			break;
		}
	}
	else
	{
		ensure(Data.BakeSettings.VertexMode == EBakeVertexMode::PerChannel);
		for (int EvalId = 0; EvalId < 4; ++EvalId)
		{
			FString OutputName = StaticEnum<EBakeVertexChannel>()->GetNameStringByIndex(EvalId);
			FString OutputTypeName = StaticEnum<EBakeVertexTypeChannel>()->GetNameStringByIndex(static_cast<int>(Data.BakeChannelSettings.BakeType[EvalId]));
			Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Settings.Output.%s.Type"), *OutputName), OutputTypeName));

			switch (Data.BakeChannelSettings.BakeType[EvalId])
			{
			case EBakeVertexTypeChannel::AmbientOcclusion:
				RecordAmbientOcclusionSettings(OutputName);
				break;
			case EBakeVertexTypeChannel::Curvature:
				RecordCurvatureSettings(OutputName);
				break;
			default:
				break;
			}
		}
	}

	FEngineAnalytics::GetProvider().RecordEvent(FString(TEXT("Editor.Usage.MeshModelingMode.")) + EventName, Attributes);

	constexpr bool bLogAnalytics = false; 
	if constexpr (bLogAnalytics)
	{
		for (const FAnalyticsEventAttribute& Attr : Attributes)
		{
			UE_LOG(LogGeometry, Log, TEXT("[%s] %s = %s"), *EventName, *Attr.GetName(), *Attr.GetValue());
		}
	}
}


#undef LOCTEXT_NAMESPACE
