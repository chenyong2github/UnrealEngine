// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameTypes.h"
#include "VectorTypes.h"

class AVolume;
class FDynamicMesh3;

namespace UE {
namespace Conversion { 

struct FVolumeToMeshOptions
{
	bool bInWorldSpace = false;
	bool bSetGroups = true;
	bool bMergeVertices = true;
	bool bAutoRepairMesh = true;
	bool bOptimizeMesh = true;
};

struct FDynamicMeshFace
{
	FFrame3d Plane;
	TArray<FVector3d> BoundaryLoop;
};

/**
 * Converts a volume to a dynamic mesh. Does not initialize normals and does not delete the volume.
 */
void MESHMODELINGTOOLSEDITORONLY_API VolumeToDynamicMesh(AVolume* Volume, FDynamicMesh3& Mesh, const FVolumeToMeshOptions& Options);

/**
 * Gets an array of face objects that can be used to convert a dynamic mesh to a volume. This version tries to
 * merge coplanar triangles into polygons.
 */
void MESHMODELINGTOOLSEDITORONLY_API GetPolygonFaces(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces);
/**
 * Gets an array of face objects that can be used to convert a dynamic mesh to a volume. This version makes
 * each triangle its own face.
 */
void MESHMODELINGTOOLSEDITORONLY_API GetTriangleFaces(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces);

/**
 * Converts a dynamic mesh to a volume.
 */
void MESHMODELINGTOOLSEDITORONLY_API DynamicMeshToVolume(const FDynamicMesh3& InputMesh, AVolume* TargetVolume);
void MESHMODELINGTOOLSEDITORONLY_API DynamicMeshToVolume(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces, AVolume* TargetVolume);

}//end namespace UE::Conversion
}//end namespace UE