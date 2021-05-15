// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryProcessing/ApproximateActorsImpl.h"

#include "Scene/MeshSceneAdapter.h"
#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "MeshTangents.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "Implicit/Solidify.h"
#include "Implicit/Morphology.h"
#include "Operations/RemoveOccludedTriangles.h"
#include "ParameterizationOps/ParameterizeMeshOp.h"
#include "Parameterization/MeshUVPacking.h"
#include "MeshQueries.h"
#include "ProjectionTargets.h"
#include "Selections/MeshFaceSelection.h"

#include "AssetUtils/CreateStaticMeshUtil.h"
#include "AssetUtils/CreateTexture2DUtil.h"
#include "AssetUtils/CreateMaterialUtil.h"
#include "AssetUtils/Texture2DUtil.h"
#include "UObject/UObjectGlobals.h"		// for CreatePackage

#include "ImageUtils.h"
#include "Image/ImageInfilling.h"
#include "Sampling/MeshGenericWorldPositionBaker.h"
#include "Scene/SceneCapturePhotoSet.h"
#include "AssetUtils/Texture2DBuilder.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "Async/Async.h"
#include "Misc/ScopedSlowTask.h"

using namespace UE::Geometry;
using namespace UE::AssetUtils;

#define LOCTEXT_NAMESPACE "ApproximateActorsImpl"

struct FGeneratedResultTextures
{
	UTexture2D* BaseColorMap;
	UTexture2D* RoughnessMap;
	UTexture2D* MetallicMap;
	UTexture2D* SpecularMap;
	UTexture2D* EmissiveMap;
	UTexture2D* NormalMap;
};


static TUniquePtr<FSceneCapturePhotoSet> CapturePhotoSet(
	const TArray<AActor*>& Actors,
	const IGeometryProcessing_ApproximateActors::FOptions& Options
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Captures);

	double FieldOfView = Options.FieldOfViewDegrees;
	double NearPlaneDist = Options.NearPlaneDist;

	FImageDimensions CaptureDimensions(Options.RenderCaptureImageSize, Options.RenderCaptureImageSize);

	TUniquePtr<FSceneCapturePhotoSet> SceneCapture = MakeUnique<FSceneCapturePhotoSet>();
	SceneCapture->SetCaptureSceneActors(Actors[0]->GetWorld(), Actors);

	SceneCapture->AddStandardExteriorCapturesFromBoundingBox(
		CaptureDimensions, FieldOfView, NearPlaneDist,
		true, true, true);
	
	return SceneCapture;
}

