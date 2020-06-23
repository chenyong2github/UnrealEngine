// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeMapsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Sampling/SphericalFibonacci.h"
#include "Sampling/Gaussians.h"
#include "Image/ImageOccupancyMap.h"
#include "Util/IndexUtil.h"

#include "SimpleDynamicMeshComponent.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ImageUtils.h"

#include "AssetUtils/Texture2DBuilder.h"
#include "AssetGenerationUtil.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "UBakeMeshAttributeMapsTool"


/*
 * ToolBuilder
 */


bool UBakeMeshAttributeMapsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 2;
}

UInteractiveTool* UBakeMeshAttributeMapsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UBakeMeshAttributeMapsTool* NewTool = NewObject<UBakeMeshAttributeMapsTool>(SceneState.ToolManager);
	NewTool->SetAssetAPI(AssetAPI);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	TArray<TUniquePtr<FPrimitiveComponentTarget>> MeshComponents;
	MeshComponents.Add( MakeComponentTarget(Cast<UPrimitiveComponent>(Components[0])) );
	MeshComponents.Add( MakeComponentTarget(Cast<UPrimitiveComponent>(Components[1])) );

	NewTool->SetSelection(MoveTemp(MeshComponents));
	return NewTool;
}



/*
 * Tool
 */

UBakeMeshAttributeMapsTool::UBakeMeshAttributeMapsTool()
{
}


void UBakeMeshAttributeMapsTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	AssetAPI = AssetAPIIn;
}

void UBakeMeshAttributeMapsTool::Setup()
{
	UInteractiveTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTargets[0]->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTargets[0]->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTargets[0]->GetWorldTransform());

	// transfer materials
	FComponentMaterialSet MaterialSet;
	ComponentTargets[0]->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		DynamicMeshComponent->SetMaterial(k, MaterialSet.Materials[k]);
	}

	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTargets[0]->GetMesh());
	
	BaseMesh.Copy(*DynamicMeshComponent->GetMesh());
	BaseSpatial.SetMesh(&BaseMesh, true);

	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTargets[1]->GetMesh(), DetailMesh);
	DetailSpatial.SetMesh(&DetailMesh, true);


	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolset/Materials/BakePreviewMaterial"));
	check(Material);
	if (Material != nullptr)
	{
		PreviewMaterial = UMaterialInstanceDynamic::Create(Material, GetToolManager());
		DynamicMeshComponent->SetOverrideRenderMaterial(PreviewMaterial);
	}

	// hide input StaticMeshComponent
	ComponentTargets[0]->SetOwnerVisibility(false);
	//ComponentTargets[1]->SetOwnerVisibility(false);

	Settings = NewObject<UBakeMeshAttributeMapsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	OcclusionMapProps = NewObject<UBakedOcclusionMapToolProperties>(this);
	OcclusionMapProps->RestoreProperties(this);
	AddToolPropertySource(OcclusionMapProps);

	NormalMapProps = NewObject<UBakedNormalMapToolProperties>(this);
	NormalMapProps->RestoreProperties(this);
	AddToolPropertySource(NormalMapProps);

	Settings->WatchProperty(Settings->Resolution, [this](EBakeTextureResolution) { InvalidateNormals(); InvalidateOcclusion(); });
	Settings->WatchProperty(Settings->bNormalMap, [this](bool) { InvalidateNormals(); });
	Settings->WatchProperty(Settings->bAmbientOcclusionMap, [this](bool) { InvalidateOcclusion(); });

	OcclusionMapProps->WatchProperty(OcclusionMapProps->OcclusionRays, [this](int32) { InvalidateOcclusion(); });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->MaxDistance, [this](float) { InvalidateOcclusion(); });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->BlurRadius, [this](float) { InvalidateOcclusion(); });
	OcclusionMapProps->WatchProperty(OcclusionMapProps->bGaussianBlur, [this](float) { InvalidateOcclusion(); });

	VisualizationProps = NewObject<UBakedOcclusionMapVisualizationProperties>(this);
	VisualizationProps->RestoreProperties(this);
	AddToolPropertySource(VisualizationProps);

	InitializeEmptyMaps();

	bResultValid = false;

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Bake Normal and AO Maps. Select Bake Mesh (LowPoly) first, then Detail Mesh second. Texture Assets will be created on Accept. "),
		EToolMessageLevel::UserNotification);
}


void UBakeMeshAttributeMapsTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	OcclusionMapProps->SaveProperties(this);
	NormalMapProps->SaveProperties(this);
	VisualizationProps->SaveProperties(this);

	if (DynamicMeshComponent != nullptr)
	{
		ComponentTargets[0]->SetOwnerVisibility(true);
		ComponentTargets[1]->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			FString BaseName = ComponentTargets[0]->GetOwnerActor()->GetName();

			if (AssetAPI != nullptr)
			{
				if (Settings->bNormalMap && NormalMapProps->Result != nullptr)
				{
					FTexture2DBuilder::CopyPlatformDataToSourceData(NormalMapProps->Result, FTexture2DBuilder::ETextureType::NormalMap);
					bool bOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, NormalMapProps->Result,
						FString::Printf(TEXT("%s_Normals"), *BaseName));
					check(bOK);
				}

				if (Settings->bAmbientOcclusionMap && OcclusionMapProps->Result != nullptr)
				{
					FTexture2DBuilder::CopyPlatformDataToSourceData(OcclusionMapProps->Result, FTexture2DBuilder::ETextureType::AmbientOcclusion);
					bool bOK = AssetGenerationUtil::SaveGeneratedTexture2D(AssetAPI, OcclusionMapProps->Result,
						FString::Printf(TEXT("%s_Occlusion"), *BaseName));
					check(bOK);
				}
			}

		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}


void UBakeMeshAttributeMapsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();

	float GrayLevel = VisualizationProps->BaseGrayLevel;
	PreviewMaterial->SetVectorParameterValue(TEXT("BaseColor"), FVector(GrayLevel, GrayLevel, GrayLevel) );
	float AOWeight = VisualizationProps->OcclusionMultiplier;
	PreviewMaterial->SetScalarParameterValue(TEXT("AOWeight"), AOWeight );

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = ComponentTargets[0]->GetWorldTransform();
}





void UBakeMeshAttributeMapsTool::InvalidateOcclusion()
{
	bResultValid = false;
}

void UBakeMeshAttributeMapsTool::InvalidateNormals()
{
	bResultValid = false;
}






/**
 * Find point on Detail mesh that corresponds to point on Base mesh.
 * If nearest point on Detail mesh is within DistanceThreshold, uses that point (cleanly handles coplanar/etc).
 * Otherwise casts a ray in Normal direction.
 * If Normal-direction ray misses, use reverse direction.
 * If both miss, we return false, no correspondence found
 */
