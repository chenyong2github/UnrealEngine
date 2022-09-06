// Copyright Epic Games, Inc. All Rights Reserved.
#include "MLDeformerGeomCacheModel.h"
#include "MLDeformerGeomCacheVizSettings.h"
#include "MLDeformerGeomCacheHelpers.h"
#include "MLDeformerComponent.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "GeometryCache.h"

#define LOCTEXT_NAMESPACE "MLDeformerGeomCacheModel"

#if WITH_EDITOR
void UMLDeformerGeomCacheModel::UpdateNumTargetMeshVertices()
{
	NumTargetMeshVerts = UE::MLDeformer::ExtractNumImportedGeomCacheVertices(GeometryCache);
}

UMLDeformerGeomCacheVizSettings* UMLDeformerGeomCacheModel::GetGeomCacheVizSettings() const
{
	return Cast<UMLDeformerGeomCacheVizSettings>(VizSettings);
}

void UMLDeformerGeomCacheModel::SetAssetEditorOnlyFlags()
{
	// Set the flags for the base class, which filters out the training anim sequence.
	UMLDeformerModel::SetAssetEditorOnlyFlags();

	// The training geometry cache is something we don't want to package.
	if (GeometryCache)
	{
		GeometryCache->GetPackage()->SetPackageFlags(PKG_EditorOnly);
	}

	// Filter the viz settings specific assets.
	UMLDeformerGeomCacheVizSettings* GeomCacheVizSettings = GetGeomCacheVizSettings();
	if (GeomCacheVizSettings)
	{
		if (GeomCacheVizSettings->GetTestGroundTruth())
		{
			GeomCacheVizSettings->GetTestGroundTruth()->GetPackage()->SetPackageFlags(PKG_EditorOnly);
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void UMLDeformerGeomCacheModel::SampleGroundTruthPositions(float SampleTime, TArray<FVector3f>& OutPositions)
{
	const UMLDeformerGeomCacheVizSettings* GeomCacheVizSettings = GetGeomCacheVizSettings();
	check(GeomCacheVizSettings);

	UGeometryCache* GeomCache = GeomCacheVizSettings->GetTestGroundTruth();
	if (GeomCache == nullptr)
	{
		OutPositions.Reset();
		return;
	}

	if (MeshMappings.IsEmpty())
	{
		TArray<FString> FailedImportedMeshNames;
		TArray<FString> VertexMisMatchNames;
		UE::MLDeformer::GenerateGeomCacheMeshMappings(SkeletalMesh, GeomCache, MeshMappings, FailedImportedMeshNames, VertexMisMatchNames);
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
