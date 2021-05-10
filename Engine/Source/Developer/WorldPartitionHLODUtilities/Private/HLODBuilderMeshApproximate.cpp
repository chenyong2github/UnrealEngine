// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderMeshApproximate.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "Modules/ModuleManager.h"
#include "IGeometryProcessingInterfacesModule.h"
#include "GeometryProcessingInterfaces/ApproximateActors.h"


TArray<UPrimitiveComponent*> FHLODBuilder_MeshApproximate::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_MeshSimplify::CreateComponents);

	TSet<AActor*> Actors;
	Algo::Transform(InSubComponents, Actors, [](UPrimitiveComponent* InPrimitiveComponent) { return InPrimitiveComponent->GetOwner(); });

	IGeometryProcessingInterfacesModule& GeomProcInterfaces = FModuleManager::Get().LoadModuleChecked<IGeometryProcessingInterfacesModule>("GeometryProcessingInterfaces");
	IGeometryProcessing_ApproximateActors* ApproxActorsAPI = GeomProcInterfaces.GetApproximateActorsImplementation();

	//
	// Construct options for ApproximateActors operation
	//

	const FMeshApproximationSettings& UseSettings = InHLODLayer->GetMeshApproximationSettings();

	IGeometryProcessing_ApproximateActors::FOptions Options;
	Options.BasePackagePath = InHLODActor->GetPackage()->GetName();
	Options.WorldSpaceApproximationAccuracyMeters = UseSettings.ApproximationAccuracy;
	Options.ClampVoxelDimension = UseSettings.ClampVoxelDimension;
	Options.WindingThreshold = UseSettings.WindingThreshold;
	Options.bApplyMorphology = UseSettings.bFillGaps;
	Options.MorphologyDistanceMeters = UseSettings.GapDistance;
	Options.FixedTriangleCount = UseSettings.TargetTriCount;
	if (UseSettings.SimplifyMethod == EMeshApproximationSimplificationPolicy::TrianglesPerArea)
	{
		Options.MeshSimplificationPolicy = IGeometryProcessing_ApproximateActors::ESimplificationPolicy::TrianglesPerUnitSqMeter;
		Options.SimplificationTargetMetric = UseSettings.TrianglesPerM;
	}
	else
	{
		Options.MeshSimplificationPolicy = IGeometryProcessing_ApproximateActors::ESimplificationPolicy::FixedTriangleCount;
	}

	Options.TextureImageSize = UseSettings.MaterialSettings.TextureSize.X;
	Options.AntiAliasMultiSampling = FMath::Max(1, UseSettings.MultiSamplingAA);

	Options.RenderCaptureImageSize = (UseSettings.RenderCaptureResolution == 0) ?
		Options.TextureImageSize : UseSettings.RenderCaptureResolution;
	Options.FieldOfViewDegrees = UseSettings.CaptureFieldOfView;
	Options.NearPlaneDist = UseSettings.NearPlaneDist;

	// run actor approximation computation
	IGeometryProcessing_ApproximateActors::FResults Results;
	ApproxActorsAPI->ApproximateActors(Actors.Array(), Options, Results);

	auto FixupAsset = [&InHLODActor](UObject* Asset)
	{
		Asset->ClearFlags(RF_Public | RF_Standalone);
		Asset->Rename(nullptr, InHLODActor->GetPackage(), REN_NonTransactional | REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	};
	
	Algo::ForEach(Results.NewMeshAssets, FixupAsset);
	Algo::ForEach(Results.NewMaterials, FixupAsset);
	Algo::ForEach(Results.NewTextures, FixupAsset);

	TArray<UPrimitiveComponent*> Components;
	if (Results.ResultCode == IGeometryProcessing_ApproximateActors::EResultCode::Success)
	{
		for (UStaticMesh* StaticMesh : Results.NewMeshAssets)
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(InHLODActor);
			Component->SetStaticMesh(StaticMesh);
			DisableCollisions(Component);

			Components.Add(Component);
		}		
	}

	return Components;
}
