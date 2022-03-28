// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeRenderCaptureTool.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ToolTargetManager.h"

#include "DynamicMesh/MeshTransforms.h"

#include "ModelingToolTargetUtil.h"

#include "ModelingObjectsCreationAPI.h"

#include "Sampling/MeshGenericWorldPositionBaker.h"
#include "Image/ImageInfilling.h"


using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeRenderCaptureTool"

//
// Implementation details
//

static FName BaseColorTexParamName = FName("BaseColor");
static FName RoughnessTexParamName = FName("Roughness");
static FName MetallicTexParamName = FName("Metallic");
static FName SpecularTexParamName = FName("Specular");
static FName EmissiveTexParamName = FName("Emissive");
static FName NormalTexParamName = FName("NormalMap");
static FName PackedMRSTexParamName = FName("PackedMRS");

class FBakeRenderCaptureOptions
{
public:
	enum class ETextureSizePolicy : uint8
	{
		TextureSize = 0,
		TexelDensity = 1
	};

	/**
	 * Input options to Actor Approximation process
	 */
	struct FOptions
	{
		//
		// Material approximation settings
		//
		int32 RenderCaptureImageSize = 1024;

		// render capture parameters
		double FieldOfViewDegrees = 45.0;
		double NearPlaneDist = 1.0;

		//
		// Material output settings
		//

		// A new MIC derived from this material will be created and assigned to the generated mesh
		UMaterialInterface* BakeMaterial = nullptr;		// if null, will use /MeshModelingToolsetExp/Materials/FullMaterialBakePreviewMaterial_PackedMRS instead
		bool bBakeBaseColor = true;
		bool bBakeRoughness = true;
		bool bBakeMetallic = true;
		bool bBakeSpecular = true;
		bool bBakeEmissive = true;
		bool bBakeNormalMap = true;
		
		bool bUsePackedMRS = true;

		// output texture options
		int32 TextureImageSize = 1024;

		// supersampling parameter
		int32 AntiAliasMultiSampling = 0;

		//
		// Mesh settings
		//

		//  Which UV layer of the Target mesh (the one we're baking to) should be used
		int32 TargetUVLayer = 0;
	};

	/**
	 * Construct an FOptions from the provided FMeshApproximationSettings.
	 */
	static FOptions ConstructOptions(
		const UBakeRenderCaptureToolProperties& UseSettings,
		const UBakeRenderCaptureInputToolProperties& InputMeshSettings)
	{
		//
		// Construct options for ApproximateActors operation
		//
		FOptions Options;

		Options.TargetUVLayer = InputMeshSettings.TargetUVLayerNamesList.IndexOfByKey(InputMeshSettings.TargetUVLayer);
		
		Options.RenderCaptureImageSize = (UseSettings.RenderCaptureResolution == 0) ? Options.TextureImageSize : UseSettings.RenderCaptureResolution;
		Options.FieldOfViewDegrees = UseSettings.CaptureFieldOfView;
		Options.NearPlaneDist = UseSettings.NearPlaneDist;
		
		//Options.bBakeBaseColor // This is always true
		Options.bBakeRoughness = UseSettings.MaterialSettings.bRoughnessMap;
		Options.bBakeMetallic = UseSettings.MaterialSettings.bMetallicMap;
		Options.bBakeSpecular = UseSettings.MaterialSettings.bSpecularMap;
		Options.bBakeEmissive = UseSettings.MaterialSettings.bEmissiveMap;
		Options.bBakeNormalMap = UseSettings.MaterialSettings.bNormalMap;
		Options.bUsePackedMRS = UseSettings.MaterialSettings.bPackedMRSMap;

		Options.TextureImageSize = UseSettings.MaterialSettings.TextureSize;
		Options.AntiAliasMultiSampling = FMath::Max(1, UseSettings.MultiSamplingAA);

		return Options;
	}
};

static TUniquePtr<FSceneCapturePhotoSet> CapturePhotoSet(
	const TArray<TObjectPtr<AActor>>& Actors,
	const FBakeRenderCaptureOptions::FOptions& Options
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CapturePhotoSet);

	double FieldOfView = Options.FieldOfViewDegrees;
	double NearPlaneDist = Options.NearPlaneDist;

	FImageDimensions CaptureDimensions(Options.RenderCaptureImageSize, Options.RenderCaptureImageSize);

	TUniquePtr<FSceneCapturePhotoSet> SceneCapture = MakeUnique<FSceneCapturePhotoSet>();

	SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::BaseColor, Options.bBakeBaseColor);
	SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::WorldNormal, Options.bBakeNormalMap);
	SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Emissive, Options.bBakeEmissive);

	bool bMetallic = Options.bBakeMetallic;
	bool bRoughness = Options.bBakeRoughness;
	bool bSpecular  = Options.bBakeSpecular;
	// if (Options.bUsePackedMRS && (bMetallic || bRoughness || bSpecular ) )
	if (Options.bUsePackedMRS)
	{
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::CombinedMRS, true);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Roughness, false);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Metallic, false);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Specular, false);
	}
	else
	{
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::CombinedMRS, false);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Roughness, bRoughness);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Metallic, bMetallic);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Specular, bSpecular);
	}

	SceneCapture->SetCaptureSceneActors(Actors[0]->GetWorld(), Actors);

	// SceneCapture->SetEnableWriteDebugImages(true);

	SceneCapture->AddStandardExteriorCapturesFromBoundingBox(
		CaptureDimensions, FieldOfView, NearPlaneDist,
		true, true, true);
	
	return SceneCapture;
}

