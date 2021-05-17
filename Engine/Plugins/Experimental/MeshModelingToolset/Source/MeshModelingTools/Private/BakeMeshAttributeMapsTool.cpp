// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeMapsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Sampling/MeshImageBakingCache.h"
#include "Sampling/MeshNormalMapBaker.h"
#include "Sampling/MeshOcclusionMapBaker.h"
#include "Sampling/MeshCurvatureMapBaker.h"
#include "Sampling/MeshPropertyMapBaker.h"
#include "Sampling/MeshResampleImageBaker.h"
#include "Util/IndexUtil.h"

#include "SimpleDynamicMeshComponent.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/MeshDescriptionUtil.h"
#include "AssetGenerationUtil.h"

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
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




TArray<FString> UBakeMeshAttributeMapsToolProperties::GetUVLayerNamesFunc()
{
	return UVLayerNamesList;
}


/*
 * Operators
 */

class FBakeMapBaseOp : public TGenericDataOperator<FMeshMapBaker>
{
public:
	virtual ~FBakeMapBaseOp() {}

	TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DetailMesh;
	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> DetailSpatial;
	UE::Geometry::FDynamicMesh3* BaseMesh;
	TUniquePtr<UE::Geometry::FMeshMapBaker> Baker;
	UBakeMeshAttributeMapsTool::FBakeCacheSettings BakeCacheSettings;

	//
	// TGenericDataOperator implementation
	//
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
	}
};

class FBakeNormalMapOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;

	// inputs
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UBakeMeshAttributeMapsTool::FNormalMapSettings Settings;

public:
	virtual ~FBakeNormalMapOp() {}

	//
	// TGenericDataOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TSharedPtr<FMeshNormalMapBaker, ESPMode::ThreadSafe> NormalBaker = MakeShared<FMeshNormalMapBaker, ESPMode::ThreadSafe>();
		Baker->AddBaker(NormalBaker);
		Baker->SetTargetMeshTangents(BaseMeshTangents);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};

class FBakeOcclusionMapOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;

	// inputs
	TSharedPtr<UE::Geometry::TMeshTangents<double>, ESPMode::ThreadSafe> BaseMeshTangents;
	UBakeMeshAttributeMapsTool::FOcclusionMapSettings Settings;

public:
	virtual ~FBakeOcclusionMapOp() {}

	//
	// TGenericDataOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);
		 
		if (Progress && Progress->Cancelled())
		{
			return;
		}

		auto InitializeBaker = [this](TSharedPtr<FMeshOcclusionMapBaker>& BakerIn) {
			BakerIn->NumOcclusionRays = Settings.OcclusionRays;
			BakerIn->MaxDistance = Settings.MaxDistance;
			BakerIn->SpreadAngle = Settings.SpreadAngle;
			BakerIn->BlurRadius = Settings.BlurRadius;
			BakerIn->BiasAngleDeg = Settings.BiasAngle;

			switch (Settings.Distribution)
			{
			case EOcclusionMapDistribution::Cosine:
				BakerIn->Distribution = FMeshOcclusionMapBaker::EDistribution::Cosine;
				break;
			case EOcclusionMapDistribution::Uniform:
				BakerIn->Distribution = FMeshOcclusionMapBaker::EDistribution::Uniform;
				break;
			}

			switch (Settings.NormalSpace)
			{
			case ENormalMapSpace::Tangent:
				BakerIn->NormalSpace = FMeshOcclusionMapBaker::ESpace::Tangent;
				break;
			case ENormalMapSpace::Object:
				BakerIn->NormalSpace = FMeshOcclusionMapBaker::ESpace::Object;
				break;
			}
		};

		TSharedPtr<FMeshOcclusionMapBaker> OcclusionBaker = MakeShared<FMeshOcclusionMapBaker>();
		InitializeBaker(OcclusionBaker);
		OcclusionBaker->OcclusionType = EOcclusionMapType::AmbientOcclusion;

		TSharedPtr<FMeshOcclusionMapBaker> BentNormalBaker = MakeShared<FMeshOcclusionMapBaker>();
		InitializeBaker(BentNormalBaker);
		BentNormalBaker->OcclusionType = EOcclusionMapType::BentNormal;

		Baker->SetTargetMeshTangents(BaseMeshTangents);
		Baker->AddBaker(OcclusionBaker);
		Baker->AddBaker(BentNormalBaker);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};

class FBakeCurvatureMapOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;

	// inputs
	UBakeMeshAttributeMapsTool::FCurvatureMapSettings Settings;

public:
	virtual ~FBakeCurvatureMapOp() {}

	//
	// TGenericDataOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TSharedPtr<FMeshCurvatureMapBaker> CurvatureBaker = MakeShared<FMeshCurvatureMapBaker>();
		CurvatureBaker->RangeScale = FMathd::Clamp(Settings.RangeMultiplier, 0.0001, 1000.0);
		CurvatureBaker->MinRangeScale = FMathd::Clamp(Settings.MinRangeMultiplier, 0.0, 1.0);
		CurvatureBaker->UseCurvatureType = (FMeshCurvatureMapBaker::ECurvatureType)Settings.CurvatureType;
		CurvatureBaker->UseColorMode = (FMeshCurvatureMapBaker::EColorMode)Settings.ColorMode;
		CurvatureBaker->UseClampMode = (FMeshCurvatureMapBaker::EClampMode)Settings.ClampMode;
		CurvatureBaker->BlurRadius = Settings.BlurRadius;
		Baker->AddBaker(CurvatureBaker);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};

class FBakeMeshPropertyMapOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;

	// inputs
	UBakeMeshAttributeMapsTool::FMeshPropertyMapSettings Settings;

public:
	virtual ~FBakeMeshPropertyMapOp() {}

	//
	// TGenericDataOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TSharedPtr<FMeshPropertyMapBaker> PropertyBaker = MakeShared<FMeshPropertyMapBaker>();
		PropertyBaker->Property = (EMeshPropertyBakeType)Settings.PropertyTypeIndex;
		Baker->AddBaker(PropertyBaker);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};

class FBakeTexture2DImageMapOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;

	// inputs
	const FDynamicMeshUVOverlay* UVOverlay = nullptr;
	TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> TextureImage;
	UBakeMeshAttributeMapsTool::FTexture2DImageSettings Settings;

public:
	virtual ~FBakeTexture2DImageMapOp() {}

	//
	// TGenericDataOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TSharedPtr<FMeshResampleImageBaker> ResampleBaker = MakeShared<FMeshResampleImageBaker>();
		ResampleBaker->DetailUVOverlay = UVOverlay;
		ResampleBaker->SampleFunction = [this](FVector2d UVCoord) {
			return TextureImage->BilinearSampleUV<float>(UVCoord, FVector4f(0, 0, 0, 1));
		};
		Baker->AddBaker(ResampleBaker);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};



class FBakeMultiTextureOp : public FBakeMapBaseOp
{
public:
	typedef FBakeMapBaseOp Super;
	using ImagePtr = TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>;

	// inputs
	const FDynamicMeshUVOverlay* UVOverlay = nullptr;
	TMap<int32, ImagePtr> MaterialToTextureImageMap;

	UBakeMeshAttributeMapsTool::FTexture2DImageSettings Settings;

public:

	virtual ~FBakeMultiTextureOp() {}

	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Super::CalculateResult(Progress);
		if (Progress && Progress->Cancelled())
		{
			return;
		}

		TSharedPtr<FMeshMultiResampleImageBaker> TextureBaker = MakeShared<FMeshMultiResampleImageBaker>();
		TextureBaker->DetailUVOverlay = UVOverlay;
		TextureBaker->MultiTextures = MaterialToTextureImageMap;
		Baker->AddBaker(TextureBaker);
		Baker->Bake();
		SetResult(MoveTemp(Baker));
	}
};

/*
 * Tool
 */

void UBakeMeshAttributeMapsTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
{
	AssetAPI = AssetAPIIn;
}

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

	Settings->WatchProperty(Settings->MapType, [this](EBakeMapType) { bInputsDirty = true; UpdateOnModeChange(); });
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
	OcclusionMapProps->WatchProperty(OcclusionMapProps->Preview, [this](EOcclusionMapPreview) { UpdateVisualization(); GetToolManager()->PostInvalidation(); });
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
		bCanAccept = Settings->Result.Num() > 0;
		for (UTexture2D* Result : Settings->Result)
		{
			bCanAccept = bCanAccept && Result;
		}
	}
	return bCanAccept;
}