static bool GetDetailTrianglePoint(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	const FVector3d& BaseNormal,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords,
	double DistanceThreshold = FMathf::ZeroTolerance * 100.0f)
{
	// check if we are within on-surface tolerance, if so we use nearest point
	IMeshSpatial::FQueryOptions OnSurfQueryOptions;
	OnSurfQueryOptions.MaxDistance = DistanceThreshold;
	double NearDistSqr = 0;
	int32 NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr, OnSurfQueryOptions);
	if (DetailMesh.IsTriangle(NearestTriID))
	{
		DetailTriangleOut = NearestTriID;
		FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
		DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
		return true;
	}

	// TODO: should we check normals here? inverse normal should probably not be considered valid

	// shoot rays forwards and backwards
	FRay3d Ray(BasePoint, BaseNormal), BackwardsRay(BasePoint, -BaseNormal);
	int32 HitTID = IndexConstants::InvalidID, BackwardHitTID = IndexConstants::InvalidID;
	double HitDist, BackwardHitDist;
	bool bHitForward = DetailSpatial.FindNearestHitTriangle(Ray, HitDist, HitTID);
	bool bHitBackward = DetailSpatial.FindNearestHitTriangle(BackwardsRay, BackwardHitDist, BackwardHitTID);

	// use the backwards hit if it is closer than the forwards hit
	if ( (bHitBackward && bHitForward == false) || (bHitForward && bHitBackward && BackwardHitDist < HitDist))
	{
		Ray = BackwardsRay;
		HitTID = BackwardHitTID;
		HitDist = BackwardHitDist;
	}

	// if we got a valid ray hit, use it
	if (DetailMesh.IsTriangle(HitTID))
	{
		DetailTriangleOut = HitTID;
		FIntrRay3Triangle3d IntrQuery = TMeshQueries<FDynamicMesh3>::TriangleIntersection(DetailMesh, HitTID, Ray);
		DetailTriBaryCoords = IntrQuery.TriangleBaryCoords;
		return true;
	}

	// if we get this far, both rays missed, so use absolute nearest point regardless of distance
	//NearestTriID = DetailSpatial.FindNearestTriangle(BasePoint, NearDistSqr);
	//if (DetailMesh.IsTriangle(NearestTriID))
	//{
	//	DetailTriangleOut = NearestTriID;
	//	FDistPoint3Triangle3d DistQuery = TMeshQueries<FDynamicMesh3>::TriangleDistance(DetailMesh, NearestTriID, BasePoint);
	//	DetailTriBaryCoords = DistQuery.TriangleBaryCoords;
	//	return true;
	//}

	return false;
}




// Information about Base/Detail point correspondence
struct FDetailPointSample
{
	FMeshUVSampleInfo BaseSample;
	FVector3d BaseNormal;

	int32 DetailTriID;
	FVector3d DetailBaryCoords;
};