static void BakeTexturesFromPhotoCapture(
	TUniquePtr<FSceneCapturePhotoSet>& SceneCapture,
	const IGeometryProcessing_ApproximateActors::FOptions& Options, 
	FGeneratedResultTextures& GeneratedTextures,
	const FDynamicMesh3* WorldTargetMesh,
	const FMeshTangentsd* MeshTangents
	)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures);

	int32 UVLayer = 0;
	int32 Supersample = FMath::Max(1, Options.AntiAliasMultiSampling);
	if ( (Options.TextureImageSize * Supersample) > 16384)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ApproximateActors] Ignoring requested supersampling rate %d because it would require image buffers with resolution %d, please try lower value."), Supersample, Options.TextureImageSize * Supersample);
		Supersample = 1;
	}

	FImageDimensions OutputDimensions(Options.TextureImageSize*Supersample, Options.TextureImageSize*Supersample);


	FScopedSlowTask Progress(8.f, LOCTEXT("BakingTextures", "Baking Textures..."));
	Progress.MakeDialog(true);

	Progress.EnterProgressFrame(1.f, LOCTEXT("BakingSetup", "Setup..."));

	FDynamicMeshAABBTree3 Spatial(WorldTargetMesh, true);

	FMeshImageBakingCache TempBakeCache;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_MakeCache);
		TempBakeCache.SetDetailMesh(WorldTargetMesh, &Spatial);
		TempBakeCache.SetBakeTargetMesh(WorldTargetMesh);
		TempBakeCache.SetDimensions(OutputDimensions);
		TempBakeCache.SetUVLayer(UVLayer);
		TempBakeCache.SetThickness(0.1);
		TempBakeCache.SetCorrespondenceStrategy(FMeshImageBakingCache::ECorrespondenceStrategy::Identity);
		TempBakeCache.ValidateCache();
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("BakingBaseColor", "Baking Base Color..."));

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
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_BakeColor);
		BaseColorBaker.Bake();
	}

	// find "hole" pixels
	TArray<FVector2i> MissingPixels;
	TUniquePtr<TImageBuilder<FVector4f>> ColorImage = BaseColorBaker.TakeResult();
	TMarchingPixelInfill<FVector4f> Infill;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_ComputeInfill);
		TempBakeCache.FindSamplingHoles([&](const FVector2i& Coords)
		{
			return ColorImage->GetPixel(Coords) == InvalidColor;
		}, MissingPixels);

		// solve infill for the holes while also caching infill information
		Infill.ComputeInfill(*ColorImage, MissingPixels, InvalidColor,
			[](FVector4f SumValue, int32 Count) {
			float InvSum = (Count == 0) ? 1.0f : (1.0f / Count);
			return FVector4f(SumValue.X * InvSum, SumValue.Y * InvSum, SumValue.Z * InvSum, 1.0f);
		});
	}

	// downsample the image if necessary
	if (Supersample > 1)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_Downsample);
		TImageBuilder<FVector4f> Downsampled = ColorImage->FastDownsample(Supersample, FVector4f::Zero(), [](FVector4f V, int N) { return V / (float)N; });
		*ColorImage = MoveTemp(Downsampled);
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

	TUniquePtr<TImageBuilder<FVector4f>> RoughnessImage, MetallicImage, SpecularImage, EmissiveImage;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_OtherChannels);

		Progress.EnterProgressFrame(1.f, LOCTEXT("BakingRoughness", "Baking Roughness..."));
		RoughnessImage = ProcessChannelFunc(ERenderCaptureType::Roughness);
		Progress.EnterProgressFrame(1.f, LOCTEXT("BakingMetallic", "Baking Metallic..."));
		MetallicImage = ProcessChannelFunc(ERenderCaptureType::Metallic);
		Progress.EnterProgressFrame(1.f, LOCTEXT("BakingSpecular", "Baking Specular..."));
		SpecularImage = ProcessChannelFunc(ERenderCaptureType::Specular);
		Progress.EnterProgressFrame(1.f, LOCTEXT("BakingEmissive", "Baking Emissive..."));
		EmissiveImage = ProcessChannelFunc(ERenderCaptureType::Emissive);
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("BakingNormals", "Baking Normals..."));

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
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_NormalMapBake);
		NormalMapBaker.Bake();
	}
	TUniquePtr<TImageBuilder<FVector3f>> NormalImage = NormalMapBaker.TakeResult();

	if (Supersample > 1)
	{
		TImageBuilder<FVector3f> Downsampled = NormalImage->FastDownsample(Supersample, FVector3f::Zero(), [](FVector3f V, int N) { return V / (float)N; });
		*NormalImage = MoveTemp(Downsampled);
	}

	// build textures
	Progress.EnterProgressFrame(1.f, LOCTEXT("BuildingTextures", "Building Textures..."));
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Textures_BuildTextures);

		FScopedSlowTask BuildTexProgress(6.f, LOCTEXT("BuildingTextures", "Building Textures..."));
		BuildTexProgress.MakeDialog(true);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.BaseColorMap = FTexture2DBuilder::BuildTextureFromImage(*ColorImage, FTexture2DBuilder::ETextureType::Color, true, false);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.RoughnessMap = FTexture2DBuilder::BuildTextureFromImage(*RoughnessImage, FTexture2DBuilder::ETextureType::Roughness, false, false);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.MetallicMap = FTexture2DBuilder::BuildTextureFromImage(*MetallicImage, FTexture2DBuilder::ETextureType::Metallic, false, false);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.SpecularMap = FTexture2DBuilder::BuildTextureFromImage(*SpecularImage, FTexture2DBuilder::ETextureType::Specular, false, false);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.EmissiveMap = FTexture2DBuilder::BuildTextureFromImage(*EmissiveImage, FTexture2DBuilder::ETextureType::Color, true, false);
		BuildTexProgress.EnterProgressFrame(1.f);
		GeneratedTextures.NormalMap = FTexture2DBuilder::BuildTextureFromImage(*NormalImage, FTexture2DBuilder::ETextureType::NormalMap, false, false);
	}
}

