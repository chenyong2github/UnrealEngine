// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMultiMeshAttributeMapsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Sampling/MeshNormalMapEvaluator.h"
#include "Sampling/MeshOcclusionMapEvaluator.h"
#include "Sampling/MeshCurvatureMapEvaluator.h"
#include "Sampling/MeshPropertyMapEvaluator.h"
#include "Sampling/MeshResampleImageEvaluator.h"

#include "ImageUtils.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetUtils/Texture2DUtil.h"
#include "ModelingObjectsCreationAPI.h"

#include "EngineAnalytics.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargetManager.h"
#include "ModelingToolTargetUtil.h"

// required to pass UStaticMesh asset so we can save at same location
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/Engine/StaticMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeMultiMeshAttributeMapsTool"

/*
* ToolBuilder
*/

const FToolTargetTypeRequirements& UBakeMultiMeshAttributeMapsToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass(),			// FMeshSceneAdapter currently only supports StaticMesh targets
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UBakeMultiMeshAttributeMapsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	const int32 NumTargets = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements());
	return (NumTargets > 1);
}

UInteractiveTool* UBakeMultiMeshAttributeMapsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeMultiMeshAttributeMapsTool* NewTool = NewObject<UBakeMultiMeshAttributeMapsTool>(SceneState.ToolManager);
	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}


TArray<FString> UBakeMultiMeshAttributeMapsToolProperties::GetUVLayerNamesFunc()
{
	return UVLayerNamesList;
}


const TArray<FString>& UBakeMultiMeshAttributeMapsToolProperties::GetMapPreviewNamesFunc()
{
	return MapPreviewNamesList;
}


/**
 * MeshSceneAdapter bake detail sampler for baking N detail meshes to 1 target mesh.
 */
class FMeshBakerMeshSceneSampler : public IMeshBakerDetailSampler
{
public:
	using FDetailTextureMap = TMap<void*, FBakeDetailTexture>;

public:
	/**
	 * Constructor.
	 *
	 * Input FMeshSceneAdapter's spatial evaluation cache is assumed to be built.
	 */
	FMeshBakerMeshSceneSampler(FMeshSceneAdapter* Scene)
		: MeshScene(Scene)
	{
	}

	// Begin IMeshBakerDetailSampler interface
	virtual void ProcessMeshes(TFunctionRef<void(const void*)> ProcessFn) const override
	{
		auto ProcessChildMesh = [ProcessFn](const FActorAdapter*, const FActorChildMesh* ChildMesh)
		{
			if (ChildMesh)
			{
				ProcessFn(ChildMesh->MeshSpatial);
			}
		};
		MeshScene->ProcessActorChildMeshes(ProcessChildMesh);
	}