static void ImageBuildersFromPhotoSet(
	FSceneCapturePhotoSet* SceneCapture,
	const FBakeRenderCaptureOptions::FOptions& Options, 
	const FDynamicMesh3* WorldTargetMesh,
	const FMeshTangentsd* MeshTangents,
	TUniquePtr<FBakeRenderCaptureResultsBuilder>& Results)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet);

	int32 UVLayer = Options.TargetUVLayer;
	int32 Supersample = FMath::Max(1, Options.AntiAliasMultiSampling);
	if ( (Options.TextureImageSize * Supersample) > 16384)
	{
		UE_LOG(LogGeometry, Warning, TEXT("Ignoring requested supersampling rate %d because it would require image buffers with resolution %d, please try lower value."), Supersample, Options.TextureImageSize * Supersample);
		Supersample = 1;
	}

	FImageDimensions OutputDimensions(Options.TextureImageSize*Supersample, Options.TextureImageSize*Supersample);

	//FScopedSlowTask Progress(8.f, LOCTEXT("BakingTextures", "Baking Textures..."));
	//Progress.MakeDialog(true);
	//Progress.EnterProgressFrame(1.f, LOCTEXT("BakingSetup", "Setup..."));

	FDynamicMeshAABBTree3 Spatial(WorldTargetMesh, true);

	FMeshImageBakingCache TempBakeCache;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_MakeCache);
		TempBakeCache.SetDetailMesh(WorldTargetMesh, &Spatial);
		TempBakeCache.SetBakeTargetMesh(WorldTargetMesh);
		TempBakeCache.SetDimensions(OutputDimensions);
		TempBakeCache.SetUVLayer(UVLayer);
		TempBakeCache.SetThickness(0.1);
		TempBakeCache.SetCorrespondenceStrategy(FMeshImageBakingCache::ECorrespondenceStrategy::Identity);
		TempBakeCache.ValidateCache();
	}

	//Progress.EnterProgressFrame(1.f, LOCTEXT("BakingBaseColor", "Baking Base Color..."));

	FAxisAlignedBox3d TargetBounds = WorldTargetMesh->GetBounds();
	double RayOffsetHackDist = (double)(100.0f * FMathf::ZeroTolerance * TargetBounds.MinDim() );

	auto VisibilityFunction = [&Spatial, RayOffsetHackDist](const FVector3d& SurfPos, const FVector3d& ImagePosWorld)
	{
		FVector3d RayDir = ImagePosWorld - SurfPos;
		double Dist = Normalize(RayDir);
		FVector3d RayOrigin = SurfPos + RayOffsetHackDist * RayDir;
		int32 HitTID = Spatial.FindNearestHitTriangle(FRay3d(RayOrigin, RayDir), IMeshSpatial::FQueryOptions(Dist));
		return (HitTID == IndexConstants::InvalidID);
	};

	FSceneCapturePhotoSet::FSceneSample DefaultSample;
	FVector4f InvalidColor(0, -1, 0, 1);
	DefaultSample.BaseColor = FVector3f(InvalidColor.X, InvalidColor.Y, InvalidColor.Z);

	FMeshGenericWorldPositionColorBaker BaseColorBaker;
	BaseColorBaker.SetCache(&TempBakeCache);
	BaseColorBaker.ColorSampleFunction = [&](FVector3d Position, FVector3d Normal) {
		FSceneCapturePhotoSet::FSceneSample Sample = DefaultSample;
		SceneCapture->ComputeSample(FRenderCaptureTypeFlags::BaseColor(),
			Position, Normal, VisibilityFunction, Sample);
		return Sample.GetValue4f(ERenderCaptureType::BaseColor);
	};
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_BakeColor);
		BaseColorBaker.Bake();
	}

	// find "hole" pixels
	TArray<FVector2i> MissingPixels;
	Results->ColorImage = BaseColorBaker.TakeResult();
	TMarchingPixelInfill<FVector4f> Infill;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_ComputeInfill);
		TempBakeCache.FindSamplingHoles([&](const FVector2i& Coords)
		{
			return Results->ColorImage->GetPixel(Coords) == InvalidColor;
		}, MissingPixels);

		// solve infill for the holes while also caching infill information
		Infill.ComputeInfill(*Results->ColorImage, MissingPixels, InvalidColor,
			[](FVector4f SumValue, int32 Count) {
			float InvSum = (Count == 0) ? 1.0f : (1.0f / Count);
			return FVector4f(SumValue.X * InvSum, SumValue.Y * InvSum, SumValue.Z * InvSum, 1.0f);
		});
	}

	// downsample the image if necessary
	if (Supersample > 1)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_Downsample);
		TImageBuilder<FVector4f> Downsampled = Results->ColorImage->FastDownsample(Supersample, FVector4f::Zero(), [](FVector4f V, int N) { return V / (float)N; });
		*Results->ColorImage = MoveTemp(Downsampled);
	}

	// this lambda is used to process the per-channel images. It does the bake, applies infill, and downsamples if necessary
	auto ProcessChannelFunc = [&](ERenderCaptureType CaptureType)
	{
		FVector4f DefaultValue(0, 0, 0, 0);
		FMeshGenericWorldPositionColorBaker ChannelBaker;
		ChannelBaker.SetCache(&TempBakeCache);
		ChannelBaker.ColorSampleFunction = [&](FVector3d Position, FVector3d Normal) {
			FSceneCapturePhotoSet::FSceneSample Sample = DefaultSample;
			SceneCapture->ComputeSample(FRenderCaptureTypeFlags::Single(CaptureType), Position, Normal, VisibilityFunction, Sample);
			return Sample.GetValue4f(CaptureType);
		};
		ChannelBaker.Bake();
		TUniquePtr<TImageBuilder<FVector4f>> Image = ChannelBaker.TakeResult();

		Infill.ApplyInfill(*Image,
			[](FVector4f SumValue, int32 Count) {
			float InvSum = (Count == 0) ? 1.0f : (1.0f / Count);
			return FVector4f(SumValue.X * InvSum, SumValue.Y * InvSum, SumValue.Z * InvSum, 1.0f);
		});

		if (Supersample > 1)
		{
			TImageBuilder<FVector4f> Downsampled = Image->FastDownsample(Supersample, FVector4f::Zero(), [](FVector4f V, int N) { return V / (float)N; });
			*Image = MoveTemp(Downsampled);
		}

		return MoveTemp(Image);
	};

	bool bMetallic = Options.bBakeMetallic;
	bool bRoughness = Options.bBakeRoughness;
	bool bSpecular = Options.bBakeSpecular;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_OtherChannels);

		// if (Options.bUsePackedMRS && (bMetallic || bRoughness || bSpecular))
		if (Options.bUsePackedMRS)
		{
			Results->PackedMRSImage = ProcessChannelFunc(ERenderCaptureType::CombinedMRS);
		}
		else
		{
			if (bRoughness)
			{
				Results->RoughnessImage = ProcessChannelFunc(ERenderCaptureType::Roughness);
			}
			if (bMetallic)
			{
				Results->MetallicImage = ProcessChannelFunc(ERenderCaptureType::Metallic);
			}
			if (bSpecular)
			{
				Results->SpecularImage = ProcessChannelFunc(ERenderCaptureType::Specular);
			}
		}

		if (Options.bBakeEmissive)
		{
			Results->EmissiveImage = ProcessChannelFunc(ERenderCaptureType::Emissive);
		}
	}


	//Progress.EnterProgressFrame(1.f, LOCTEXT("BakingNormals", "Baking Normals..."));

	if (Options.bBakeNormalMap)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Textures_NormalMapBake);

		// no infill on normal map for now, doesn't make sense to do after mapping to tangent space!
		//  (should we build baked normal map in world space, and then resample to tangent space??)
		FVector4f DefaultNormalValue(0, 0, 1, 1);
		FMeshGenericWorldPositionNormalBaker NormalMapBaker;
		NormalMapBaker.SetCache(&TempBakeCache);
		NormalMapBaker.BaseMeshTangents = MeshTangents;
		NormalMapBaker.NormalSampleFunction = [&](FVector3d Position, FVector3d Normal) {
			FSceneCapturePhotoSet::FSceneSample Sample = DefaultSample;
			SceneCapture->ComputeSample(FRenderCaptureTypeFlags::WorldNormal(),
				Position, Normal, VisibilityFunction, Sample);
			FVector3f NormalColor = Sample.WorldNormal;
			float x = (NormalColor.X - 0.5f) * 2.0f;
			float y = (NormalColor.Y - 0.5f) * 2.0f;
			float z = (NormalColor.Z - 0.5f) * 2.0f;
			return FVector3f(x, y, z);
		};

		NormalMapBaker.Bake();
		Results->NormalImage = NormalMapBaker.TakeResult();

		if (Supersample > 1)
		{
			TImageBuilder<FVector3f> Downsampled = Results->NormalImage->FastDownsample(Supersample, FVector3f::Zero(), [](FVector3f V, int N) { return V / (float)N; });
			*Results->NormalImage = MoveTemp(Downsampled);
		}
	}

	// build textures
	//Progress.EnterProgressFrame(1.f, LOCTEXT("BuildingTextures", "Building Textures..."));
}