struct FApproximationMeshData
{
	IGeometryProcessing_ApproximateActors::EResultCode ResultCode = IGeometryProcessing_ApproximateActors::EResultCode::UnknownError;

	bool bHaveMesh = false;
	FDynamicMesh3 Mesh;

	bool bHaveTangents = false;
	FMeshTangentsd Tangents;
};


static TSharedPtr<FApproximationMeshData> GenerateApproximationMesh(
	FMeshSceneAdapter& Scene,
	const IGeometryProcessing_ApproximateActors::FOptions& Options,
	double ApproxAccuracy
)
{
	FScopedSlowTask Progress(8.f, LOCTEXT("Generating Mesh", "Generating Mesh.."));

	TSharedPtr<FApproximationMeshData> Result = MakeShared<FApproximationMeshData>();

	// collect seed poitns
	TArray<FVector3d> SeedPoints;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_SeedPoints);
		Scene.CollectMeshSeedPoints(SeedPoints);
	}
	FAxisAlignedBox3d SceneBounds = Scene.GetBoundingBox();

	// calculate a voxel size based on target world-space approximation accuracy
	float WorldBoundsSize = SceneBounds.DiagonalLength();
	int32 VoxelDimTarget = (int)(WorldBoundsSize / ApproxAccuracy) + 1;
	if (VoxelDimTarget < 64)
	{
		VoxelDimTarget = 64;		// use a sane minimum in case the parameter is super-wrong
	}

	// avoid insane memory usage
	if (VoxelDimTarget > Options.ClampVoxelDimension)
	{
		UE_LOG(LogTemp, Warning, TEXT("FApproximateActorsImpl - very large voxel size %d clamped to %d"), VoxelDimTarget, Options.ClampVoxelDimension);
		VoxelDimTarget = Options.ClampVoxelDimension;
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("SolidifyMesh", "Approximating Mesh..."));

	FWindingNumberBasedSolidify Solidify(
		[&Scene](const FVector3d& Position) { return Scene.FastWindingNumber(Position); },
		SceneBounds, SeedPoints);
	Solidify.SetCellSizeAndExtendBounds(SceneBounds, 2.0 * ApproxAccuracy, VoxelDimTarget);
	Solidify.WindingThreshold = Options.WindingThreshold;

	FDynamicMesh3 SolidMesh;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Solidify);
		SolidMesh = FDynamicMesh3(&Solidify.Generate());
	}
	SolidMesh.DiscardAttributes();
	FDynamicMesh3* CurResultMesh = &SolidMesh;		// this pointer will be updated as we recompute the mesh

	if (Options.bVerbose)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ApproximateActors] Solidify mesh has %d triangles"), CurResultMesh->TriangleCount());
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("ClosingMesh", "Topological Operations..."));

	// do topological closure to fix small gaps/etc
	FDynamicMesh3 MorphologyMesh;
	if (Options.bApplyMorphology)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Morphology);
		double MorphologyDistance = Options.MorphologyDistanceMeters * 100.0;		// convert to cm
		FAxisAlignedBox3d MorphologyBounds = CurResultMesh->GetBounds();
		FDynamicMeshAABBTree3 MorphologyBVTree(CurResultMesh);
		TImplicitMorphology<FDynamicMesh3> ImplicitMorphology;
		ImplicitMorphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close;
		ImplicitMorphology.Source = CurResultMesh;
		ImplicitMorphology.SourceSpatial = &MorphologyBVTree;
		ImplicitMorphology.SetCellSizesAndDistance(MorphologyBounds, MorphologyDistance, VoxelDimTarget, VoxelDimTarget);
		MorphologyMesh = FDynamicMesh3(&ImplicitMorphology.Generate());
		MorphologyMesh.DiscardAttributes();
		CurResultMesh = &MorphologyMesh;
	}

	// if mesh has no triangles, something has gone wrong
	if (CurResultMesh == nullptr || CurResultMesh->TriangleCount() == 0)
	{
		Result->ResultCode = IGeometryProcessing_ApproximateActors::EResultCode::MeshGenerationFailed;
		return Result;
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("RemoveHidden", "Removing Hidden Geometry..."));

	if (Options.OcclusionPolicy == IGeometryProcessing_ApproximateActors::EOcclusionPolicy::VisibilityBased)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Occlusion);
		TRemoveOccludedTriangles<FDynamicMesh3> Remover(CurResultMesh);
		Remover.InsideMode = EOcclusionCalculationMode::SimpleOcclusionTest;
		Remover.TriangleSamplingMethod = EOcclusionTriangleSampling::Centroids;
		Remover.AddTriangleSamples = 5;
		FDynamicMeshAABBTree3 CurResultMeshSpatial(CurResultMesh, true);

		TArray<FTransform3d> NoTransforms;
		NoTransforms.Add(FTransform3d::Identity());
		TArray<FDynamicMeshAABBTree3*> Spatials;
		Spatials.Add(&CurResultMeshSpatial);
		Remover.Select(NoTransforms, Spatials, {}, NoTransforms);
		if (Remover.RemovedT.Num() > 0)
		{
			FMeshFaceSelection Selection(CurResultMesh);
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Occlusion_Build);
				Selection.Select(Remover.RemovedT);
				Selection.ExpandToOneRingNeighbours(1);
				Selection.ContractBorderByOneRingNeighbours(4);
			}
			FDynamicMeshEditor Editor(CurResultMesh);
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Occlusion_Delete);
				Editor.RemoveTriangles(Selection.AsArray(), true);
			}
		}
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("SimplifyingMesh", "Simplifying Mesh..."));

	FVolPresMeshSimplification Simplifier(CurResultMesh);
	Simplifier.ProjectionMode = FVolPresMeshSimplification::ETargetProjectionMode::NoProjection;
	Simplifier.DEBUG_CHECK_LEVEL = 0;
	Simplifier.bAllowSeamCollapse = false;

	int32 BaseTargeTriCount = Options.FixedTriangleCount;
	{
		int32 BeforeCount = CurResultMesh->TriangleCount();

		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Simplification);
		if (Options.MeshSimplificationPolicy == IGeometryProcessing_ApproximateActors::ESimplificationPolicy::TrianglesPerUnitSqMeter)
		{
			FVector2d VolArea = TMeshQueries<FDynamicMesh3>::GetVolumeArea(*CurResultMesh);
			double MeshAreaMeterSqr = VolArea.Y * 0.0001;
			int32 AreaBaseTargetTriCount = MeshAreaMeterSqr * Options.SimplificationTargetMetric;
			Simplifier.SimplifyToTriangleCount(AreaBaseTargetTriCount);
		}
		else if (Options.MeshSimplificationPolicy == IGeometryProcessing_ApproximateActors::ESimplificationPolicy::GeometricTolerance)
		{
			double UseTargetTolerance = Options.SimplificationTargetMetric * 100.0;		// convert to cm (UE Units)

			// first do fast collapse
			Simplifier.FastCollapsePass(0.1 * UseTargetTolerance, 5);

			// now simplify down to a reasonable tri count, as geometric metric is (relatively) expensive
			// (still, this is all incredibly cheap compared to the cost of the rest of this method in practice)
			Simplifier.SimplifyToTriangleCount(50000);

			FDynamicMesh3 MeshCopy(*CurResultMesh);
			FDynamicMeshAABBTree3 MeshCopySpatial(&MeshCopy, true);
			FMeshProjectionTarget ProjectionTarget(&MeshCopy, &MeshCopySpatial);
			Simplifier.SetProjectionTarget(&ProjectionTarget);
			Simplifier.GeometricErrorConstraint = FVolPresMeshSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
			Simplifier.GeometricErrorTolerance = UseTargetTolerance;
			Simplifier.SimplifyToTriangleCount(8);
		}
		else
		{
			Simplifier.SimplifyToTriangleCount(BaseTargeTriCount);
		}

		int32 AfterCount = CurResultMesh->TriangleCount();
		if (Options.bVerbose)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ApproximateActors] Simplified mesh from %d to %d triangles"), BeforeCount, AfterCount);
		}
	}

	// re-enable attributes
	CurResultMesh->EnableAttributes();

	//  TODO: clip hidden triangles against occluder geo like landscape

	// compute normals
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Normals);
		FMeshNormals::InitializeOverlayToPerVertexNormals(CurResultMesh->Attributes()->PrimaryNormals());
	}

	// exit here if we are just generating a merged collision mesh
	if (Options.BasePolicy == IGeometryProcessing_ApproximateActors::EApproximationPolicy::CollisionMesh)
	{
		Result->ResultCode = IGeometryProcessing_ApproximateActors::EResultCode::Success;
		Result->bHaveMesh = true;
		Result->Mesh = MoveTemp(*CurResultMesh);
		return Result;
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("ComputingUVs", "Computing UVs..."));

	// compute UVs
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> UVInputMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	*UVInputMesh = MoveTemp(*CurResultMesh);
	FParameterizeMeshOp ParameterizeMeshOp;
	ParameterizeMeshOp.Stretch = 0.1;
	ParameterizeMeshOp.NumCharts = 0;
	ParameterizeMeshOp.InputMesh = UVInputMesh;
	ParameterizeMeshOp.IslandMode = EParamOpIslandMode::Auto;
	ParameterizeMeshOp.UnwrapType = EParamOpUnwrapType::MinStretch;
	FProgressCancel UVProgressCancel;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_GenerateUVs);
		ParameterizeMeshOp.CalculateResult(&UVProgressCancel);
	}

	TUniquePtr<FDynamicMesh3> FinalMesh = ParameterizeMeshOp.ExtractResult();

	Progress.EnterProgressFrame(1.f, LOCTEXT("PackingUVs", "Packing UVs..."));

	// repack UVs
	FDynamicMeshUVOverlay* RepackUVLayer = FinalMesh->Attributes()->PrimaryUV();
	RepackUVLayer->SplitBowties();
	FDynamicMeshUVPacker Packer(RepackUVLayer);
	Packer.TextureResolution = Options.TextureImageSize / 4;		// maybe too conservative? We don't have gutter control currently.
	Packer.GutterSize = 1.0;		// not clear this works
	Packer.bAllowFlips = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_PackUVs);
		bool bOK = Packer.StandardPack();
		ensure(bOK);
	}

	Progress.EnterProgressFrame(1.f, LOCTEXT("ComputingTangents", "Computing Tangents..."));

	Result->ResultCode = IGeometryProcessing_ApproximateActors::EResultCode::Success;
	Result->bHaveMesh = true;
	Result->Mesh = MoveTemp(*FinalMesh);

	// compute tangents
	Result->bHaveTangents = true;
	Result->Tangents.SetMesh(&Result->Mesh);
	FComputeTangentsOptions TangentsOptions;
	TangentsOptions.bAveraged = true;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Tangents);
		Result->Tangents.ComputeTriVertexTangents(
			Result->Mesh.Attributes()->PrimaryNormals(),
			Result->Mesh.Attributes()->PrimaryUV(),
			TangentsOptions);
	}

	return Result;
}