	virtual int32 GetTriangleCount(const void* Mesh) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->GetTriangleCount();
	}

	virtual void SetColorMap(const void* Mesh, const FBakeDetailTexture& Map) override
	{
		DetailColorMaps[Mesh] = Map;
	}

	virtual void SetNormalMap(const void* Mesh, const FBakeDetailTexture& Map) override
	{
		DetailNormalMaps[Mesh] = Map;
	}

	virtual const FBakeDetailTexture* GetColorMap(const void* Mesh) const override
	{
		return DetailColorMaps.Find(Mesh);
	}
	
	virtual const FBakeDetailTexture* GetNormalMap(const void* Mesh) const override
	{
		return DetailNormalMaps.Find(Mesh);
	}

	virtual bool SupportsIdentityCorrespondence() const override
	{
		return false;
	}

	virtual bool SupportsNearestPointCorrespondence() const override
	{
		return false;
	}

	virtual bool SupportsRaycastCorrespondence() const override
	{
		return true;
	}

	virtual const void* FindNearestHitTriangle(
		const UE::Geometry::FRay3d& Ray,
		double& NearestT,
		int& TriId,
		FVector3d& TriBaryCoords,
		const IMeshSpatial::FQueryOptions& Options = IMeshSpatial::FQueryOptions()) const override
	{
		// TODO: Pass-through max distance to FMeshSceneAdapter query
		FMeshSceneRayHit HitResult;
		const bool bHit = MeshScene->FindNearestRayIntersection(Ray, HitResult);
		const void* HitMesh = nullptr;

		// Use TNumericLimits<float>::Max() for consistency with MeshAABBTree3.
		NearestT = (Options.MaxDistance < TNumericLimits<float>::Max()) ? Options.MaxDistance : TNumericLimits<float>::Max();
		if (bHit && HitResult.RayDistance < Options.MaxDistance)
		{
			TriId = HitResult.HitMeshTriIndex;
			NearestT = HitResult.RayDistance;
			TriBaryCoords = HitResult.HitMeshBaryCoords;
			HitMesh = HitResult.HitMeshSpatialWrapper;
		}
		return HitMesh;
	}

	virtual bool TestAnyHitTriangle(
		const UE::Geometry::FRay3d& Ray,
		const IMeshSpatial::FQueryOptions& Options = IMeshSpatial::FQueryOptions()) const override
	{
		// TODO: Add proper test any hit triangle support (for Occlusion)
		// TODO: Pass through max distance to FMeshSceneAdapter query
		double NearestT = TNumericLimits<double>::Max();
		int TriId = IndexConstants::InvalidID;
		FVector3d TriBaryCoords;
		const bool bHit = FindNearestHitTriangle(Ray, NearestT, TriId, TriBaryCoords, Options) != nullptr;
		return bHit && NearestT < Options.MaxDistance;
	}

	virtual FAxisAlignedBox3d GetBounds() const override
	{
		return MeshScene->GetBoundingBox();
	}

	virtual bool IsTriangle(const void* Mesh, const int TriId) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->IsTriangle(TriId);
	}

	virtual FIndex3i GetTriangle(const void* Mesh, const int TriId) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->GetTriangle(TriId);
	}

	virtual FVector3d GetTriNormal(const void* Mesh, const int TriId) const override
	{
		// TODO
		checkNoEntry();
		return FVector3d::Zero();
	}

	virtual int32 GetMaterialID(const void* Mesh, const int TriId) const override
	{
		// TODO
		checkNoEntry();
		return IndexConstants::InvalidID;
	}

	virtual bool HasNormals(const void* Mesh) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->HasNormals();
	}

	virtual bool HasUVs(const void* Mesh, const int UVLayer = 0) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->HasUVs(UVLayer);
	}

	virtual bool HasTangents(const void* Mesh) const override
	{
		// TODO
		checkNoEntry();
		return false;
	}

	virtual bool HasColors(const void* Mesh) const override
	{
		// TODO
		checkNoEntry();
		return false;
	}

	virtual FVector3d TriBaryInterpolatePoint(
		const void* Mesh,
		const int32 TriId,
		const FVector3d& BaryCoords) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->TriBaryInterpolatePoint(TriId, BaryCoords);
	}

	virtual bool TriBaryInterpolateNormal(
		const void* Mesh,
		const int32 TriId,
		const FVector3d& BaryCoords,
		FVector3f& NormalOut) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->TriBaryInterpolateNormal(TriId, BaryCoords, NormalOut);
	}

	virtual bool TriBaryInterpolateUV(
		const void* Mesh,
		const int32 TriId,
		const FVector3d& BaryCoords,
		const int UVLayer,
		FVector2f& UVOut ) const override
	{
		const IMeshSpatialWrapper* Spatial = static_cast<const IMeshSpatialWrapper*>(Mesh);
		return Spatial->TriBaryInterpolateUV(TriId, BaryCoords, UVLayer, UVOut);
	}

	virtual bool TriBaryInterpolateColor(
		const void* Mesh,
		const int32 TriId,
		const FVector3d& BaryCoords,
		FVector4f& ColorOut) const override
	{
		// TODO
		checkNoEntry();
		return false;
	}

	virtual bool TriBaryInterpolateTangents(
		const void* Mesh,
		const int32 TriId,
		const FVector3d& BaryCoords,
		FVector3d& TangentX,
		FVector3d& TangentY) const override
	{
		// TODO
		checkNoEntry();
		return false;
	}

	virtual void GetCurvature(
		const void* Mesh,
		FMeshVertexCurvatureCache& CurvatureCache) const override
	{
		// TODO
		checkNoEntry();
	}
	// End IMeshBakerDetailSampler interface

	/** Initialize the mesh to color textures map */
	void SetColorMaps(const FDetailTextureMap& Map)
	{
		DetailColorMaps = Map;
	}

	/** Initialize the mesh to normal textures map */
	void SetNormalMaps(const FDetailTextureMap& Map)
	{
		DetailNormalMaps = Map;
	}

