// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/VoxelMergeMeshesOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"


void FVoxelMergeMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	FMeshDescription MergedMeshesDescription;
	FStaticMeshAttributes Attributes(MergedMeshesDescription);
	Attributes.Register();

	// Use the world space bounding box of each mesh to compute the voxel size
	VoxelSizeD = ComputeVoxelSize();

	// give this an absolute min since the user might scale both objects to zero..
	VoxelSizeD = FMath::Max(VoxelSizeD, 0.001);

	// Create CSGTool and merge the meshes.
	TUniquePtr<IVoxelBasedCSG> VoxelCSGTool = IVoxelBasedCSG::CreateCSGTool(VoxelSizeD);
	FVector MergedOrigin = VoxelCSGTool->ComputeUnion(*InputMeshArray, MergedMeshesDescription, AdaptivityD, IsoSurfaceD);
	ResultTransform = FTransform3d(MergedOrigin);

	if (Progress->Cancelled())
	{
		return;
	}

	// Convert to dynamic mesh
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = true;
	Converter.Convert(&MergedMeshesDescription, *ResultMesh);

	if (Progress->Cancelled())
	{
		return;
	}

	if (bAutoSimplify)
	{
		FQEMSimplification Reducer(ResultMesh.Get());
		FMeshConstraints constraints;
		FMeshConstraintsUtil::ConstrainAllSeams(constraints, *ResultMesh, true, false);
		Reducer.SetExternalConstraints(&constraints);
		Reducer.Progress = Progress;

		const double MaxDisplacementSqr = 3. *VoxelSizeD * VoxelSizeD;

		Reducer.SimplifyToMaxError(MaxDisplacementSqr);
	}
}


float FVoxelMergeMeshesOp::ComputeVoxelSize() const
{
	const TArray<IVoxelBasedCSG::FPlacedMesh>& PlacedMeshes = *(InputMeshArray);

	float Size = 0.f;
	for (int32 i = 0; i < PlacedMeshes.Num(); ++i)
	{
		//Bounding box
		FVector BBoxMin(FLT_MAX, FLT_MAX, FLT_MAX);
		FVector BBoxMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		// Use the scale define by the transform.  We don't care about actual placement
		// in the world for this.
		FVector Scale = PlacedMeshes[i].Transform.GetScale3D();
		const FMeshDescription&  MeshDescription = *PlacedMeshes[i].Mesh;

		TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription.VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			const FVector Pos = VertexPositions[VertexID];

			BBoxMin.X = FMath::Min(BBoxMin.X, Pos.X);
			BBoxMin.Y = FMath::Min(BBoxMin.Y, Pos.Y);
			BBoxMin.Z = FMath::Min(BBoxMin.Z, Pos.Z);

			BBoxMax.X = FMath::Max(BBoxMax.X, Pos.X);
			BBoxMax.Y = FMath::Max(BBoxMax.Y, Pos.Y);
			BBoxMax.Z = FMath::Max(BBoxMax.Z, Pos.Z);
		}

		// The size of the BBox in each direction
		FVector Extents(BBoxMax.X - BBoxMin.X, BBoxMax.Y - BBoxMin.Y, BBoxMax.Z - BBoxMin.Z);

		// Scale with the local space scale.
		Extents.X = Extents.X * Scale.X;
		Extents.Y = Extents.Y * Scale.Y;
		Extents.Z = Extents.Z * Scale.Z;

		float MajorAxisSize = FMath::Max3(Extents.X, Extents.Y, Extents.Z);


		Size = FMath::Max(MajorAxisSize / VoxelCount, Size);

	}

	return Size;

}
