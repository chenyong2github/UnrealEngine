// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModule.h"

class UGeometryCache;
class USkeletalMesh;
class UAnimSequence;

namespace UE::MLDeformer
{
#if WITH_EDITORONLY_DATA
	/** Maps SkeletalMesh imported meshes to geometry cache tracks. */
	struct FMLDeformerGeomCacheMeshMapping
	{
		int32 MeshIndex = INDEX_NONE;	// The imported model's mesh info index.
		int32 TrackIndex = INDEX_NONE;	// The geom cache track that this mesh is mapped to.
		TArray<int32> SkelMeshToTrackVertexMap;	// This maps imported model individual meshes to the geomcache track's mesh data.
		TArray<int32> ImportedVertexToRenderVertexMap; // Map the imported dcc vertex number to a render vertex. This is just one of the duplicates, which shares the same position.
	};

	// Error checks.
	MLDEFORMERFRAMEWORK_API FText GetGeomCacheErrorText(USkeletalMesh* InSkeletalMesh, UGeometryCache* InGeomCache);
	MLDEFORMERFRAMEWORK_API FText GetGeomCacheAnimSequenceErrorText(UGeometryCache* InGeomCache, UAnimSequence* InAnimSequence);
	MLDEFORMERFRAMEWORK_API FText GetGeomCacheMeshMappingErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache);

	// Geom cache operations.
	MLDEFORMERFRAMEWORK_API int32 ExtractNumImportedGeomCacheVertices(UGeometryCache* GeometryCache);
	MLDEFORMERFRAMEWORK_API void GenerateGeomCacheMeshMappings(USkeletalMesh* SkelMesh, UGeometryCache* GeomCache, TArray<FMLDeformerGeomCacheMeshMapping>& OutMeshMappings, TArray<FString>& OutFailedImportedMeshNames, TArray<FString>& OutVertexMisMatchNames);
	MLDEFORMERFRAMEWORK_API void SampleGeomCachePositions(
		int32 InLODIndex,
		float InSampleTime,
		const TArray<FMLDeformerGeomCacheMeshMapping>& InMeshMappings,
		const USkeletalMesh* SkelMesh,
		const UGeometryCache* InGeometryCache,
		const FTransform& AlignmentTransform,
		TArray<FVector3f>& OutPositions);
#endif	// #if WITH_EDITORONLY_DATA
}	// namespace UE::MLDeformer