protected:
	FMeshSceneAdapter* MeshScene = nullptr;
	FDetailTextureMap DetailColorMaps;
	FDetailTextureMap DetailNormalMaps;
};


/*
 * Operators
 */

class FMultiMeshMapBakerOp : public TGenericDataOperator<FMeshMapBaker>
{
public:
	// General bake settings
	FMeshSceneAdapter* DetailMeshScene = nullptr;
	UE::Geometry::FDynamicMesh3* BaseMesh;
	TSharedPtr<UE::Geometry::FMeshTangentsd, ESPMode::ThreadSafe> BaseMeshTangents;
	TUniquePtr<UE::Geometry::FMeshMapBaker> Baker;
	UBakeMultiMeshAttributeMapsTool::FBakeCacheSettings BakeCacheSettings;

	// Detail bake data
	TArray<TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>>> CachedColorImages;
	UBakeMultiMeshAttributeMapsTool::FTextureImageMap CachedMeshToColorImageMap;

	// Begin TGenericDataOperator interface
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		Baker = MakeUnique<FMeshMapBaker>();
		Baker->CancelF = [Progress]() {
			return Progress && Progress->Cancelled();
		};
		Baker->SetTargetMesh(BaseMesh);
		Baker->SetDimensions(BakeCacheSettings.Dimensions);
		Baker->SetUVLayer(BakeCacheSettings.UVLayer);
		Baker->SetThickness(BakeCacheSettings.Thickness);
		Baker->SetMultisampling(BakeCacheSettings.Multisampling);
		Baker->SetTargetMeshTangents(BaseMeshTangents);
		
		FMeshBakerMeshSceneSampler DetailSampler(DetailMeshScene);
		Baker->SetDetailSampler(&DetailSampler);

