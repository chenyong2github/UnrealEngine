// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshDescriptionHelper.h"

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshAttributes.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "RenderUtils.h"
#include "IMeshReductionInterfaces.h"
#include "IMeshReductionManagerModule.h"
#include "Materials/Material.h"
#include "RawMesh.h"
#include "BuildStatisticManager.h"
#include "MeshDescriptionOperations.h"
#include "Modules/ModuleManager.h"

//Enable all check
//#define ENABLE_NTB_CHECK

DEFINE_LOG_CATEGORY(LogMeshDescriptionBuildStatistic);

FMeshDescriptionHelper::FMeshDescriptionHelper(FMeshBuildSettings* InBuildSettings)
	: BuildSettings(InBuildSettings)
{
}

void FMeshDescriptionHelper::SetupRenderMeshDescription(UObject* Owner, FMeshDescription& RenderMeshDescription)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshDescriptionHelper::GetRenderMeshDescription);

	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Owner);
	check(StaticMesh);

	float ComparisonThreshold = BuildSettings->bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
	
	//This function make sure the Polygon NTB are compute and also remove degenerated triangle from the render mesh description.
	FMeshDescriptionOperations::CreatePolygonNTB(RenderMeshDescription, ComparisonThreshold);
	//OutRenderMeshDescription->ComputePolygonTangentsAndNormals(BuildSettings->bRemoveDegenerates ? SMALL_NUMBER : 0.0f);

	FVertexInstanceArray& VertexInstanceArray = RenderMeshDescription.VertexInstances();
	TVertexInstanceAttributesRef<FVector> Normals = RenderMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>( MeshAttribute::VertexInstance::Normal );
	TVertexInstanceAttributesRef<FVector> Tangents = RenderMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>( MeshAttribute::VertexInstance::Tangent );
	TVertexInstanceAttributesRef<float> BinormalSigns = RenderMeshDescription.VertexInstanceAttributes().GetAttributesRef<float>( MeshAttribute::VertexInstance::BinormalSign );

	// Find overlapping corners to accelerate adjacency.
	FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, RenderMeshDescription, ComparisonThreshold);

	// Compute any missing normals or tangents.
	{
		// Static meshes always blend normals of overlapping corners.
		uint32 TangentOptions = FMeshDescriptionOperations::ETangentOptions::BlendOverlappingNormals;
		if (BuildSettings->bRemoveDegenerates)
		{
			// If removing degenerate triangles, ignore them when computing tangents.
			TangentOptions |= FMeshDescriptionOperations::ETangentOptions::IgnoreDegenerateTriangles;
		}
		if (BuildSettings->bComputeWeightedNormals)
		{
			// Specify we want to compute weighted normals.
			TangentOptions |= FMeshDescriptionOperations::ETangentOptions::UseWeightedAreaAndAngle;
		}
		
		//Keep the original mesh description NTBs if we do not rebuild the normals or tangents.
		bool bComputeTangentLegacy = !BuildSettings->bUseMikkTSpace && (BuildSettings->bRecomputeNormals || BuildSettings->bRecomputeTangents);
		bool bHasAllNormals = true;
		bool bHasAllTangents = true;
		for (const FVertexInstanceID VertexInstanceID : VertexInstanceArray.GetElementIDs())
		{
			// Dump normals and tangents if we are recomputing them.
			if (BuildSettings->bRecomputeTangents)
			{
				//Dump the tangents
				BinormalSigns[VertexInstanceID] = 0.0f;
				Tangents[VertexInstanceID] = FVector(0.0f);
			}
			if (BuildSettings->bRecomputeNormals)
			{
				//Dump the normals
				Normals[VertexInstanceID] = FVector(0.0f);
			}
			bHasAllNormals &= !Normals[VertexInstanceID].IsNearlyZero();
			bHasAllTangents &= !Tangents[VertexInstanceID].IsNearlyZero();
		}


		//MikkTSpace should be use only when the user want to recompute the normals or tangents otherwise should always fallback on builtin
		//We cannot use mikkt space with degenerated normals fallback on buitin.
		if (BuildSettings->bUseMikkTSpace && (BuildSettings->bRecomputeNormals || BuildSettings->bRecomputeTangents))
		{
			if (!bHasAllNormals)
			{
				FMeshDescriptionOperations::CreateNormals(RenderMeshDescription, (FMeshDescriptionOperations::ETangentOptions)TangentOptions, false);

				//EComputeNTBsOptions ComputeNTBsOptions = EComputeNTBsOptions::Normals;
				//OutRenderMeshDescription.ComputeTangentsAndNormals(ComputeNTBsOptions);
			}
			FMeshDescriptionOperations::CreateMikktTangents(RenderMeshDescription, (FMeshDescriptionOperations::ETangentOptions)TangentOptions);
		}
		else
		{
			FMeshDescriptionOperations::CreateNormals(RenderMeshDescription, (FMeshDescriptionOperations::ETangentOptions)TangentOptions, true);
			//EComputeNTBsOptions ComputeNTBsOptions = (bHasAllNormals ? EComputeNTBsOptions::None : EComputeNTBsOptions::Normals) | (bHasAllTangents ? EComputeNTBsOptions::None : EComputeNTBsOptions::Tangents);
			//OutRenderMeshDescription.ComputeTangentsAndNormals(ComputeNTBsOptions);
		}
	}

	if (BuildSettings->bGenerateLightmapUVs && VertexInstanceArray.Num() > 0)
	{
		TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = RenderMeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		int32 NumIndices = VertexInstanceUVs.GetNumIndices();
		//Verify the src light map channel
		if (BuildSettings->SrcLightmapIndex >= NumIndices)
		{
			BuildSettings->SrcLightmapIndex = 0;
		}
		//Verify the destination light map channel
		if (BuildSettings->DstLightmapIndex >= NumIndices)
		{
			//Make sure we do not add illegal UV Channel index
			if (BuildSettings->DstLightmapIndex >= MAX_MESH_TEXTURE_COORDS_MD)
			{
				BuildSettings->DstLightmapIndex = MAX_MESH_TEXTURE_COORDS_MD - 1;
			}

			//Add some unused UVChannel to the mesh description for the lightmapUVs
			VertexInstanceUVs.SetNumIndices(BuildSettings->DstLightmapIndex + 1);
			BuildSettings->DstLightmapIndex = NumIndices;
		}
		FMeshDescriptionOperations::CreateLightMapUVLayout(RenderMeshDescription,
			BuildSettings->SrcLightmapIndex,
			BuildSettings->DstLightmapIndex,
			BuildSettings->MinLightmapResolution,
			(ELightmapUVVersion)(StaticMesh->LightmapUVVersion),
			OverlappingCorners);
	}
}

void FMeshDescriptionHelper::ReduceLOD(const FMeshDescription& BaseMesh, FMeshDescription& DestMesh, const FMeshReductionSettings& ReductionSettings, const FOverlappingCorners& InOverlappingCorners, float &OutMaxDeviation)
{
	IMeshReductionManagerModule& MeshReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface");
	IMeshReduction* MeshReduction = MeshReductionModule.GetStaticMeshReductionInterface();

	
	if (!MeshReduction)
	{
		// no reduction possible
		OutMaxDeviation = 0.f;
		return;
	}

	OutMaxDeviation = ReductionSettings.MaxDeviation;
	MeshReduction->ReduceMeshDescription(DestMesh, OutMaxDeviation, BaseMesh, InOverlappingCorners, ReductionSettings);
}

void FMeshDescriptionHelper::FindOverlappingCorners(const FMeshDescription& MeshDescription, float ComparisonThreshold)
{
	FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, MeshDescription, ComparisonThreshold);
}