void FApproximateActorsImpl::ApproximateActors(const TArray<AActor*>& Actors, const FOptions& Options, FResults& ResultsOut)
{
	int32 ActorClusters = 1;
	FScopedSlowTask Progress(1.f, LOCTEXT("ApproximatingActors", "Generating Actor Approximation..."));
	Progress.MakeDialog(true);
	Progress.EnterProgressFrame(1.f);
	GenerateApproximationForActorSet(Actors, Options, ResultsOut);
}




void FApproximateActorsImpl::GenerateApproximationForActorSet(const TArray<AActor*>& Actors, const FOptions& Options, FResults& ResultsOut)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate);

	//
	// Future Optimizations
	// 	   - can do most of the mesh processing at the same time as capturing the photo set (if that matters)
	//     - some parts of mesh gen can be done simultaneously (maybe?)
	//

	FScopedSlowTask Progress(11.f, LOCTEXT("ApproximatingActors", "Generating Actor Approximation..."));

	Progress.EnterProgressFrame(1.f, LOCTEXT("BuildingScene", "Building Scene..."));

	float ApproxAccuracy = Options.WorldSpaceApproximationAccuracyMeters * 100.0;		// convert to cm (UE Units)

	FMeshSceneAdapter Scene;
	FMeshSceneAdapterBuildOptions SceneBuildOptions;
	SceneBuildOptions.bThickenThinMeshes = Options.bAutoThickenThinParts;
	SceneBuildOptions.DesiredMinThickness = Options.AutoThickenThicknessMeters * 100.0;		// convert to cm (UE Units)
	SceneBuildOptions.bPrintDebugMessages = Options.bVerbose;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_BuildScene);
		Scene.AddActors(Actors);
		Scene.Build(SceneBuildOptions);
	}

	// todo: make optional
	if (Options.bVerbose)
	{
		FMeshSceneAdapter::FStatistics Stats;
		Scene.GetGeometryStatistics(Stats);
		UE_LOG(LogTemp, Warning, TEXT("[ApproximateActors] %d triangles in %d unique meshes, total %d triangles in %d instances"),
			Stats.UniqueMeshTriangleCount, Stats.UniqueMeshCount, Stats.InstanceMeshTriangleCount, Stats.InstanceMeshCount);
	}

	if (Options.BaseCappingPolicy != EBaseCappingPolicy::NoBaseCapping)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_Capping);
		double UseThickness = (Options.BaseThicknessOverrideMeters != 0) ? (Options.BaseThicknessOverrideMeters * 100.0) :
			(Options.bAutoThickenThinParts ? SceneBuildOptions.DesiredMinThickness : 1.25 * ApproxAccuracy);
		double UseHeight = (Options.BaseHeightOverrideMeters != 0) ? (Options.BaseHeightOverrideMeters * 100.0) : (2.0 * ApproxAccuracy);
		Scene.GenerateBaseClosingMesh(UseHeight, UseThickness);
	}

	FDynamicMesh3 DebugMesh;
	FDynamicMesh3* WriteDebugMesh = nullptr;
	if (Options.bWriteDebugMesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApproximateActorsImpl_Generate_DebugMesh);
		DebugMesh.EnableAttributes();
		Scene.GetAccumulatedMesh(DebugMesh);
		FMeshNormals::InitializeMeshToPerTriangleNormals(&DebugMesh);
		WriteDebugMesh = &DebugMesh;
	}

	// if we are only generating collision mesh, we are going to exit after mesh generation
	if (Options.BasePolicy == IGeometryProcessing_ApproximateActors::EApproximationPolicy::CollisionMesh)
	{
		TSharedPtr<FApproximationMeshData> ApproximationMeshData = GenerateApproximationMesh(Scene, Options, ApproxAccuracy);
		ResultsOut.ResultCode = ApproximationMeshData->ResultCode;
		if (ResultsOut.ResultCode == EResultCode::Success)
		{
			EmitGeneratedMeshAsset(Actors, Options, ResultsOut, &ApproximationMeshData->Mesh, nullptr, WriteDebugMesh);
		}
		return;
	}

	// launch async mesh compute which can run while we do (relatively) expensive render captures
	TFuture<TSharedPtr<FApproximationMeshData>> MeshComputeFuture = Async(EAsyncExecution::Thread,
		[&Scene, &Options, &ApproxAccuracy]() {
			return GenerateApproximationMesh(Scene, Options, ApproxAccuracy);
		});

	Progress.EnterProgressFrame(1.f, LOCTEXT("CapturingScene", "Capturing Scene..."));

	TUniquePtr<FSceneCapturePhotoSet> SceneCapture = CapturePhotoSet(Actors, Options);

	Progress.EnterProgressFrame(1.f, LOCTEXT("BakingTextures", "Baking Textures..."));

	// need to wait for mesh to finish computing
	MeshComputeFuture.Wait();
	TSharedPtr<FApproximationMeshData> ApproximationMeshData = MeshComputeFuture.Get();
	if (ApproximationMeshData->ResultCode != EResultCode::Success)
	{
		ResultsOut.ResultCode = ApproximationMeshData->ResultCode;
		return;
	}
	FDynamicMesh3 FinalMesh = MoveTemp(ApproximationMeshData->Mesh);
	FMeshTangentsd FinalMeshTangents = MoveTemp(ApproximationMeshData->Tangents);

	// bake textures for Actor
	FGeneratedResultTextures GeneratedTextures;
	BakeTexturesFromPhotoCapture(SceneCapture, Options,
		GeneratedTextures,
		&FinalMesh, &FinalMeshTangents);

	Progress.EnterProgressFrame(1.f, LOCTEXT("Writing Assets", "Writing Assets..."));

	// Make material for textures by duplicating input material (hardcoded!!)
	UMaterialInterface* UseBaseMaterial = (Options.BakeMaterial != nullptr) ? 
		Options.BakeMaterial : LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/FullMaterialBakePreviewMaterial"));
	FMaterialAssetOptions MatOptions;
	MatOptions.NewAssetPath = Options.BasePackagePath + TEXT("_Material");
	FMaterialAssetResults MatResults;
	ECreateMaterialResult MatResult = UE::AssetUtils::CreateDerivedMaterialInstance(UseBaseMaterial, MatOptions, MatResults);
	UMaterialInstanceConstant* NewMaterial = nullptr;
	if (ensure(MatResult == ECreateMaterialResult::Ok))
	{
		NewMaterial = MatResults.NewMaterialInstance;
		ResultsOut.NewMaterials.Add(NewMaterial);
	}

	// this lambda converts a generated texture to an Asset, and then assigns it to a parameter of the Material
	FString BaseTexturePath = MatOptions.NewAssetPath;
	auto WriteTextureLambda = [BaseTexturePath, NewMaterial, &ResultsOut](
		UTexture2D* Texture, 
		FString TextureTypeSuffix, 
		FTexture2DBuilder::ETextureType Type,
		FName MaterialParamName )
	{
		if (ensure(Texture != nullptr) == false) return;

		FTexture2DBuilder::CopyPlatformDataToSourceData(Texture, Type);

		if (Type == FTexture2DBuilder::ETextureType::Roughness
			|| Type == FTexture2DBuilder::ETextureType::Metallic
			|| Type == FTexture2DBuilder::ETextureType::Specular)
		{
			UE::AssetUtils::ConvertToSingleChannel(Texture);
		}

		FTexture2DAssetOptions TexOptions;
		TexOptions.NewAssetPath = BaseTexturePath + TextureTypeSuffix;
		FTexture2DAssetResults Results;
		ECreateTexture2DResult TexResult = UE::AssetUtils::SaveGeneratedTexture2DAsset(Texture, TexOptions, Results);
		if (ensure(TexResult == ECreateTexture2DResult::Ok))
		{
			ResultsOut.NewTextures.Add(Texture);
			if (NewMaterial != nullptr)
			{
				NewMaterial->SetTextureParameterValueEditorOnly(MaterialParamName, Texture);
			}
		}
	};


	// process the generated textures
	if (Options.bBakeBaseColor && GeneratedTextures.BaseColorMap)
	{
		WriteTextureLambda(GeneratedTextures.BaseColorMap, TEXT("_BaseColor"), FTexture2DBuilder::ETextureType::Color, Options.BaseColorTexParamName);
	}
	if (Options.bBakeRoughness && GeneratedTextures.RoughnessMap)
	{
		WriteTextureLambda(GeneratedTextures.RoughnessMap, TEXT("_Roughness"), FTexture2DBuilder::ETextureType::Roughness, Options.RoughnessTexParamName);
	}
	if (Options.bBakeMetallic && GeneratedTextures.MetallicMap)
	{
		WriteTextureLambda(GeneratedTextures.MetallicMap, TEXT("_Metallic"), FTexture2DBuilder::ETextureType::Metallic, Options.MetallicTexParamName);
	}
	if (Options.bBakeSpecular && GeneratedTextures.SpecularMap)
	{
		WriteTextureLambda(GeneratedTextures.SpecularMap, TEXT("_Specular"), FTexture2DBuilder::ETextureType::Specular, Options.SpecularTexParamName);
	}
	if (Options.bBakeEmissive && GeneratedTextures.EmissiveMap)
	{
		WriteTextureLambda(GeneratedTextures.EmissiveMap, TEXT("_Emissive"), FTexture2DBuilder::ETextureType::Color, Options.EmissiveTexParamName);
	}
	if (Options.bBakeNormalMap && GeneratedTextures.NormalMap)
	{
		WriteTextureLambda(GeneratedTextures.NormalMap, TEXT("_Normal"), FTexture2DBuilder::ETextureType::NormalMap, Options.NormalTexParamName);
	}

	// force material update now that we have updated texture parameters
	// (does this do that? Let calling code do it?)
	NewMaterial->PostEditChange();

	EmitGeneratedMeshAsset(Actors, Options, ResultsOut, &FinalMesh, NewMaterial, WriteDebugMesh);
	ResultsOut.ResultCode = EResultCode::Success;
}