//
// Tool Operators
//


class FRenderCaptureMapBakerOp : public TGenericDataOperator<FBakeRenderCaptureResultsBuilder>
{
public:
	TArray<TObjectPtr<AActor>>* Actors;
	UE::Geometry::FDynamicMesh3* BaseMesh = nullptr;
	TSharedPtr<UE::Geometry::FMeshTangentsd, ESPMode::ThreadSafe> BaseMeshTangents;
	FBakeRenderCaptureOptions::FOptions Options;
	TObjectPtr<UBakeRenderCaptureInputToolProperties> InputMeshSettings;
	FSceneCapturePhotoSet* SceneCapture;
	
	// Begin TGenericDataOperator interface
	// @Incomplete Use the Progress thing
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		check(Actors != nullptr);
		check(SceneCapture != nullptr);

		// bake textures onto the target mesh by projecting/sampling the set of captured photos
		ImageBuildersFromPhotoSet(SceneCapture, Options, BaseMesh, BaseMeshTangents.Get(), Result);
	}
	// End TGenericDataOperator interface
};


//
// Tool Builder
//



const FToolTargetTypeRequirements& UBakeRenderCaptureToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass(),			// FMeshSceneAdapter currently only supports StaticMesh targets
		UMaterialProvider::StaticClass()
		});
	return TypeRequirements;
}

