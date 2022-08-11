// Copyright Epic Games, Inc. All Rights Reserved.
#include "LegacyVertexDeltaModel.h"
#include "LegacyVertexDeltaModelVizSettings.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "UObject/Object.h"

#define LOCTEXT_NAMESPACE "ULegacyVertexDeltaModel"

ULegacyVertexDeltaModel::ULegacyVertexDeltaModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	VizSettings = ObjectInitializer.CreateEditorOnlyDefaultSubobject<ULegacyVertexDeltaModelVizSettings>(this, TEXT("VizSettings"));
#endif
}

#if WITH_EDITOR
	void ULegacyVertexDeltaModel::UpdateNumTargetMeshVertices()
	{
		NumTargetMeshVerts = UE::MLDeformer::ExtractNumImportedGeomCacheVertices(GeometryCache);
	}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	void ULegacyVertexDeltaModel::SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions)
	{
		ULegacyVertexDeltaModelVizSettings* VertexVizSettings = Cast<ULegacyVertexDeltaModelVizSettings>(VizSettings);
		check(VertexVizSettings);

		UGeometryCache* GeomCache = VertexVizSettings->GetTestGroundTruth();
		if (GeomCache == nullptr)
		{
			OutPositions.Reset();
			return;
		}

		if (MeshMappings.IsEmpty())
		{
			TArray<FString> FailedImportedMeshnames;
			TArray<FString> VertexMisMatchNames;
			GenerateGeomCacheMeshMappings(SkeletalMesh, GeomCache, MeshMappings, FailedImportedMeshnames, VertexMisMatchNames);
		}

		UE::MLDeformer::SampleGeomCachePositions(
			0,
			SampleTime,
			MeshMappings,
			SkeletalMesh,
			GeomCache,
			AlignmentTransform,
			OutPositions);
	}
#endif

#undef LOCTEXT_NAMESPACE