UStaticMesh* FApproximateActorsImpl::EmitGeneratedMeshAsset(
	const TArray<AActor*>& Actors, 
	const FOptions& Options, 
	FResults& ResultsOut,
	FDynamicMesh3* FinalMesh,
	UMaterialInterface* Material,
	FDynamicMesh3* DebugMesh)
{
	FStaticMeshAssetOptions MeshAssetOptions;
	MeshAssetOptions.NewAssetPath = Options.BasePackagePath;
	MeshAssetOptions.SourceMeshes.DynamicMeshes.Add(FinalMesh);
	if (Material)
	{
		MeshAssetOptions.AssetMaterials.Add(Material);
	}
	FStaticMeshResults MeshAssetOutputs;
	ECreateStaticMeshResult ResultCode = UE::AssetUtils::CreateStaticMeshAsset(MeshAssetOptions, MeshAssetOutputs);
	ensure(ResultCode == ECreateStaticMeshResult::Ok);

	ResultsOut.NewMeshAssets.Add(MeshAssetOutputs.StaticMesh);

	if (DebugMesh != nullptr)
	{
		FStaticMeshAssetOptions DebugMeshAssetOptions;
		DebugMeshAssetOptions.NewAssetPath = Options.BasePackagePath + TEXT("_DEBUG");
		DebugMeshAssetOptions.SourceMeshes.DynamicMeshes.Add(DebugMesh);
		FStaticMeshResults DebugMeshAssetOutputs;
		UE::AssetUtils::CreateStaticMeshAsset(DebugMeshAssetOptions, DebugMeshAssetOutputs);
	}

	return MeshAssetOutputs.StaticMesh;
}


#undef LOCTEXT_NAMESPACE