void UBakeMeshAttributeMapsTool::UpdateResult()
{
	if (bResultValid) 
	{
		return;
	}

	int32 ImageSize = (int32)Settings->Resolution;
	FImageDimensions Dimensions(ImageSize, ImageSize);

	FNormalMapSettings NormalMapSettings;
	NormalMapSettings.Dimensions = Dimensions;
	bool bBakeNormals = Settings->bNormalMap &&  ! (CachedNormalMapSettings == NormalMapSettings);


	FOcclusionMapSettings OcclusionMapSettings;
	OcclusionMapSettings.Dimensions = Dimensions;
	OcclusionMapSettings.MaxDistance = (OcclusionMapProps->MaxDistance == 0) ? TNumericLimits<float>::Max() : OcclusionMapProps->MaxDistance;
	OcclusionMapSettings.OcclusionRays = OcclusionMapProps->OcclusionRays;
	OcclusionMapSettings.BlurRadius = (OcclusionMapProps->bGaussianBlur) ? OcclusionMapProps->BlurRadius : 0.0;
	bool bBakeOcclusion = Settings->bAmbientOcclusionMap && ! (CachedOcclusionMapSettings == OcclusionMapSettings);

	// if we have nothing to do, we can early-out
	if (bBakeNormals == false && bBakeOcclusion == false)
	{
		UpdateVisualization();
		GetToolManager()->PostInvalidation();
		bResultValid = true;
		return;
	}

	const FDynamicMesh3* Mesh = &BaseMesh;
	const FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->GetUVLayer(0);
	const FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->PrimaryNormals();

	// calculate tangents
	FMeshTangentsd Tangents(Mesh);
	Tangents.ComputeTriVertexTangents(NormalOverlay, UVOverlay, FComputeTangentsOptions());

	const FDynamicMeshNormalOverlay* DetailNormalOverlay = DetailMesh.Attributes()->GetNormalLayer(0);
	check(DetailNormalOverlay);

	// this sampler finds the correspondence between base surface and detail surface
	TMeshSurfaceUVSampler<FDetailPointSample> DetailMeshSampler;
	DetailMeshSampler.Initialize(Mesh, UVOverlay, EMeshSurfaceSamplerQueryType::TriangleAndUV, FDetailPointSample(),
		[Mesh, NormalOverlay, this](const FMeshUVSampleInfo& SampleInfo, FDetailPointSample& ValueOut)
	{
		//FVector3d BaseTriNormal = Mesh->GetTriNormal(SampleInfo.TriangleIndex);
		NormalOverlay->GetTriBaryInterpolate<double>(SampleInfo.TriangleIndex, &SampleInfo.BaryCoords[0], &ValueOut.BaseNormal[0]);
		FVector3d RayDir = ValueOut.BaseNormal;

		ValueOut.BaseSample = SampleInfo;

		// find detail mesh triangle point
		bool bFoundTri = GetDetailTrianglePoint(DetailMesh, DetailSpatial, SampleInfo.SurfacePoint, RayDir,
			ValueOut.DetailTriID, ValueOut.DetailBaryCoords);
		if (!bFoundTri)
		{
			ValueOut.DetailTriID = FDynamicMesh3::InvalidID;
		}
	});


	// sample normal of detail surface in tangent-space of base surface
	auto NormalSampleFunction = [&](const FDetailPointSample& SampleData)
	{
		int32 DetailTriID = SampleData.DetailTriID;
		if (DetailMesh.IsTriangle(DetailTriID))
		{
			// get tangents on base mesh
			FVector3d BaseTangentX, BaseTangentY;
			Tangents.GetInterpolatedTriangleTangent(SampleData.BaseSample.TriangleIndex, SampleData.BaseSample.BaryCoords, BaseTangentX, BaseTangentY);

			FVector3d DetailNormal;
			DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTriID, &SampleData.DetailBaryCoords[0], &DetailNormal[0]);
			double dx = DetailNormal.Dot(BaseTangentX);
			double dy = DetailNormal.Dot(BaseTangentY);
			double dz = DetailNormal.Dot(SampleData.BaseNormal);
			return (FVector3f)FVector3d(dx, dy, dz);
		}
		return FVector3f::UnitZ();
	};


	// precompute ray directions for AO
	TSphericalFibonacci<double> Points(2 * OcclusionMapSettings.OcclusionRays);
	TArray<FVector3d> RayDirections;
	for (int32 k = 0; k < Points.Num(); ++k)
	{
		FVector3d P = Points[k];
		if (P.Z > 0)
		{
			RayDirections.Add(P.Normalized());
		}
	}

	// 
	FRandomStream RotationGen(31337);
	FCriticalSection RotationLock;
	auto GetRandomRotation = [&RotationGen, &RotationLock]() {
		RotationLock.Lock();
		double Angle = RotationGen.GetFraction() * FMathd::TwoPi;
		RotationLock.Unlock();
		return Angle;
	};


	auto OcclusionSampleFunction = [&](const FDetailPointSample& SampleData)
	{
		int32 DetailTriID = SampleData.DetailTriID;
		if (DetailMesh.IsTriangle(DetailTriID))
		{
			FIndex3i DetailTri = DetailMesh.GetTriangle(DetailTriID);
			FVector3d DetailTriNormal = DetailMesh.GetTriNormal(DetailTriID);
			//FVector3d DetailNormal;
			//DetailNormalOverlay->GetTriBaryInterpolate<double>(DetailTID, &DetailTriBaryCoords[0], &DetailNormal[0]);

			FVector3d DetailBaryCoords = SampleData.DetailBaryCoords;
			FVector3d DetailPos = DetailMesh.GetTriBaryPoint(DetailTriID, DetailBaryCoords.X, DetailBaryCoords.Y, DetailBaryCoords.Z);
			DetailPos += 10.0f * FMathf::ZeroTolerance * DetailTriNormal;
			FFrame3d SurfaceFrame(DetailPos, DetailTriNormal);

			double RotationAngle = GetRandomRotation();
			SurfaceFrame.Rotate(FQuaterniond(SurfaceFrame.Z(), RotationAngle, false));

			IMeshSpatial::FQueryOptions QueryOptions;
			//QueryOptions.MaxDistance = OcclusionMapSettings.OcclusionRays;

			double AccumOcclusion = 0;
			for (FVector3d SphereDir : RayDirections)
			{
				FRay3d OcclusionRay(DetailPos, SurfaceFrame.FromFrameVector(SphereDir));
				check(OcclusionRay.Direction.Dot(DetailTriNormal) > 0);
				//int32 HitTriangleID;
				if ( DetailSpatial.TestAnyHitTriangle(OcclusionRay, QueryOptions) )
				{
					AccumOcclusion += 1.0;
				}
			}

			AccumOcclusion /= (double)RayDirections.Num();
			return AccumOcclusion;
		}
		return 0.0;
	};

	// make UV-space version of mesh
	FDynamicMesh3 FlatMesh(EMeshComponents::FaceGroups);
	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		if (UVOverlay->IsSetTriangle(tid))
		{
			FVector2f A, B, C;
			UVOverlay->GetTriElements(tid, A, B, C);
			int32 VertA = FlatMesh.AppendVertex(FVector3d(A.X, A.Y, 0));
			int32 VertB = FlatMesh.AppendVertex(FVector3d(B.X, B.Y, 0));
			int32 VertC = FlatMesh.AppendVertex(FVector3d(C.X, C.Y, 0));
			int32 NewTriID = FlatMesh.AppendTriangle(VertA, VertB, VertC, tid);
		}
	}

	// calculate occupancy map
	FImageOccupancyMap Occupancy;
	Occupancy.Initialize(Dimensions);
	Occupancy.ComputeFromUVSpaceMesh(FlatMesh, [&](int32 TriangleID) { return FlatMesh.GetTriangleGroup(TriangleID); } );

	// initialize the textures
	//FTexture2DBuilder ColorBuilder;
	//ColorBuilder.Initialize(FTexture2DBuilder::ETextureType::Color, Dimensions);
	FTexture2DBuilder OcclusionBuilder;
	if (bBakeOcclusion)
	{
		OcclusionBuilder.Initialize(FTexture2DBuilder::ETextureType::AmbientOcclusion, Dimensions);
	}
	FTexture2DBuilder NormalsBuilder;
	if (bBakeNormals)
	{
		NormalsBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, Dimensions);
	}

	// calculate interior texels
	ParallelFor(Dimensions.Num(), [&](int64 LinearIdx)
	{
		if (Occupancy.IsInterior(LinearIdx) == false)
		{
			return;
		}

		FVector2d UVPosition = (FVector2d)Occupancy.TexelQueryUV[LinearIdx];
		int32 UVTriangleID = Occupancy.TexelQueryTriangle[LinearIdx];

		FDetailPointSample DetailInfo;
		DetailMeshSampler.SampleUV(UVTriangleID, UVPosition, DetailInfo);

		// calculate normal
		if (bBakeNormals)
		{
			FVector3f RelativeDetailNormal = NormalSampleFunction(DetailInfo);
			FVector3f MapNormal = (RelativeDetailNormal + FVector3f::One()) * 0.5;
			NormalsBuilder.SetTexel(LinearIdx, ((FLinearColor)MapNormal).ToFColor(false));
		}

		// todo: calculate color?
		//ColorBuilder.SetTexel(LinearIdx, FColor(128, 128, 128));
		//ColorBuilder.SetTexel(LinearIdx, FColor(192, 192, 192));

		// calculate occlusion
		if (bBakeOcclusion)
		{
			double Occlusion = OcclusionSampleFunction(DetailInfo);
			FVector3d OcclusionColor = FMathd::Clamp(1.0 - Occlusion, 0.0, 1.0) * FVector3d::One();
			OcclusionBuilder.SetTexel(LinearIdx, ((FLinearColor)OcclusionColor).ToFColor(false));
		}
	});


	// fill in the gutter texels
	for (int64 k = 0; k < Occupancy.GutterTexels.Num(); k++)
	{
		TPair<int64, int64> GutterTexel = Occupancy.GutterTexels[k];
		//ColorBuilder.CopyTexel(GutterTexel.Value, GutterTexel.Key);
		if (bBakeNormals)
		{
			NormalsBuilder.CopyTexel(GutterTexel.Value, GutterTexel.Key);
			//NormalsBuilder.ClearTexel(GutterTexel.Key);
		}
		if (bBakeOcclusion)
		{
			OcclusionBuilder.CopyTexel(GutterTexel.Value, GutterTexel.Key);
		}
	}

	// apply AO blur pass
	if (bBakeOcclusion && OcclusionMapSettings.BlurRadius > 0.01)
	{
		TDiscreteKernel2f BlurKernel2d;
		TGaussian2f::MakeKernelFromRadius(OcclusionMapSettings.BlurRadius, BlurKernel2d);
		TArray<float> AOBlurBuffer;
		Occupancy.ParallelProcessingPass<float>(
			[&](int64 Index) { return 0.0f; },
			[&](int64 LinearIdx, float Weight, float& CurValue) { CurValue += Weight * (float)OcclusionBuilder.GetTexel(LinearIdx).R; },
			[&](int64 LinearIdx, float WeightSum, float& CurValue) { CurValue /= WeightSum; },
			[&](int64 LinearIdx, float& CurValue) { uint8 Val = FMath::Clamp(CurValue, 0.0f, 255.0f); OcclusionBuilder.SetTexel(LinearIdx, FColor(Val,Val,Val)); },
			[&](const FVector2i& TexelOffset) { return BlurKernel2d.EvaluateFromOffset(TexelOffset); },
			BlurKernel2d.IntRadius,
			AOBlurBuffer);
	}


	// Unlock the textures and update them
	//ColorBuilder.Commit();
	if (bBakeNormals)
	{
		NormalsBuilder.Commit(false);
		CachedNormalMap = NormalsBuilder.GetTexture2D();
		CachedNormalMapSettings = NormalMapSettings;
	}

	if (bBakeOcclusion)
	{
		OcclusionBuilder.Commit(false);
		CachedOcclusionMap = OcclusionBuilder.GetTexture2D();
		CachedOcclusionMapSettings = OcclusionMapSettings;
	}

	UpdateVisualization();
	GetToolManager()->PostInvalidation();

	bResultValid = true;
}




