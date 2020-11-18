// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameTypes.h"
#include "VectorTypes.h"

// NOTE: The current implementation of DynamicMeshToVolume is editor-only,
// and is therefore split here from VolumeToDynamicMesh. If it ever becomes
// safe for runtime, we should move it to the same place (or combine it in
// one file).

class AVolume;
class FDynamicMesh3;

namespace UE {
namespace Conversion { 

struct FDynamicMeshFace
{
	FFrame3d Plane;
	TArray<FVector3d> BoundaryLoop;
};

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

}}//end namespace UE::Conversion