TUniquePtr<UE::Geometry::TGenericDataOperator<FMeshMapBaker>> UBakeMeshAttributeMapsTool::MakeNewOperator()
{
	auto InitializeBaseOp = [this](FBakeMapBaseOp* Op)
	{
		Op->DetailMesh = DetailMesh;
		Op->DetailSpatial = DetailSpatial;
		Op->BaseMesh = &BaseMesh;
		Op->BakeCacheSettings = CachedBakeCacheSettings;
	};

	switch (Settings->MapType)
	{
	case EBakeMapType::TangentSpaceNormalMap:
	{
		TUniquePtr<FBakeNormalMapOp> Op = MakeUnique<FBakeNormalMapOp>();
		InitializeBaseOp(Op.Get());
		Op->BaseMeshTangents = BaseMeshTangents;
		Op->Settings = CachedNormalMapSettings;
		return Op;
	}
	case EBakeMapType::Occlusion:
	{
		TUniquePtr<FBakeOcclusionMapOp> Op = MakeUnique<FBakeOcclusionMapOp>();
		InitializeBaseOp(Op.Get());
		Op->BaseMeshTangents = BaseMeshTangents;
		Op->Settings = CachedOcclusionMapSettings;
		return Op;
	}
	case EBakeMapType::Curvature:
	{
		TUniquePtr<FBakeCurvatureMapOp> Op = MakeUnique<FBakeCurvatureMapOp>();
		InitializeBaseOp(Op.Get());
		Op->Settings = CachedCurvatureMapSettings;
		return Op;
	}
	case EBakeMapType::PositionImage:
	case EBakeMapType::NormalImage:
	case EBakeMapType::FaceNormalImage:
	case EBakeMapType::MaterialID:
	{
		TUniquePtr<FBakeMeshPropertyMapOp> Op = MakeUnique<FBakeMeshPropertyMapOp>();
		InitializeBaseOp(Op.Get());
		Op->Settings = CachedMeshPropertyMapSettings;
		return Op;
	}
	case EBakeMapType::Texture2DImage:
	{
		TUniquePtr<FBakeTexture2DImageMapOp> Op = MakeUnique<FBakeTexture2DImageMapOp>();
		InitializeBaseOp(Op.Get());
		Op->UVOverlay = DetailMesh->Attributes()->GetUVLayer(CachedTexture2DImageSettings.UVLayer);
		Op->TextureImage = CachedTextureImage;
		Op->Settings = CachedTexture2DImageSettings;
		return Op;
	}
	case EBakeMapType::MultiTexture:
	{
		TUniquePtr<FBakeMultiTextureOp> Op = MakeUnique<FBakeMultiTextureOp>();
		InitializeBaseOp(Op.Get());
		Op->UVOverlay = DetailMesh->Attributes()->GetUVLayer(CachedTexture2DImageSettings.UVLayer);
		Op->MaterialToTextureImageMap = CachedMultiTextures;
		Op->Settings = CachedTexture2DImageSettings;
		return Op;
	}
	}
	check(false);
	return nullptr;
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

			if (AssetAPI != nullptr)
			{
				bool bCreatedAssetOK = false;
				switch (Settings->MapType)
				{
				default:
					check(false);
					break;

				case EBakeMapType::TangentSpaceNormalMap:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::NormalMap);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_Normals"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::Occlusion:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::AmbientOcclusion);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_Occlusion"), *BaseName), StaticMeshAsset);
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[1], FTexture2DBuilder::ETextureType::NormalMap);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[1],
						FString::Printf(TEXT("%s_BentNormal"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::Curvature:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_Curvature"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::NormalImage:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_NormalImg"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::FaceNormalImage:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_FaceNormalImg"), *BaseName), StaticMeshAsset);
					break;
				case EBakeMapType::MaterialID:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
																				  FString::Printf(TEXT("%s_MaterialIDImg"), *BaseName), StaticMeshAsset);
					break;
				case EBakeMapType::PositionImage:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_PositionImg"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::Texture2DImage:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
						FString::Printf(TEXT("%s_TextureImg"), *BaseName), StaticMeshAsset);
					break;

				case EBakeMapType::MultiTexture:
					FTexture2DBuilder::CopyPlatformDataToSourceData(Settings->Result[0], FTexture2DBuilder::ETextureType::Color);
					bCreatedAssetOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, Settings->Result[0],
																				  FString::Printf(TEXT("%s_MultiTextureImg"), *BaseName), StaticMeshAsset);
					break;

				}
				ensure(bCreatedAssetOK);
			}

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

	// update map type settings
	switch (Settings->MapType)
	{
		default:
		case EBakeMapType::TangentSpaceNormalMap:
			OpState = UpdateResult_Normal();
			break;
		case EBakeMapType::Occlusion:
			OpState = UpdateResult_Occlusion();
			break;
		case EBakeMapType::Curvature:
			OpState = UpdateResult_Curvature();
			break;

		case EBakeMapType::NormalImage:
		case EBakeMapType::FaceNormalImage:
		case EBakeMapType::PositionImage:
		case EBakeMapType::MaterialID:
			OpState = UpdateResult_MeshProperty();
			break;

		case EBakeMapType::Texture2DImage:
			OpState = UpdateResult_Texture2DImage();
			break;

		case EBakeMapType::MultiTexture:
			OpState = UpdateResult_MultiTexture();
			break;

	}

	// Early exit if op input parameters are invalid.
	if (OpState == EOpState::Invalid)
	{
		return;
	}

	// This should be the only point of compute invalidation to
	// minimize synchronization issues.
	bool bInvalidate = bInputsDirty || (OpState == EOpState::Evaluate);
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



UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_Normal()
{
	EOpState ResultState = EOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FNormalMapSettings NormalMapSettings;
	NormalMapSettings.Dimensions = Dimensions;

	if (!(CachedNormalMapSettings == NormalMapSettings))
	{
		CachedNormalMapSettings = NormalMapSettings;
		ResultState = EOpState::Evaluate;
	}
	return ResultState;
}


UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_Occlusion()
{
	EOpState ResultState = EOpState::Complete;

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
		ResultState = EOpState::Evaluate;
	}
	return ResultState;
}



UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_Curvature()
{
	EOpState ResultState = EOpState::Complete;

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
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapBaker::ECurvatureType::Mean;
		break;
	case EBakedCurvatureTypeMode::Gaussian:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapBaker::ECurvatureType::Gaussian;
		break;
	case EBakedCurvatureTypeMode::Max:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapBaker::ECurvatureType::MaxPrincipal;
		break;
	case EBakedCurvatureTypeMode::Min:
		CurvatureMapSettings.CurvatureType = (int32)FMeshCurvatureMapBaker::ECurvatureType::MinPrincipal;
		break;
	}
	switch (CurvatureMapProps->ColorMode)
	{
	default:
	case EBakedCurvatureColorMode::Grayscale:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapBaker::EColorMode::BlackGrayWhite;
		break;
	case EBakedCurvatureColorMode::RedBlue:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapBaker::EColorMode::RedBlue;
		break;
	case EBakedCurvatureColorMode::RedGreenBlue:
		CurvatureMapSettings.ColorMode = (int32)FMeshCurvatureMapBaker::EColorMode::RedGreenBlue;
		break;
	}
	switch (CurvatureMapProps->Clamping)
	{
	default:
	case EBakedCurvatureClampMode::None:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapBaker::EClampMode::FullRange;
		break;
	case EBakedCurvatureClampMode::Positive:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapBaker::EClampMode::Positive;
		break;
	case EBakedCurvatureClampMode::Negative:
		CurvatureMapSettings.ClampMode = (int32)FMeshCurvatureMapBaker::EClampMode::Negative;
		break;
	}
	CurvatureMapSettings.BlurRadius = (CurvatureMapProps->bGaussianBlur) ? CurvatureMapProps->BlurRadius : 0.0;

	if (!(CachedCurvatureMapSettings == CurvatureMapSettings))
	{
		CachedCurvatureMapSettings = CurvatureMapSettings;
		ResultState = EOpState::Evaluate;
	}
	return ResultState;
}



UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_MeshProperty()
{
	EOpState ResultState = EOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FMeshPropertyMapSettings MeshPropertyMapSettings;
	MeshPropertyMapSettings.Dimensions = Dimensions;
	switch (Settings->MapType)
	{
		case EBakeMapType::NormalImage:
			MeshPropertyMapSettings.PropertyTypeIndex = (int32)EMeshPropertyBakeType::Normal;
			break;
		case EBakeMapType::FaceNormalImage:
			MeshPropertyMapSettings.PropertyTypeIndex = (int32)EMeshPropertyBakeType::FacetNormal;
			break;
		case EBakeMapType::PositionImage:
			MeshPropertyMapSettings.PropertyTypeIndex = (int32)EMeshPropertyBakeType::Position;
			break;
		case EBakeMapType::MaterialID:
			MeshPropertyMapSettings.PropertyTypeIndex = (int32)EMeshPropertyBakeType::MaterialID;
			break;
		default:
			check(false);		// should not be possible!
	}
	//MeshPropertyMapSettings.BlurRadius = (CurvatureMapProps->bGaussianBlur) ? CurvatureMapProps->BlurRadius : 0.0;

	if (!(CachedMeshPropertyMapSettings == MeshPropertyMapSettings))
	{
		CachedMeshPropertyMapSettings = MeshPropertyMapSettings;
		ResultState = EOpState::Evaluate;
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







UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_Texture2DImage()
{
	EOpState ResultState = EOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FTexture2DImageSettings NewSettings;
	NewSettings.Dimensions = Dimensions;
	NewSettings.UVLayer = 0;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EOpState::Invalid;
	}
	
	if (Texture2DProps->SourceTexture == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
		return EOpState::Invalid;
	}


	{
		FTempTextureAccess TextureAccess(Texture2DProps->SourceTexture);
		CachedTextureImage = MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
		CachedTextureImage->SetDimensions(TextureAccess.GetDimensions());
		if (!TextureAccess.CopyTo(*CachedTextureImage))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("CannotReadTextureWarning", "Cannot read from the source texture"), EToolMessageLevel::UserWarning);
			return EOpState::Invalid;
		}
	}

	if (!(CachedTexture2DImageSettings == NewSettings))
	{
		CachedTexture2DImageSettings = NewSettings;
		ResultState = EOpState::Evaluate;
	}
	return ResultState;
}


UBakeMeshAttributeMapsTool::EOpState UBakeMeshAttributeMapsTool::UpdateResult_MultiTexture()
{
	EOpState ResultState = EOpState::Complete;

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FTexture2DImageSettings NewSettings;
	NewSettings.Dimensions = Dimensions;
	NewSettings.UVLayer = MultiTextureProps->UVLayer;

	const FDynamicMeshUVOverlay* UVOverlay = DetailMesh->Attributes()->GetUVLayer(NewSettings.UVLayer);
	if (UVOverlay == nullptr)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidUVWarning", "The Source Mesh does not have the selected UV layer"), EToolMessageLevel::UserWarning);
		return EOpState::Invalid;
	}

	for (TPair<int32, UTexture2D*>& InputTexture : MultiTextureProps->MaterialIDSourceTextureMap)
	{
		if (InputTexture.Value == nullptr)
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EOpState::Invalid;
		}
	}


	CachedMultiTextures.Reset();

	for ( TPair<int32, UTexture2D*>& InputTexture : MultiTextureProps->MaterialIDSourceTextureMap)
	{
		UTexture2D* Texture = InputTexture.Value;
		if (!ensure(Texture != nullptr))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
			return EOpState::Invalid;
		}

		int32 MaterialID = InputTexture.Key;
		FTempTextureAccess TextureAccess(Texture);
		CachedMultiTextures.Add(MaterialID, MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>());
		CachedMultiTextures[MaterialID]->SetDimensions(TextureAccess.GetDimensions());

		if (!TextureAccess.CopyTo(*CachedMultiTextures[MaterialID]))
		{
			GetToolManager()->DisplayMessage(LOCTEXT("CannotReadTextureWarning", "Cannot read from the source texture"), EToolMessageLevel::UserWarning);
			return EOpState::Invalid;
		}
	}
	if (CachedMultiTextures.Num() == 0)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
		return EOpState::Invalid;
	}

	if (!(CachedTexture2DImageSettings == NewSettings))
	{
		CachedTexture2DImageSettings = NewSettings;
		ResultState = EOpState::Evaluate;
	}
	return ResultState;
}