void UBakeMeshAttributeMapsTool::UpdateVisualization()
{
	//if (BakeColor != nullptr)
	//{
	//	PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), BakeColor);
	//}

	if (Settings->bNormalMap)
	{
		NormalMapProps->Result = CachedNormalMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), CachedNormalMap);
	}
	else
	{
		NormalMapProps->Result = nullptr;
		PreviewMaterial->SetTextureParameterValue(TEXT("NormalMap"), EmptyNormalMap);
	}
	
	if (Settings->bAmbientOcclusionMap)
	{
		OcclusionMapProps->Result = CachedOcclusionMap;
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), CachedOcclusionMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), CachedOcclusionMap);
	}
	else
	{
		OcclusionMapProps->Result = nullptr;
		PreviewMaterial->SetTextureParameterValue(TEXT("OcclusionMap"), EmptyOcclusionMap);
		PreviewMaterial->SetTextureParameterValue(TEXT("ColorMap"), EmptyOcclusionMap);
	}
}



bool UBakeMeshAttributeMapsTool::HasAccept() const
{
	return true;
}

bool UBakeMeshAttributeMapsTool::CanAccept() const
{
	return true;
}



void UBakeMeshAttributeMapsTool::InitializeEmptyMaps()
{
	FTexture2DBuilder OcclusionBuilder;
	OcclusionBuilder.Initialize(FTexture2DBuilder::ETextureType::AmbientOcclusion, FImageDimensions(16,16));
	OcclusionBuilder.Commit(false);
	EmptyOcclusionMap = OcclusionBuilder.GetTexture2D();

	FTexture2DBuilder NormalsBuilder;
	NormalsBuilder.Initialize(FTexture2DBuilder::ETextureType::NormalMap, FImageDimensions(16, 16));
	NormalsBuilder.Commit(false);
	EmptyNormalMap = NormalsBuilder.GetTexture2D();
}



#undef LOCTEXT_NAMESPACE