bool UBakeRenderCaptureToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	const int32 NumTargets = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements());
	return (NumTargets > 1);
}

UMultiSelectionMeshEditingTool* UBakeRenderCaptureToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UBakeRenderCaptureTool>(SceneState.ToolManager);
}



//
// Tool
//



void UBakeRenderCaptureTool::Setup()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UBakeRenderCaptureTool::Setup);

	Super::Setup();

	InitializePreviewMaterials();

	// Initialize base mesh
	const FTransformSRT3d BaseToWorld = UE::ToolTarget::GetLocalToWorldTransform(Targets[0]);
	PreviewMesh->ProcessMesh([this, BaseToWorld](const FDynamicMesh3& Mesh)
	{
		TargetMesh.Copy(Mesh);
		TargetMeshTangents = MakeShared<FMeshTangentsd, ESPMode::ThreadSafe>(&TargetMesh);
		TargetMeshTangents->CopyTriVertexTangents(Mesh);
		
		// FMeshSceneAdapter operates in world space, so ensure our mesh transformed to world.
		MeshTransforms::ApplyTransform(TargetMesh, BaseToWorld);
		TargetSpatial.SetMesh(&TargetMesh, true);
	});

	// Initialize actors
	const int NumTargets = Targets.Num();
	Actors.Empty(NumTargets - 1);
	for (int Idx = 1; Idx < NumTargets; ++Idx)
	{
		if (AActor* Actor = UE::ToolTarget::GetTargetActor(Targets[Idx]))
		{
			Actors.Add(Actor);
		}
	}

	UToolTarget* Target = Targets[0];

	// Setup tool property sets

	Settings = NewObject<UBakeRenderCaptureToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->MapPreview = BaseColorTexParamName.ToString(); // We always bake the base color
	Settings->WatchProperty(Settings->MapPreview, [this](FString) { UpdateVisualization(); GetToolManager()->PostInvalidation(); });
	Settings->WatchProperty(Settings->MultiSamplingAA, [this](int32) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->RenderCaptureResolution, [this](int32) {  OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->MaterialSettings, [this](FMaterialProxySettingsRC) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->CaptureFieldOfView, [this](float) { OpState |= EBakeOpState::Evaluate; });
	Settings->WatchProperty(Settings->NearPlaneDist, [this](float) { OpState |= EBakeOpState::Evaluate; });
	
	InputMeshSettings = NewObject<UBakeRenderCaptureInputToolProperties>(this);
	InputMeshSettings->RestoreProperties(this);
	AddToolPropertySource(InputMeshSettings);
	InputMeshSettings->TargetStaticMesh = GetStaticMeshTarget(Target);
	UpdateUVLayerNames(InputMeshSettings->TargetUVLayer, InputMeshSettings->TargetUVLayerNamesList, TargetMesh);
	InputMeshSettings->WatchProperty(InputMeshSettings->TargetUVLayer, [this](FString) { OpState |= EBakeOpState::Evaluate; });
	
	{
		FBakeRenderCaptureOptions::FOptions Options = FBakeRenderCaptureOptions::ConstructOptions(*Settings, *InputMeshSettings);
		Settings->MapPreviewNamesList.Add(NormalTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(BaseColorTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(RoughnessTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(MetallicTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(SpecularTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(EmissiveTexParamName.ToString());
		Settings->MapPreviewNamesList.Add(PackedMRSTexParamName.ToString());
	}

	ResultSettings = NewObject<UBakeRenderCaptureResults>(this);
	ResultSettings->RestoreProperties(this);
	AddToolPropertySource(ResultSettings);
	SetToolPropertySourceEnabled(ResultSettings, true);

	// Hide the render capture meshes since this baker operates solely in world space which will occlude the preview of
	// the target mesh.
	for (int Idx = 1; Idx < NumTargets; ++Idx)
	{
		UE::ToolTarget::HideSourceObject(Targets[Idx]);
	}

	OpState |= EBakeOpState::Evaluate;

	SetToolDisplayName(LOCTEXT("ToolName", "Bake Render Capture"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Bake Render Capture. Select Bake Mesh (LowPoly) first, then select Detail Meshes (HiPoly) to bake. Assets will be created on Accept."),
		EToolMessageLevel::UserNotification);

	PostSetup();
}


void UBakeRenderCaptureTool::OnShutdown(EToolShutdownType ShutdownType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UBakeRenderCaptureTool::Shutdown);
	
	Super::OnShutdown(ShutdownType);
	
	Settings->SaveProperties(this);
	InputMeshSettings->SaveProperties(this);

	if (ComputeRC)
	{
		ComputeRC->Shutdown();
	}

	// Restore visibility of source meshes
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
		CreateTextureAssetsRC(SourceComponent->GetWorld(), SourceAsset);
	}

	// Clear actors on shutdown so that their lifetime is not tied to the lifetime of the tool
	Actors.Empty();
}

void UBakeRenderCaptureTool::CreateTextureAssetsRC(UWorld* SourceWorld, UObject* SourceAsset)
{
	bool bCreatedAssetOK = true;
	const FString BaseName = UE::ToolTarget::GetTargetActor(Targets[0])->GetActorNameOrLabel();

	auto CreateTextureAsset = [this, &bCreatedAssetOK, &SourceWorld, &SourceAsset](const FString& TexName, FTexture2DBuilder::ETextureType Type, TObjectPtr<UTexture2D> Tex)
	{
		// See :DeferredPopulateSourceData
		FTexture2DBuilder::CopyPlatformDataToSourceData(Tex, Type);

		// TODO The original implementation in ApproximateActors also did the following, see WriteTextureLambda in ApproximateActorsImpl.cpp
		//if (Type == FTexture2DBuilder::ETextureType::Roughness
		//	|| Type == FTexture2DBuilder::ETextureType::Metallic
		//	|| Type == FTexture2DBuilder::ETextureType::Specular)
		//{
		//	UE::AssetUtils::ConvertToSingleChannel(Texture);
		//}

		bCreatedAssetOK = bCreatedAssetOK &&
			UE::Modeling::CreateTextureObject(
				GetToolManager(),
				FCreateTextureObjectParams{ 0, SourceWorld, SourceAsset, TexName, Tex }).IsOK();
	};

	if (ResultSettings->BaseColorMap != nullptr)
	{
		const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *BaseColorTexParamName.ToString());
		CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::Color, ResultSettings->BaseColorMap);
	}

	if (Settings->MaterialSettings.bNormalMap && ResultSettings->NormalMap != nullptr)
	{
		const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *NormalTexParamName.ToString()); 
		// Use ColorLinear because the baked NormalMap is in world space
		CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::ColorLinear, ResultSettings->NormalMap);
	}

	if (Settings->MaterialSettings.bEmissiveMap && ResultSettings->EmissiveMap != nullptr)
	{
		const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *EmissiveTexParamName.ToString());
		CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::EmissiveHDR, ResultSettings->EmissiveMap);
	}

	if (Settings->MaterialSettings.bPackedMRSMap && ResultSettings->PackedMRSMap != nullptr)
	{
		const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *PackedMRSTexParamName.ToString());
		CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::ColorLinear, ResultSettings->PackedMRSMap);
	}
	else
	{
		if (Settings->MaterialSettings.bMetallicMap && ResultSettings->MetallicMap != nullptr)
		{
			const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *MetallicTexParamName.ToString());
			CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::Metallic, ResultSettings->MetallicMap);
		}

		if (Settings->MaterialSettings.bRoughnessMap && ResultSettings->RoughnessMap != nullptr)
		{
			const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *RoughnessTexParamName.ToString());
			CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::Roughness, ResultSettings->RoughnessMap);
		}

		if (Settings->MaterialSettings.bSpecularMap && ResultSettings->SpecularMap != nullptr)
		{
			const FString TexName = FString::Printf(TEXT("%s_%s"), *BaseName, *SpecularTexParamName.ToString());
			CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::Specular, ResultSettings->SpecularMap);
		}
	}

	ensure(bCreatedAssetOK);

	//RecordAnalytics(BakeAnalytics, GetAnalyticsEventName());
}


