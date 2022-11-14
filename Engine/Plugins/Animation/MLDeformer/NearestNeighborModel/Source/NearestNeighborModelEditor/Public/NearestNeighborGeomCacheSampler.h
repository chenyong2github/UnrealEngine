// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerGeomCacheSampler.h"

namespace UE::NearestNeighborModel
{
	class NEARESTNEIGHBORMODELEDITOR_API FNearestNeighborGeomCacheSampler
		: public UE::MLDeformer::FMLDeformerGeomCacheSampler
	{
	public:
		virtual uint8 SamplePart(int32 InAnimFrameIndex, int32 PartId);
		virtual void SampleKMeansAnim(const int32 SkeletonId);
		virtual void SampleKMeansFrame(const int32 Frame);
		const TArray<float>& GetPartVertexDeltas() const { return PartVertexDeltas; }
		uint8 GeneratePartMeshMappings(const TArray<uint32>& VertexMap, bool bUsePartOnlyMesh);
		uint8 GenerateMeshMappingIndices();

		bool IsMeshMappingsEmpty() const { return MeshMappings.IsEmpty(); }
	protected:
		TArray<float> PartVertexDeltas;
		TArray<int32> MeshMappingIndices;
	};
}
