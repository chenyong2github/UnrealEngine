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

#include "Sampling/MeshImageBakingCache.h"
#include "Sampling/MeshMapBaker.h"
#include "Sampling/MeshGenericWorldPositionEvaluator.h"
#include "Image/ImageInfilling.h"
#include "Algo/NoneOf.h"


using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UBakeRenderCaptureTool"

//
// Implementation details
//



// TODO Reimplement this when the render capture algorithm is refactored to decouple the visibility checking from the evaluation
class FSceneCapturePhotoSetSampler : public FMeshBakerDynamicMeshSampler
{
public:
	FSceneCapturePhotoSetSampler(
		const FSceneCapturePhotoSet* SceneCapture,
		TFunctionRef<bool(const FVector3d&, const FVector3d&)> VisibilityFunction,
		const FDynamicMesh3* Mesh,
		const FDynamicMeshAABBTree3* Spatial,
		const FMeshTangentsd* Tangents = nullptr) :
		FMeshBakerDynamicMeshSampler(Mesh, Spatial, Tangents),
		SceneCapture(SceneCapture),
		VisibilityFunction(VisibilityFunction)
	{
	}

	virtual bool IsValidCorrespondence(const FMeshMapEvaluator::FCorrespondenceSample& Sample) const override
	{
		return SceneCapture->IsValidSample(Sample.BaseSample.SurfacePoint, Sample.BaseNormal, VisibilityFunction);
	}

public:
	const FSceneCapturePhotoSet* SceneCapture = nullptr;
	TFunctionRef<bool(const FVector3d&, const FVector3d&)> VisibilityFunction;
};



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

		EBakeTextureSamplesPerPixel SamplesPerPixel = EBakeTextureSamplesPerPixel::Sample1;

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
		Options.SamplesPerPixel = UseSettings.SamplesPerPixel;

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
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Roughness, Options.bBakeRoughness);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Metallic, Options.bBakeMetallic);
		SceneCapture->SetCaptureTypeEnabled(ERenderCaptureType::Specular, Options.bBakeSpecular);
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
	const FDynamicMesh3* BaseMesh,
	const TSharedPtr<UE::Geometry::FMeshTangentsd, ESPMode::ThreadSafe>& BaseMeshTangents,
	TUniquePtr<FBakeRenderCaptureResultsBuilder>& Results)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet);

	FDynamicMeshAABBTree3 BaseMeshSpatial(BaseMesh, true);

	double RayOffsetHackDist = (double)(100.0f * FMathf::ZeroTolerance * BaseMesh->GetBounds().MinDim() );
	auto VisibilityFunction = [&BaseMeshSpatial, RayOffsetHackDist](const FVector3d& SurfPos, const FVector3d& ImagePosWorld)
	{
		FVector3d RayDir = ImagePosWorld - SurfPos;
		double Dist = Normalize(RayDir);
		FVector3d RayOrigin = SurfPos + RayOffsetHackDist * RayDir;
		int32 HitTID = BaseMeshSpatial.FindNearestHitTriangle(FRay3d(RayOrigin, RayDir), IMeshSpatial::FQueryOptions(Dist));
		return (HitTID == IndexConstants::InvalidID);
	};

	struct FInfillData
	{
		struct FSampleStats {
			uint16 NumValid = 0;
			uint16 NumInvalid = 0;

			// The ==, !=, += operators and the Zero() function are required by the TMarchingPixelInfill implementation

			bool operator==(const FSampleStats& Other) const
			{
				return (NumValid == Other.NumValid) && (NumInvalid == Other.NumInvalid);
			}

			bool operator!=(const FSampleStats& Other) const
			{
				return !(*this == Other);
			}

			FSampleStats& operator+=(const FSampleStats& Other)
			{
				NumValid += Other.NumValid;
				NumInvalid += Other.NumInvalid;
				return *this;
			}

			static FSampleStats Zero()
			{
				return FSampleStats{0, 0};
			}
		};

		// Collect some sample stats per pixel, used to determine if a pixel requires infill or not
		TImageBuilder<FSampleStats> SampleStats;

		// The i-th element of this array indicates if the i-th evaluator needs infill
		TArray<bool> EvaluatorNeedsInfill;
	} InfillData;

	InfillData.SampleStats.SetDimensions(FImageDimensions(Options.TextureImageSize, Options.TextureImageSize));
	InfillData.SampleStats.Clear(FInfillData::FSampleStats{});

	auto RegisterSampleStats = [&InfillData](bool bSampleValid, const FMeshMapEvaluator::FCorrespondenceSample& Sample, const FVector2d& UVPosition, const FVector2i& ImageCoords)
	{
		checkSlow(InfillData.SampleStats.GetDimensions().IsValidCoords(ImageCoords));
		if (bSampleValid)
		{
			InfillData.SampleStats.GetPixel(ImageCoords).NumValid += 1;
		}
		else
		{
			InfillData.SampleStats.GetPixel(ImageCoords).NumInvalid += 1;
		}
	};

	auto ComputeAndApplyInfill = [&InfillData](TArray<TUniquePtr<TImageBuilder<FVector4f>>>& BakeResults)
	{
		check(BakeResults.Num() == InfillData.EvaluatorNeedsInfill.Num());

		if (BakeResults.IsEmpty() || Algo::NoneOf(InfillData.EvaluatorNeedsInfill))
		{
			return;
		}

		// Find pixels that need infill
		TArray<FVector2i> MissingPixels;
		FCriticalSection MissingPixelsLock;
		ParallelFor(InfillData.SampleStats.GetDimensions().GetHeight(), [&MissingPixels, &MissingPixelsLock, &InfillData](int32 Y)
		{
			for (int32 X = 0; X < InfillData.SampleStats.GetDimensions().GetWidth(); X++)
			{
				const FInfillData::FSampleStats& Stats = InfillData.SampleStats.GetPixel(X, Y);
				// TODO experiment with other classifications
				if (Stats.NumInvalid > 0 && Stats.NumValid == 0)
				{
					MissingPixelsLock.Lock();
					MissingPixels.Add(FVector2i(X, Y));
					MissingPixelsLock.Unlock();
				}
			}
		});

		auto NormalizeFunc = [](FVector4f SumValue, int32 Count)
		{
			float InvSum = (Count == 0) ? 1.0f : (1.0f / Count);
			return FVector4f(SumValue.X * InvSum, SumValue.Y * InvSum, SumValue.Z * InvSum, 1.0f);
		};
		
		auto DummyNormalizeStatsFunc = [](FInfillData::FSampleStats SumValue, int32 Count)
		{
			// The return value must be different from MissingValue below so that ComputeInfill works correctly
			return FInfillData::FSampleStats{TNumericLimits<uint16>::Max(), TNumericLimits<uint16>::Max()};
		};

		TMarchingPixelInfill<FInfillData::FSampleStats> Infill;
		// This must be the same as the value of exterior pixels, otherwise infill will spread the exterior values into the texture
		FInfillData::FSampleStats MissingValue{0, 0};
		Infill.ComputeInfill(InfillData.SampleStats, MissingPixels, MissingValue, DummyNormalizeStatsFunc);
		for (int EvaluatorIndex = 0; EvaluatorIndex < BakeResults.Num(); EvaluatorIndex++)
		{
			if (InfillData.EvaluatorNeedsInfill[EvaluatorIndex])
			{
				Infill.ApplyInfill<FVector4f>(*BakeResults[EvaluatorIndex], NormalizeFunc);
			}
		}
	};

	FMeshMapBaker Baker;
	Baker.SetTargetMesh(BaseMesh);
	Baker.SetTargetMeshTangents(BaseMeshTangents);
	Baker.SetDimensions(FImageDimensions(Options.TextureImageSize, Options.TextureImageSize));
	Baker.SetSamplesPerPixel(static_cast<int32>(Options.SamplesPerPixel));
	Baker.SetFilter(FMeshMapBaker::EBakeFilterType::BSpline);
	Baker.SetTargetMeshUVLayer(Options.TargetUVLayer);
	Baker.InteriorSampleCallback = RegisterSampleStats;
	Baker.PostWriteToImageCallback = ComputeAndApplyInfill;

	FSceneCapturePhotoSetSampler Sampler(SceneCapture, VisibilityFunction, BaseMesh, &BaseMeshSpatial, BaseMeshTangents.Get());
	Baker.SetDetailSampler(&Sampler);
	Baker.SetCorrespondenceStrategy(FMeshMapBaker::ECorrespondenceStrategy::Identity);

	TMap<ERenderCaptureType, int32> CaptureTypeToEvaluatorIndexMap;

	// Pixels in the output textures which don't map onto the mesh have a light grey color (except the normal map which
	// will show a color corresponding to a unit z tangent space normal)
	const FVector4f InvalidColor(.42, .42, .42, 1);
	const FVector3f DefaultNormal = FVector3f::UnitZ();

	FSceneCapturePhotoSet::FSceneSample DefaultColorSample;
	DefaultColorSample.BaseColor = FVector3f(InvalidColor.X, InvalidColor.Y, InvalidColor.Z);
	DefaultColorSample.Roughness = InvalidColor.X;
	DefaultColorSample.Specular = InvalidColor.X;
	DefaultColorSample.Metallic = InvalidColor.X;
	DefaultColorSample.Emissive = FVector3f(InvalidColor.X, InvalidColor.Y, InvalidColor.Z);
	DefaultColorSample.WorldNormal = FVector4f((DefaultNormal + FVector3f::One()) * .5f, InvalidColor.W);

	auto AddColorEvaluator = [&](ERenderCaptureType CaptureType)
	{
		TSharedPtr<FMeshGenericWorldPositionColorEvaluator> Evaluator = MakeShared<FMeshGenericWorldPositionColorEvaluator>();
		Evaluator->DefaultColor = DefaultColorSample.GetValue4f(CaptureType);
		Evaluator->ColorSampleFunction =
			[&DefaultColorSample, &VisibilityFunction, SceneCapture, CaptureType](FVector3d Position, FVector3d Normal)
		{
			FSceneCapturePhotoSet::FSceneSample ColorSample = DefaultColorSample;
			SceneCapture->ComputeSample(FRenderCaptureTypeFlags::Single(CaptureType), Position, Normal, VisibilityFunction, ColorSample);
			return ColorSample.GetValue4f(CaptureType);
		};
		
		int32 EvaluatorIndex = Baker.AddEvaluator(Evaluator);
		
		InfillData.EvaluatorNeedsInfill.Add(true);
		CaptureTypeToEvaluatorIndexMap.Emplace(CaptureType, EvaluatorIndex);
	};

	AddColorEvaluator(ERenderCaptureType::BaseColor);

	if (Options.bUsePackedMRS)
	{
		AddColorEvaluator(ERenderCaptureType::CombinedMRS);
	}
	else
	{
		if (Options.bBakeRoughness)
		{
			AddColorEvaluator(ERenderCaptureType::Roughness);
		}
		if (Options.bBakeMetallic)
		{
			AddColorEvaluator(ERenderCaptureType::Metallic);
		}
		if (Options.bBakeSpecular)
		{
			AddColorEvaluator(ERenderCaptureType::Specular);
		}
	}

	if (Options.bBakeEmissive)
	{
		AddColorEvaluator(ERenderCaptureType::Emissive);
	}

	if (Options.bBakeNormalMap) {
		TSharedPtr<FMeshGenericWorldPositionNormalEvaluator> Evaluator = MakeShared<FMeshGenericWorldPositionNormalEvaluator>();
		Evaluator->DefaultUnitWorldNormal = DefaultNormal;
		Evaluator->UnitWorldNormalSampleFunction =
			[&DefaultColorSample, &VisibilityFunction, SceneCapture](FVector3d Position, FVector3d Normal)
		{
			FSceneCapturePhotoSet::FSceneSample ColorSample = DefaultColorSample;
			SceneCapture->ComputeSample(FRenderCaptureTypeFlags::WorldNormal(), Position, Normal, VisibilityFunction, ColorSample);
			FVector3f NormalColor = ColorSample.WorldNormal;

			// Map from color components [0,1] to normal components [-1,1]
			float x = (NormalColor.X - 0.5f) * 2.0f;
			float y = (NormalColor.Y - 0.5f) * 2.0f;
			float z = (NormalColor.Z - 0.5f) * 2.0f;
			return FVector3f(x, y, z);
		};
		
		int32 EvaluatorIndex = Baker.AddEvaluator(Evaluator);
		
		CaptureTypeToEvaluatorIndexMap.Emplace(ERenderCaptureType::WorldNormal, EvaluatorIndex);

		// Note: No infill on normal map for now, doesn't make sense to do after mapping to tangent space!
		//  (should we build baked normal map in world space, and then resample to tangent space??)
		InfillData.EvaluatorNeedsInfill.Add(false);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ImageBuildersFromPhotoSet_Bake);
		Baker.Bake();
	}

	for (TTuple<ERenderCaptureType, int32>& Item : CaptureTypeToEvaluatorIndexMap)
	{
		if (Item.Key == ERenderCaptureType::BaseColor)
		{
			Results->ColorImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::CombinedMRS)
		{
			Results->PackedMRSImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::Roughness)
		{
			Results->RoughnessImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::Specular)
		{
			Results->SpecularImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::Metallic)
		{
			Results->MetallicImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::Emissive)
		{
			Results->EmissiveImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
		else if (Item.Key == ERenderCaptureType::WorldNormal)
		{
			Results->NormalImage = MoveTemp(Baker.GetBakeResults(Item.Value)[0]);
		}
	}
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
	TUniquePtr<FSceneCapturePhotoSet> SceneCapture;
	
	// Begin TGenericDataOperator interface
	// @Incomplete Use the Progress thing
	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		check(Actors != nullptr);
		check(BaseMesh != nullptr);
		check(BaseMeshTangents.IsValid());
		check(SceneCapture.IsValid());

		// Bake textures onto the base/target mesh by projecting/sampling the set of captured photos
		ImageBuildersFromPhotoSet(SceneCapture.Get(), Options, BaseMesh, BaseMeshTangents, Result);
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
	Settings->WatchProperty(Settings->SamplesPerPixel, [this](EBakeTextureSamplesPerPixel) { OpState |= EBakeOpState::Evaluate; });
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
		CreateTextureAsset(TexName, FTexture2DBuilder::ETextureType::NormalMap, ResultSettings->NormalMap);
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
	Op->SceneCapture = MoveTemp(Tool->SceneCapture);
	return Op;
}


