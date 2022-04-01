// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerSampler.h"
#include "MLDeformerModel.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "UObject/ObjectPtr.h"
#include "GeometryCacheMeshData.h"

class UGeometryCacheComponent;
class UGeometryCache;

namespace UE::MLDeformer
{
	DECLARE_DELEGATE_RetVal(UGeometryCache*, FMLDeformerGetGeomCacheEvent)

	/**
	 * The input data sampler.
	 * This class can sample bone rotations, curve values and vertex deltas.
	 * It does this by creating two temp actors, one with skeletal mesh component and one with geom cache component.
	 */
	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerGeomCacheSampler
		: public FMLDeformerSampler
	{
	public:
		// FVertexDeltaSampler overrides.
		virtual void Sample(int32 AnimFrameIndex) override;
		virtual void RegisterTargetComponents() override;
		virtual float GetTimeAtFrame(int32 InAnimFrameIndex) const override;
		// ~END FVertexDeltaSampler overrides.

		const TArray<FString>& GetFailedImportedMeshNames() const { return FailedImportedMeshNames; }

		FMLDeformerGetGeomCacheEvent& OnGetGeometryCache() { return GetGeometryCacheEvent; }

	protected:
		void CalculateVertexDeltas(const TArray<FVector3f>& SkinnedPositions, float DeltaCutoffLength, TArray<float>& OutVertexDeltas);

	protected:
		/** The geometry cache component used to sample the geometry cache. */
		TObjectPtr<UGeometryCacheComponent> GeometryCacheComponent = nullptr;

		/** Maps skeletal meshes imported meshes to geometry tracks. */
		TArray<FMLDeformerGeomCacheMeshMapping> MeshMappings;

		/** The geometry cache mesh data reusable buffers. One for each MeshMapping.*/
		TArray<FGeometryCacheMeshData> GeomCacheMeshDatas;

		/** Imported mesh names in the skeletal mesh for which no geom cache track could be found. */
		TArray<FString> FailedImportedMeshNames; 

		/** The function that grabs the geometry cache. */
		FMLDeformerGetGeomCacheEvent GetGeometryCacheEvent;
	};
}	// namespace UE::MLDeformer