void UBakeRenderCaptureTool::OnTick(float DeltaTime)
{
	if (ComputeRC)
	{
		ComputeRC->Tick(DeltaTime);

		const float ElapsedComputeTime = ComputeRC->GetElapsedComputeTime();
		if (!CanAccept() && ElapsedComputeTime > SecondsBeforeWorkingMaterial)
		{
			UMaterialInstanceDynamic* ProgressMaterial =
				static_cast<bool>(OpState & EBakeOpState::Invalid) ? ErrorPreviewMaterial : WorkingPreviewMaterial;
			PreviewMesh->SetOverrideRenderMaterial(ProgressMaterial);
		}
	}
}


bool UBakeRenderCaptureTool::CanAccept() const
{
	if ((OpState & EBakeOpState::Invalid) == EBakeOpState::Invalid)
	{
		return false;
	}

	if (ResultSettings->BaseColorMap == nullptr)
	{
		return false;
	}
	if (Settings->MaterialSettings.bNormalMap && ResultSettings->NormalMap == nullptr)
	{
		return false;
	}
	if (Settings->MaterialSettings.bEmissiveMap && ResultSettings->EmissiveMap == nullptr)
	{
		return false;
	}
	if (Settings->MaterialSettings.bPackedMRSMap)
	{
		if (ResultSettings->PackedMRSMap == nullptr)
		{
			return false;
		}
	}
	else
	{
		if (Settings->MaterialSettings.bMetallicMap && ResultSettings->MetallicMap == nullptr)
		{
			return false;
		}
		if (Settings->MaterialSettings.bRoughnessMap && ResultSettings->RoughnessMap == nullptr)
		{
			return false;
		}
		if (Settings->MaterialSettings.bSpecularMap && ResultSettings->SpecularMap == nullptr)
		{
			return false;
		}
	}

	return true;
}