		for (const EBakeMapType MapType : ALL_BAKE_MAP_TYPES)
		{
			switch (BakeCacheSettings.BakeMapTypes & MapType)
			{
			case EBakeMapType::TangentSpaceNormal:
			{
				TSharedPtr<FMeshNormalMapEvaluator, ESPMode::ThreadSafe> NormalEval = MakeShared<FMeshNormalMapEvaluator, ESPMode::ThreadSafe>();
				Baker->AddEvaluator(NormalEval);
				break;	
			}
			case EBakeMapType::Texture:
			{
				TSharedPtr<FMeshResampleImageEvaluator, ESPMode::ThreadSafe> TextureEval = MakeShared<FMeshResampleImageEvaluator, ESPMode::ThreadSafe>();
				DetailSampler.SetColorMaps(CachedMeshToColorImageMap);
				Baker->AddEvaluator(TextureEval);
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

void UBakeMultiMeshAttributeMapsTool::Setup()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UBakeMultiMeshAttributeMapsTool::Setup);
	
	Super::Setup();

	// Initialize base mesh
	const UE::Geometry::FTransform3d BaseToWorld = UE::ToolTarget::GetLocalToWorldTransform(Targets[0]);
	PreviewMesh->ProcessMesh([this, BaseToWorld](const FDynamicMesh3& Mesh)
	{
		BaseMesh.Copy(Mesh);
		BaseMeshTangents = MakeShared<FMeshTangentsd, ESPMode::ThreadSafe>(&BaseMesh);
		BaseMeshTangents->CopyTriVertexTangents(Mesh);
		
		// FMeshSceneAdapter operates in world space, so ensure our mesh transformed to world.
		MeshTransforms::ApplyTransform(BaseMesh, BaseToWorld);
		BaseSpatial.SetMesh(&BaseMesh, true);
	});

	// Initialize detail sampler
	const int NumTargets = Targets.Num();
	TArray<UActorComponent*> DetailComponents;
	for (int Idx = 1; Idx < NumTargets; ++Idx)
	{
		UPrimitiveComponent* Component = UE::ToolTarget::GetTargetComponent(Targets[Idx]);
		if (Component)
		{
			DetailComponents.Add(Component);
		}
	}
	DetailMeshScene.AddComponents(DetailComponents);
	DetailMeshScene.Build(FMeshSceneAdapterBuildOptions());
	DetailMeshScene.BuildSpatialEvaluationCache();

	// Setup tool property sets
	Settings = NewObject<UBakeMultiMeshAttributeMapsToolProperties>(this);
	Settings->RestoreProperties(this);
	UpdateUVLayerNames(Settings, BaseMesh);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->MapTypes, [this](int32) { OpState |= EBakeOpState::Evaluate; UpdateOnModeChange(); });
	Settings->WatchProperty(Settings->MapPreview, [this](FString) { UpdateVisualization(); GetToolManager()->PostInvalidation(); });
	Settings->WatchProperty(Settings->Resolution, [this](EBakeTextureResolution) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->SourceFormat, [this](EBakeTextureFormat) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->UVLayer, [this](FString) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->Thickness, [this](float) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->Multisampling, [this](EBakeMultisampling) { OpState |= EBakeOpState::Evaluate; });

	DetailProps = NewObject<UBakeMultiMeshDetailToolProperties>(this);
	DetailProps->RestoreProperties(this);
	AddToolPropertySource(DetailProps);
	SetToolPropertySourceEnabled(DetailProps, true);

	// Pre-populate detail mesh data
	TArray<UTexture2D*> DetailColorTextures;
	for (int Idx = 1; Idx < NumTargets; ++Idx)
	{
		UToolTarget* DetailTarget = Targets[Idx];

		// Hide each of our detail targets since this baker operates solely in world space
		// which will occlude the preview of the target mesh. 
		UE::ToolTarget::HideSourceObject(DetailTarget);
		
		IStaticMeshBackedTarget* DetailStaticMeshTarget = Cast<IStaticMeshBackedTarget>(DetailTarget);
		UStaticMesh* DetailStaticMesh = DetailStaticMeshTarget ? DetailStaticMeshTarget->GetStaticMesh() : nullptr;
		const UPrimitiveComponent* Component = UE::ToolTarget::GetTargetComponent(Targets[Idx]);
		UTexture2D* DetailColorTexture = nullptr;
		ProcessComponentTextures(Component, [&DetailColorTexture](const int MaterialID, const TArray<UTexture*>& Textures)
		{
			// TODO: Support multiple materialIDs per detail mesh
			if (MaterialID == 0)
			{
				const int SelectedTextureIndex = SelectColorTextureToBake(Textures);
				if (SelectedTextureIndex >= 0)
				{
					DetailColorTexture = Cast<UTexture2D>(Textures[SelectedTextureIndex]);
				}
			}
		});

		FBakeMultiMeshDetailProperties DetailMeshProp;
		DetailMeshProp.DetailMesh = DetailStaticMesh;
		DetailMeshProp.DetailColorMap = DetailColorTexture;
		DetailProps->DetailProperties.Add(DetailMeshProp);
		DetailProps->WatchProperty(DetailProps->DetailProperties[Idx-1].DetailColorMap, [this](UTexture2D*) { OpState |= EBakeOpState::Evaluate; });
		DetailProps->WatchProperty(DetailProps->DetailProperties[Idx-1].DetailColorMapUVLayer, [this](int) { OpState |= EBakeOpState::Evaluate; });
	}

	UpdateOnModeChange();

	OpState |= EBakeOpState::Evaluate;

	SetToolDisplayName(LOCTEXT("ToolName", "Bake Textures"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Bake Maps. Select Bake Mesh (LowPoly) first, then select Detail Meshes (HiPoly) to bake. Texture Assets will be created on Accept. "),
		EToolMessageLevel::UserNotification);

	PostSetup();
}




bool UBakeMultiMeshAttributeMapsTool::CanAccept() const
{
	bool bCanAccept = Compute ? Compute->HaveValidResult() && Settings->MapTypes != (int) EBakeMapType::None : false;
	if (bCanAccept)
	{
		// Allow Accept if all non-None types have valid results.
		for (const TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Result : Settings->Result)
		{
			bCanAccept = bCanAccept && Result.Get<1>();
		}
	}
	return bCanAccept;
}


TUniquePtr<UE::Geometry::TGenericDataOperator<FMeshMapBaker>> UBakeMultiMeshAttributeMapsTool::MakeNewOperator()
{
	TUniquePtr<FMultiMeshMapBakerOp> Op = MakeUnique<FMultiMeshMapBakerOp>();
	Op->DetailMeshScene = &DetailMeshScene;
	Op->BaseMesh = &BaseMesh;
	Op->BakeCacheSettings = CachedBakeCacheSettings;

	constexpr EBakeMapType RequiresTangents = EBakeMapType::TangentSpaceNormal | EBakeMapType::BentNormal;
	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & RequiresTangents))
	{
		Op->BaseMeshTangents = BaseMeshTangents;
	}