void UBakeMeshAttributeMapsTool::UpdateVisualization()
{
	DynamicMeshComponent->SetOverrideRenderMaterial(PreviewMaterial);
	switch (Settings->MapType)
	{
	default:
		Settings->Result[0] = nullptr;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		break;
	case EBakeMapType::TangentSpaceNormalMap:
		Settings->Result[0] = CachedNormalMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), CachedNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
		break;
	case EBakeMapType::Occlusion:
		Settings->Result[0] = CachedOcclusionMap;
		Settings->Result[1] = CachedBentNormalMap;
		switch (OcclusionMapProps->Preview)
		{
		case EOcclusionMapPreview::AmbientOcclusion:
			PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
			PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), CachedOcclusionMap);
			PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
			break;
		case EOcclusionMapPreview::BentNormal:
			BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
			BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), CachedOcclusionMap);
			BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyColorMapWhite);
			BentNormalPreviewMaterial->SetTextureParameterValue(TEXT("BentNormalMap"), CachedBentNormalMap);
			DynamicMeshComponent->SetOverrideRenderMaterial(BentNormalPreviewMaterial);
			break;
		}
		break;
	case EBakeMapType::Curvature:
		Settings->Result[0] = CachedCurvatureMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), CachedCurvatureMap);
		break;

	case EBakeMapType::NormalImage:
	case EBakeMapType::FaceNormalImage:
	case EBakeMapType::PositionImage:
	case EBakeMapType::MaterialID:
		Settings->Result[0] = CachedMeshPropertyMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), CachedMeshPropertyMap);
		break;

	case EBakeMapType::Texture2DImage:
	case EBakeMapType::MultiTexture:
		Settings->Result[0] = CachedTexture2DImageMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyColorMapWhite);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), CachedTexture2DImageMap);
		break;
	}
}



void UBakeMeshAttributeMapsTool::UpdateOnModeChange()
{
	SetToolPropertySourceEnabled(NormalMapProps, false);
	SetToolPropertySourceEnabled(OcclusionMapProps, false);
	SetToolPropertySourceEnabled(CurvatureMapProps, false);
	SetToolPropertySourceEnabled(Texture2DProps, false);
	SetToolPropertySourceEnabled(MultiTextureProps, false);

	Settings->Result.Empty();
	Settings->Result.Add(nullptr);
	switch (Settings->MapType)
	{
	case EBakeMapType::TangentSpaceNormalMap:
		SetToolPropertySourceEnabled(NormalMapProps, true);
		break;
	case EBakeMapType::Occlusion:
		SetToolPropertySourceEnabled(OcclusionMapProps, true);
		Settings->Result.Add(nullptr); // Extra slot for BentNormalMap
		break;
	case EBakeMapType::Curvature:
		SetToolPropertySourceEnabled(CurvatureMapProps, true);
		break;
	case EBakeMapType::Texture2DImage:
		SetToolPropertySourceEnabled(Texture2DProps, true);
		break;
	case EBakeMapType::MultiTexture:
		SetToolPropertySourceEnabled(MultiTextureProps, true);
		break;
	}

}


void UBakeMeshAttributeMapsTool::OnMapsUpdated(const TUniquePtr<FMeshMapBaker>& NewResult)
{
	switch (Settings->MapType)
	{
	case EBakeMapType::TangentSpaceNormalMap:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, CachedNormalMapSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0));
		TextureBuilder.Commit(false);
		CachedNormalMap = TextureBuilder.GetTexture2D();
		break;
	}
	case EBakeMapType::Occlusion:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::AmbientOcclusion, CachedOcclusionMapSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0));
		TextureBuilder.Commit(false);
		CachedOcclusionMap = TextureBuilder.GetTexture2D();

		FTexture2DBuilder TextureNormalBuilder;
		TextureNormalBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, CachedOcclusionMapSettings.Dimensions);
		TextureNormalBuilder.Copy(*NewResult->GetBakeResult(1));
		TextureNormalBuilder.Commit(false);
		CachedBentNormalMap = TextureNormalBuilder.GetTexture2D();
		break;
	}
	case EBakeMapType::Curvature:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedCurvatureMapSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0));
		TextureBuilder.Commit(false);
		CachedCurvatureMap = TextureBuilder.GetTexture2D();
		break;
	}
	case EBakeMapType::PositionImage:
	case EBakeMapType::NormalImage:
	case EBakeMapType::FaceNormalImage:
	case EBakeMapType::MaterialID:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedMeshPropertyMapSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0));
		TextureBuilder.Commit(false);
		CachedMeshPropertyMap = TextureBuilder.GetTexture2D();
		break;
	}
	case EBakeMapType::Texture2DImage:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedTexture2DImageSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0), true);
		TextureBuilder.Commit(false);
		CachedTexture2DImageMap = TextureBuilder.GetTexture2D();
		break;
	}
	case EBakeMapType::MultiTexture:
	{
		FTexture2DBuilder TextureBuilder;
		TextureBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, CachedTexture2DImageSettings.Dimensions);
		TextureBuilder.Copy(*NewResult->GetBakeResult(0), true);
		TextureBuilder.Commit(false);
		CachedTexture2DImageMap = TextureBuilder.GetTexture2D();
		break;
	}
	}

	UpdateVisualization();
	GetToolManager()->PostInvalidation();
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