TUniquePtr<UE::Geometry::TGenericDataOperator<FBakeRenderCaptureResultsBuilder>> UBakeRenderCaptureTool::FComputeFactory::MakeNewOperator()
{
	TUniquePtr<FRenderCaptureMapBakerOp> Op = MakeUnique<FRenderCaptureMapBakerOp>();
	Op->Actors = &Tool->Actors;
	Op->BaseMesh = &Tool->TargetMesh;
	Op->BaseMeshTangents = Tool->TargetMeshTangents;
	Op->Options = FBakeRenderCaptureOptions::ConstructOptions(*Tool->Settings, *Tool->InputMeshSettings);
	Op->SceneCapture = Tool->SceneCapture.Get();
	return Op;
}


void UBakeRenderCaptureTool::OnMapsUpdatedRC(const TUniquePtr<FBakeRenderCaptureResultsBuilder>& NewResult)
{
	FBakeRenderCaptureOptions::FOptions Options = FBakeRenderCaptureOptions::ConstructOptions(*Settings, *InputMeshSettings);
	bool bMetallic = Options.bBakeMetallic;
	bool bRoughness = Options.bBakeRoughness;
	bool bSpecular = Options.bBakeSpecular;

	check(IsInGameThread());

	// We do this to defer work I guess, it was like this in the original ApproximateActors implementation :DeferredPopulateSourceData 
	constexpr bool bPopulateSourceData = false;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(BakeRenderCaptureTool_Textures_BuildTextures);

		if (Options.bBakeBaseColor && NewResult->ColorImage.IsValid())
		{
			ResultSettings->BaseColorMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->ColorImage, FTexture2DBuilder::ETextureType::Color, true, bPopulateSourceData);
		}
		if (Options.bBakeEmissive && NewResult->EmissiveImage.IsValid())
		{
			ResultSettings->EmissiveMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->EmissiveImage, FTexture2DBuilder::ETextureType::EmissiveHDR, false, bPopulateSourceData);
			ResultSettings->EmissiveMap->CompressionSettings = TC_HDR_Compressed;
		}
		if (Options.bBakeNormalMap && NewResult->NormalImage.IsValid())
		{
			// Use ColorLinear because the baked NormalMap is in world space
			ResultSettings->NormalMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->NormalImage, FTexture2DBuilder::ETextureType::ColorLinear, false, bPopulateSourceData);
		}

		// if (Options.bUsePackedMRS && (bRoughness || bMetallic || bSpecular) && NewResult->PackedMRSImage.IsValid())
		if (Options.bUsePackedMRS && NewResult->PackedMRSImage.IsValid())
		{
			ResultSettings->PackedMRSMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->PackedMRSImage, FTexture2DBuilder::ETextureType::ColorLinear, false, bPopulateSourceData);
		}
		else
		{ 
			if (bRoughness && NewResult->RoughnessImage.IsValid())
			{
				ResultSettings->RoughnessMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->RoughnessImage, FTexture2DBuilder::ETextureType::Roughness, false, bPopulateSourceData);
			}
			if (bMetallic && NewResult->MetallicImage.IsValid())
			{
				ResultSettings->MetallicMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->MetallicImage, FTexture2DBuilder::ETextureType::Metallic, false, bPopulateSourceData);
			}
			if (bSpecular && NewResult->SpecularImage.IsValid())
			{
				ResultSettings->SpecularMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->SpecularImage, FTexture2DBuilder::ETextureType::Specular, false, bPopulateSourceData);
			}
		}
	}

	//GatherAnalytics(*NewResult, CachedBakeSettings, BakeAnalytics);
	UpdateVisualization();
	GetToolManager()->PostInvalidation();
}