	if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Texture))
	{
		Op->CachedColorImages = CachedColorImages;
		Op->CachedMeshToColorImageMap = CachedMeshToColorImagesMap;
	}
	return Op;
}


void UBakeMultiMeshAttributeMapsTool::Shutdown(EToolShutdownType ShutdownType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UBakeMultiMeshAttributeMapsTool::Shutdown);
	
	Super::Shutdown(ShutdownType);
	
	Settings->SaveProperties(this);
	DetailProps->SaveProperties(this);

	if (Compute)
	{
		Compute->Shutdown();
	}

	// Restore visibility of detail targets
	const int NumTargets = Targets.Num();
	for (int Idx = 1; Idx < NumTargets; ++Idx)
	{
		UE::ToolTarget::ShowSourceObject(Targets[Idx]);
	}
	
	if (ShutdownType == EToolShutdownType::Accept)
	{
		IStaticMeshBackedTarget* StaticMeshTarget = Cast<IStaticMeshBackedTarget>(Targets[0]);
		UObject* SourceAsset = StaticMeshTarget ? StaticMeshTarget->GetStaticMesh() : nullptr;
		const UPrimitiveComponent* SourceComponent = UE::ToolTarget::GetTargetComponent(Targets[0]);
		CreateTextureAssets(Settings->Result, SourceComponent->GetWorld(), SourceAsset);
	}
}


void UBakeMultiMeshAttributeMapsTool::UpdateResult()
{
	if (OpState == EBakeOpState::Clean)
	{
		return;
	}

	// clear warning (ugh)
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);

	const int32 ImageSize = (int32)Settings->Resolution;
	const FImageDimensions Dimensions(ImageSize, ImageSize);

	FBakeCacheSettings BakeCacheSettings;
	BakeCacheSettings.Dimensions = Dimensions;
	BakeCacheSettings.SourceFormat = Settings->SourceFormat;
	BakeCacheSettings.UVLayer = FCString::Atoi(*Settings->UVLayer);
	BakeCacheSettings.Thickness = Settings->Thickness;
	BakeCacheSettings.Multisampling = (int32)Settings->Multisampling;

	// Record the original map types and process the raw bitfield which may add
	// additional targets.
	BakeCacheSettings.SourceBakeMapTypes = static_cast<EBakeMapType>(Settings->MapTypes);
	BakeCacheSettings.BakeMapTypes = GetMapTypes(Settings->MapTypes);

	// update bake cache settings
	if (!(CachedBakeCacheSettings == BakeCacheSettings))
	{
		CachedBakeCacheSettings = BakeCacheSettings;
		CachedDetailSettings = FBakeMultiMeshDetailSettings();
	}

	// Clear our invalid bitflag to check again for valid inputs.
	OpState &= ~EBakeOpState::Invalid;

	// Update map type settings
	OpState |= UpdateResult_DetailMeshes();

	// Early exit if op input parameters are invalid.
	if ((bool)(OpState & EBakeOpState::Invalid))
	{
		InvalidateResults();
		return;
	}

	// This should be the only point of compute invalidation to
	// minimize synchronization issues.
	InvalidateCompute();
}