void UBakeRenderCaptureTool::OnMapsUpdatedRC(const TUniquePtr<FBakeRenderCaptureResultsBuilder>& NewResult)
{
	FBakeRenderCaptureOptions::FOptions Options = FBakeRenderCaptureOptions::ConstructOptions(*Settings, *InputMeshSettings);

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
			ResultSettings->NormalMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->NormalImage, FTexture2DBuilder::ETextureType::NormalMap, false, bPopulateSourceData);
		}

		if (Options.bUsePackedMRS && NewResult->PackedMRSImage.IsValid())
		{
			ResultSettings->PackedMRSMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->PackedMRSImage, FTexture2DBuilder::ETextureType::ColorLinear, false, bPopulateSourceData);
		}
		else
		{ 
			if (Options.bBakeRoughness && NewResult->RoughnessImage.IsValid())
			{
				ResultSettings->RoughnessMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->RoughnessImage, FTexture2DBuilder::ETextureType::Roughness, false, bPopulateSourceData);
			}
			if (Options.bBakeMetallic && NewResult->MetallicImage.IsValid())
			{
				ResultSettings->MetallicMap = FTexture2DBuilder::BuildTextureFromImage(*NewResult->MetallicImage, FTexture2DBuilder::ETextureType::Metallic, false, bPopulateSourceData);
			}
			if (Options.bBakeSpecular && NewResult->SpecularImage.IsValid())
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
	
	if (Options.bUsePackedMRS)
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