void UBakeRenderCaptureTool::InitializePreviewMaterials()
{
	// EmptyColorMapWhite, EmptyColorMapBlack and EmptyNormalMap are defined in the base tool

	{
		FTexture2DBuilder Builder;
		Builder.Initialize(FTexture2DBuilder::ETextureType::EmissiveHDR, FImageDimensions(16, 16));
		Builder.Commit(false);
		EmptyEmissiveMap = Builder.GetTexture2D();
	}

	{
		FTexture2DBuilder Builder;
		Builder.Initialize(FTexture2DBuilder::ETextureType::ColorLinear, FImageDimensions(16, 16));
		Builder.Clear(FColor(0,0,0));
		Builder.Commit(false);
		EmptyPackedMRSMap = Builder.GetTexture2D();
	}

	{
		FTexture2DBuilder Builder;
		Builder.Initialize(FTexture2DBuilder::ETextureType::Roughness, FImageDimensions(16, 16));
		Builder.Commit(false);
		EmptyRoughnessMap = Builder.GetTexture2D();
	}

	{
		FTexture2DBuilder Builder;
		Builder.Initialize(FTexture2DBuilder::ETextureType::Metallic, FImageDimensions(16, 16));
		Builder.Commit(false);
		EmptyMetallicMap = Builder.GetTexture2D();
	}

	{
		FTexture2DBuilder Builder;
		Builder.Initialize(FTexture2DBuilder::ETextureType::Specular, FImageDimensions(16, 16));
		Builder.Commit(false);
		EmptySpecularMap = Builder.GetTexture2D();
	}
	
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/BakeRenderCapturePreviewMaterial"));
		check(Material);
		if (Material != nullptr)
		{
			PreviewMaterialRC = UMaterialInstanceDynamic::Create(Material, GetToolManager());
			PreviewMaterialRC->SetTextureParameterValue(TEXT("BaseColor"), EmptyColorMapWhite);
			PreviewMaterialRC->SetTextureParameterValue(TEXT("Roughness"), EmptyRoughnessMap);
			PreviewMaterialRC->SetTextureParameterValue(TEXT("Metallic"), EmptyMetallicMap);
			PreviewMaterialRC->SetTextureParameterValue(TEXT("Specular"), EmptySpecularMap);
			PreviewMaterialRC->SetTextureParameterValue(TEXT("Emissive"), EmptyEmissiveMap);
			PreviewMaterialRC->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		}
	}
	
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/FullMaterialBakePreviewMaterial_PackedMRS"));
		check(Material);
		if (Material != nullptr)
		{
			PreviewMaterialPackedRC = UMaterialInstanceDynamic::Create(Material, GetToolManager());
			PreviewMaterialPackedRC->SetTextureParameterValue(TEXT("BaseColor"), EmptyColorMapWhite);
			PreviewMaterialPackedRC->SetTextureParameterValue(TEXT("PackedMRS"), EmptyPackedMRSMap);
			PreviewMaterialPackedRC->SetTextureParameterValue(TEXT("Emissive"), EmptyEmissiveMap);
			PreviewMaterialPackedRC->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		}
	}
}



void UBakeRenderCaptureTool::InvalidateComputeRC()
{
	if (!ComputeRC)
	{
		// Initialize background compute
		ComputeRC = MakeUnique<TGenericDataBackgroundCompute<FBakeRenderCaptureResultsBuilder>>();
		ComputeFactory.Tool = this;
		ComputeRC->Setup(&ComputeFactory);
		ComputeRC->OnResultUpdated.AddLambda([this](const TUniquePtr<FBakeRenderCaptureResultsBuilder>& NewResult) { OnMapsUpdatedRC(NewResult); });
	}
	ComputeRC->InvalidateResult();
	OpState = EBakeOpState::Clean;
}


void UBakeRenderCaptureTool::UpdateResult()
{
	if (OpState == EBakeOpState::Clean)
	{
		// Evaluation already launched/complete. Note that the ComputeRC background compute updates ResultSettings when
		// they are available by calling OnMapsUpdatedRC in its OnResultUpdated delegate.
		return;
	}

	//
	// create a set of spatially located render captures of the scene ("photo set").
	// @Refactor SceneCapture currently must happen on the game thread, we should try to fix that
	//
	FBakeRenderCaptureOptions::FOptions Options = FBakeRenderCaptureOptions::ConstructOptions(*Settings, *InputMeshSettings);

	for (int Idx = 1; Idx < Targets.Num(); ++Idx)
	{
		UE::ToolTarget::ShowSourceObject(Targets[Idx]);
	}

	SceneCapture.Reset();
	SceneCapture = CapturePhotoSet(Actors, Options);

	for (int Idx = 1; Idx < Targets.Num(); ++Idx)
	{
		UE::ToolTarget::HideSourceObject(Targets[Idx]);
	}

	// @Incomplete Clear our invalid bitflag to check for valid inputs.
	//OpState &= ~EBakeOpState::Invalid;
	//OpState |= CheckValidInputs();
	//if (static_cast<bool>(OpState & EBakeOpState::Invalid))
	//{
	//	// Early exit if op input parameters are invalid.
	//	return;
	//}
	
	InvalidateResults();

	// This should be the only point of compute invalidation to minimize synchronization issues.
	InvalidateComputeRC();
}