EBakeOpState UBakeMultiMeshAttributeMapsTool::UpdateResult_DetailMeshes()
{
	FBakeMultiMeshDetailSettings NewSettings;

	// Iterate through our detail props to build our detail mesh data.
	const int32 NumDetail = DetailProps->DetailProperties.Num();
	CachedColorImages.SetNum(NumDetail);
	CachedColorUVLayers.SetNum(NumDetail);
	TMap<UActorComponent*, int> ActorToDataMap;
	for (int Idx = 0; Idx < NumDetail; ++Idx)
	{
		UActorComponent* ActorComponent = UE::ToolTarget::GetTargetComponent(Targets[Idx+1]);
		ActorToDataMap.Emplace(ActorComponent, Idx);
		
		// Color map data
		if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Texture))
		{
			UTexture2D* ColorMapSourceTexture = DetailProps->DetailProperties[Idx].DetailColorMap;
			const int ColorMapUVLayer = DetailProps->DetailProperties[Idx].DetailColorMapUVLayer;
			if (!ColorMapSourceTexture)
			{
				GetToolManager()->DisplayMessage(LOCTEXT("InvalidTextureWarning", "The Source Texture is not valid"), EToolMessageLevel::UserWarning);
				return EBakeOpState::Invalid;
			}

			TSharedPtr<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe> ColorTextureImage = MakeShared<UE::Geometry::TImageBuilder<FVector4f>, ESPMode::ThreadSafe>();
			if (!UE::AssetUtils::ReadTexture(ColorMapSourceTexture, *ColorTextureImage, bPreferPlatformData))
			{
				GetToolManager()->DisplayMessage(LOCTEXT("CannotReadTextureWarning", "Cannot read from the source texture"), EToolMessageLevel::UserWarning);
				return EBakeOpState::Invalid;
			}
			CachedColorImages[Idx] = ColorTextureImage;
			CachedColorUVLayers[Idx] = ColorMapUVLayer;
		}
	}

	// Iterate through mesh scene adapter and build mesh to data maps.
	CachedMeshToColorImagesMap.Empty();
	auto BuildMeshToDataMaps = [this, ActorToDataMap](const FActorAdapter*, const FActorChildMesh* ChildMesh)
	{
		if (ChildMesh)
		{
			if (const int* DataIndex = ActorToDataMap.Find(ChildMesh->SourceComponent))
			{
				if ((bool)(CachedBakeCacheSettings.BakeMapTypes & EBakeMapType::Texture))
				{
					CachedMeshToColorImagesMap.Emplace(
					ChildMesh->MeshSpatial,
					FTextureImageData(CachedColorImages[*DataIndex].Get(), CachedColorUVLayers[*DataIndex]));
				}
			}
		}
	};
	DetailMeshScene.ProcessActorChildMeshes(BuildMeshToDataMaps);

	// This method will always force a re-evaluation.
	return EBakeOpState::Evaluate;
}


void UBakeMultiMeshAttributeMapsTool::UpdateVisualization()
{
	PreviewMesh->SetOverrideRenderMaterial(PreviewMaterial);

	// Populate Settings->Result from CachedMaps
	for (const TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Map : CachedMaps)
	{
		if (Settings->Result.Contains(Map.Get<0>()))
		{
			Settings->Result[Map.Get<0>()] = Map.Get<1>();
		}
	}

	UpdatePreview(Settings);
}


void UBakeMultiMeshAttributeMapsTool::UpdateOnModeChange()
{
	OnMapTypesUpdated(Settings);
}


void UBakeMultiMeshAttributeMapsTool::InvalidateResults()
{
	for (TTuple<EBakeMapType, TObjectPtr<UTexture2D>>& Result : Settings->Result)
	{
		Result.Get<1>() = nullptr;
	}
}


void UBakeMultiMeshAttributeMapsTool::GatherAnalytics(FBakeAnalytics::FMeshSettings& Data)
{
	if (FEngineAnalytics::IsAvailable())
	{
		Data.NumTargetMeshTris = BaseMesh.TriangleCount();
		Data.NumDetailMesh = 0;
		Data.NumDetailMeshTris = 0;
		DetailMeshScene.ProcessActorChildMeshes([&Data](const FActorAdapter* ActorAdapter, const FActorChildMesh* ChildMesh)
		{
			if (ChildMesh)
			{
				++Data.NumDetailMesh;
				Data.NumDetailMeshTris += ChildMesh->MeshSpatial->GetTriangleCount();
			}
		});
	}
}



#undef LOCTEXT_NAMESPACE
