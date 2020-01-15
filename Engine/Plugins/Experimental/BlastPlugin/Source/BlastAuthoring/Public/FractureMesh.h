// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "EditableMesh.h"
#include "MeshFractureSettings.h"
#include "NvBlastExtAuthoringFractureTool.h"
#include "GeneratedFracturedChunk.h"
#include "FractureMesh.generated.h"

struct NoiseConfiguration;

DECLARE_LOG_CATEGORY_EXTERN(LogFractureMesh, Log, All);

/** Random Generator class implementation required by Blast, based on Nv::Blast::RandomGeneratorBase */
class FractureRandomGenerator : public Nv::Blast::RandomGeneratorBase
{
public:
	FractureRandomGenerator(int32_t RandomSeed)
	{
		seed(RandomSeed);
	};

	virtual ~FractureRandomGenerator() {};

	virtual float getRandomValue() override
	{
		return RandStream.GetFraction();
	}
	virtual void seed(int32_t RandomSeed) override
	{
		RandStream.Initialize(RandomSeed);
	}

private:
	FRandomStream RandStream;

};

/** Performs Voronoi or Slicing fracture of the currently selected mesh */
UCLASS()
class BLASTAUTHORING_API UFractureMesh : public UObject
{
	GENERATED_BODY()
public:
	/** Performs fracturing of an Editable Mesh */
	bool FractureMesh(const UEditableMesh* SourceMesh, const FString& ParentName, const UMeshFractureSettings& FractureSettings, int32 FracturedChunkIndex, const FTransform& Transform, int RandomSeed, UGeometryCollection* FracturedGeometryCollection, TArray<FGeneratedFracturedChunk>& GeneratedChunksOut, TArray<int32>& DeletedChunksOut, FBox WorldBounds, const FVector& InBoundsOffset);

	/** ensure node hierarchy is setup appropriately */
	void FixupHierarchy(int32 FracturedChunkIndex, class UGeometryCollection* GeometryCollectionObject, FGeneratedFracturedChunk& GeneratedChunk, const FString& Name);

private:
	const float MagicScaling = 100.0f;

#if PLATFORM_WINDOWS
	/** Generate geometry for all the bones of the geometry collection */
	bool GenerateChunkMeshes(Nv::Blast::FractureTool* BlastFractureTool, const UMeshFractureSettings& FractureSettings, int32 FracturedChunkIndex, const FString& ParentName, const FTransform& ParentTransform, Nv::Blast::Mesh* BlastMesh, UGeometryCollection* FracturedGeometryCollection, TArray<FGeneratedFracturedChunk>& GeneratedChunksOut, TArray<int32>& DeletedChunksOut);

	/** Log some stats */
	void LogStatsAndTimings(const Nv::Blast::Mesh* BlastMesh, const Nv::Blast::FractureTool* BlastFractureTool, const FTransform& Transform, const UMeshFractureSettings& FractureSettings, float ProcessingTime);
#endif

	/** Get raw bitmap data from texture */
	void ExtractDataFromTexture(const TWeakObjectPtr<UTexture> SourceTexture, TArray<uint8>& RawDataOut, int32& WidthOut, int32& HeightOut);

#if PLATFORM_WINDOWS
	/** Draw debug render of exploded shape, i.e. all fracture chunks */
	void RenderDebugGraphics(Nv::Blast::FractureTool* BlastFractureTool, const UMeshFractureSettings& FractureSettings, const FTransform& Transform);

	/** Draws all edges of Blast Mesh as debug lines */
	void DrawDebugBlastMesh(const Nv::Blast::Mesh* ChunkMesh, int ChunkIndex, float ExplodedViewAmount, const FTransform& Transform);
#endif

	/** Generate uniform locations in a bounding box */
	void GenerateUniformSites(const FRandomStream &RandomStream, const FVector Offset, const FBox& Bounds, uint32 NumberToGenerate, TArray<FVector>& Sites) const;

	/** Generate locations in a radius around a point in space */
	void GenerateSitesInSphere(const FRandomStream &RandomStream, const FVector Offset, float Radius, uint32 NumberToGenerate, TArray<FVector>& Sites) const;

	/** Generate locations in a box */
	void ScatterInBounds(const FRandomStream& RandomStream, const FBox& Bounds, uint32 NumberToGenerate, TArray<FVector>& Positions, TArray<FVector>& Normals) const;

	/** Generate locations in a box */
	int32 PlaneCut(Nv::Blast::FractureTool* BlastFractureTool, const Nv::Blast::NoiseConfiguration& Noise, FractureRandomGenerator* RandomGenerator, const FVector& Position, const FVector& Normal, int32 ChunkID = 0) const;

	TArray<int32> GetChunkIDs(Nv::Blast::FractureTool* BlastFractureTool) const;
	TArray<int32> GetAddedIDs(const TArray<int32>& StartingArray, const TArray<int32>& EndingArray) const;
};