void UBakeRenderCaptureTool::UpdateVisualization()
{
	if (Settings->MapPreview.IsEmpty())
	{
		return;
	}

	FBakeRenderCaptureOptions::FOptions Options = FBakeRenderCaptureOptions::ConstructOptions(*Settings, *InputMeshSettings);
	
	// bool bPackedMRS = Options.bUsePackedMRS && (Options.bBakeRoughness || Options.bBakeMetallic || Options.bBakeSpecular);
	bool bPackedMRS = Options.bUsePackedMRS;

	if (bPackedMRS)
	{
		TObjectPtr<UMaterialInstanceDynamic> Material = PreviewMaterialPackedRC;
		PreviewMesh->SetOverrideRenderMaterial(Material);
		
		if (VisualizationProps->bPreviewAsMaterial)
		{
			// We set all textures which were computed in the corresponding texture channels
			Material->SetTextureParameterValue(TEXT("BaseColor"), Options.bBakeBaseColor ? ResultSettings->BaseColorMap : EmptyColorMapWhite);
			Material->SetTextureParameterValue(TEXT("Emissive"),  Options.bBakeEmissive  ? ResultSettings->EmissiveMap  : EmptyEmissiveMap);
			Material->SetTextureParameterValue(TEXT("NormalMap"), Options.bBakeNormalMap ? ResultSettings->NormalMap    : EmptyNormalMap);
			Material->SetTextureParameterValue(TEXT("PackedMRS"), ResultSettings->PackedMRSMap);
		}
		else
		{
			// The BaseColor texture channel will be set according to the selected MapPreview
			TObjectPtr<UTexture2D> BaseColorMap = EmptyColorMapWhite;
			if (Options.bBakeBaseColor && Settings->MapPreview == BaseColorTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->BaseColorMap;
			}
			else if (Options.bBakeEmissive && Settings->MapPreview == EmissiveTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->EmissiveMap;
			}
			else if (Options.bBakeNormalMap && Settings->MapPreview == NormalTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->NormalMap;
			}
			else if (Settings->MapPreview == PackedMRSTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->PackedMRSMap;
			}
			Material->SetTextureParameterValue(TEXT("BaseColor"), BaseColorMap);
			Material->SetTextureParameterValue(TEXT("Emissive"),  EmptyEmissiveMap);
			Material->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
			Material->SetTextureParameterValue(TEXT("PackedMRS"), EmptyPackedMRSMap);
		}

		Material->SetScalarParameterValue(TEXT("UVChannel"), Options.TargetUVLayer);
	}
	else
	{
		TObjectPtr<UMaterialInstanceDynamic> Material = PreviewMaterialRC;
		PreviewMesh->SetOverrideRenderMaterial(Material);

		if (VisualizationProps->bPreviewAsMaterial)
		{
			// We set all textures which were computed in the corresponding texture channels
			Material->SetTextureParameterValue(TEXT("BaseColor"), Options.bBakeBaseColor ? ResultSettings->BaseColorMap : EmptyColorMapWhite);
			Material->SetTextureParameterValue(TEXT("Roughness"), Options.bBakeRoughness ? ResultSettings->RoughnessMap : EmptyRoughnessMap);
			Material->SetTextureParameterValue(TEXT("Metallic"),  Options.bBakeMetallic  ? ResultSettings->MetallicMap  : EmptyMetallicMap);
			Material->SetTextureParameterValue(TEXT("Specular"),  Options.bBakeSpecular  ? ResultSettings->SpecularMap  : EmptySpecularMap);
			Material->SetTextureParameterValue(TEXT("Emissive"),  Options.bBakeEmissive  ? ResultSettings->EmissiveMap  : EmptyEmissiveMap);
			Material->SetTextureParameterValue(TEXT("NormalMap"), Options.bBakeNormalMap ? ResultSettings->NormalMap    : EmptyNormalMap);
		}
		else
		{
			// The BaseColor texture channel will be set according to the selected MapPreview
			TObjectPtr<UTexture2D> BaseColorMap = EmptyColorMapWhite;
			if (Options.bBakeBaseColor && Settings->MapPreview == BaseColorTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->BaseColorMap;
			}
			else if (Options.bBakeRoughness && Settings->MapPreview == RoughnessTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->RoughnessMap;
			}
			else if (Options.bBakeMetallic && Settings->MapPreview == MetallicTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->MetallicMap;
			}
			else if (Options.bBakeSpecular && Settings->MapPreview == SpecularTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->SpecularMap;
			}
			else if (Options.bBakeEmissive && Settings->MapPreview == EmissiveTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->EmissiveMap;
			}
			else if (Options.bBakeNormalMap && Settings->MapPreview == NormalTexParamName.ToString())
			{
				BaseColorMap = ResultSettings->NormalMap;
			}
			Material->SetTextureParameterValue(TEXT("BaseColor"), BaseColorMap);
			
			Material->SetTextureParameterValue(TEXT("Roughness"), EmptyRoughnessMap);
			Material->SetTextureParameterValue(TEXT("Metallic"),  EmptyMetallicMap);
			Material->SetTextureParameterValue(TEXT("Specular"),  EmptySpecularMap);
			Material->SetTextureParameterValue(TEXT("Emissive"),  EmptyEmissiveMap);
			Material->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
		}
		
		Material->SetScalarParameterValue(TEXT("UVChannel"), Options.TargetUVLayer);
	}
}


void UBakeRenderCaptureTool::GatherAnalytics(FBakeAnalytics::FMeshSettings& Data)
{
	
}


void UBakeRenderCaptureTool::InvalidateResults()
{
	ResultSettings->BaseColorMap = nullptr;
	ResultSettings->RoughnessMap = nullptr;
	ResultSettings->MetallicMap = nullptr;
	ResultSettings->SpecularMap = nullptr;
	ResultSettings->PackedMRSMap = nullptr;
	ResultSettings->EmissiveMap = nullptr;
	ResultSettings->NormalMap = nullptr;
}

#undef LOCTEXT_NAMESPACE
