// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/VoxelBooleanMeshesOp.h"

#include "Engine/StaticMesh.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "CleaningOps/EditNormalsOp.h"

void FVoxelBooleanMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	FMeshDescription ResultMeshDescription;
	FStaticMeshAttributes Attributes(ResultMeshDescription);
	Attributes.Register();

	if (VoxelCount < 16)
	{
		VoxelCount = 16;
	}

	// Use the world space bounding box of each mesh to compute the voxel size
	VoxelSizeD = ComputeVoxelSize();

	// give this an absolute min since the user might scale both objects to zero..
	VoxelSizeD = FMath::Max(VoxelSizeD, 0.001);

	// world space units.
	const double MaxNarrowBand = 3 * VoxelSizeD;
	const double MaxIsoOffset = 2 * VoxelSizeD;

	// IsoSurfaceD is in world units

	const bool bShinkResult = (IsoSurfaceD < 0.);

	// Note after the min/max operations of voxel-based CSG the interior distances or exterior 
	// distances can be pretty f'd up so we need to do the initial post-CSG remesh with a "safe"
	// value.
	double CSGIsoSurface = 0.;

	
	// Create BooleanTool and Boolean the meshes.
	TUniquePtr<IVoxelBasedCSG> VoxelCSGTool = IVoxelBasedCSG::CreateCSGTool(VoxelSizeD);
	FVector MergedOrigin;

	IVoxelBasedCSG::FPlacedMesh& A = (*InputMeshArray)[0];
	IVoxelBasedCSG::FPlacedMesh& B = (*InputMeshArray)[1];

	switch (Operation)
	{
	case EBooleanOperation::DifferenceAB:
		CSGIsoSurface = 0.;
		MergedOrigin = VoxelCSGTool->ComputeDifference(A, B, ResultMeshDescription, AdaptivityD, CSGIsoSurface);
		break;
	case EBooleanOperation::DifferenceBA:
		CSGIsoSurface = 0.;
		MergedOrigin = VoxelCSGTool->ComputeDifference(B, A, ResultMeshDescription, AdaptivityD, CSGIsoSurface);
		break;
	case EBooleanOperation::Intersect:
		CSGIsoSurface = FMath::Clamp(IsoSurfaceD, -MaxIsoOffset, MaxIsoOffset);
		MergedOrigin = VoxelCSGTool->ComputeIntersection(A, B, ResultMeshDescription, AdaptivityD, CSGIsoSurface);
		break;
	case EBooleanOperation::Union:
		CSGIsoSurface = FMath::Clamp(IsoSurfaceD, 0., MaxIsoOffset);
		MergedOrigin = VoxelCSGTool->ComputeUnion(A, B, ResultMeshDescription, AdaptivityD, CSGIsoSurface);
		break;
	default:
		check(0);
	}

	ResultTransform = FTransform3d(MergedOrigin);

	if (Progress->Cancelled())
	{
		return;
	}

	const bool bNeedAdditionalRemeshing = (CSGIsoSurface != IsoSurfaceD);

	// iteratively expand (or contract) the surface
	if ( bNeedAdditionalRemeshing )
	{
		// how additional remeshes do we need?
		int32 NumRemeshes = FMath::CeilToInt( FMath::Abs(IsoSurfaceD - CSGIsoSurface) / MaxIsoOffset );

		double DeltaIsoSurface = (IsoSurfaceD - CSGIsoSurface) / NumRemeshes;

		for (int32 i = 0; i < NumRemeshes; ++i)
		{
			if (Progress->Cancelled())
			{
				return;
			}

			IVoxelBasedCSG::FPlacedMesh PlacedMesh;
			PlacedMesh.Mesh = &ResultMeshDescription;
			PlacedMesh.Transform = static_cast<FTransform>(ResultTransform);

			TArray< IVoxelBasedCSG::FPlacedMesh> SourceMeshes;
			SourceMeshes.Add(PlacedMesh);

			// union of only one mesh. we are using this to voxelize and remesh with and offset.
			MergedOrigin = VoxelCSGTool->ComputeUnion(SourceMeshes, ResultMeshDescription, AdaptivityD, DeltaIsoSurface);

			ResultTransform = FTransform3d(MergedOrigin);
		}
	}


	// Convert to dynamic mesh
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = true;
	Converter.Convert(&ResultMeshDescription, *ResultMesh);

	if (Progress->Cancelled())
	{
		return;
	}

	// for now we automatically fix up the normals if simplificaiton was requested.
	bool bFixNormals = bAutoSimplify;

	if (bAutoSimplify)
	{
		FQEMSimplification Reducer(ResultMesh.Get());
		FMeshConstraints constraints;
		FMeshConstraintsUtil::ConstrainAllSeams(constraints, *ResultMesh, true, false);
		Reducer.SetExternalConstraints(&constraints);
		Reducer.Progress = Progress;

		const double MaxDisplacementSqr = 3. * VoxelSizeD * VoxelSizeD;

		Reducer.SimplifyToMaxError(MaxDisplacementSqr);

	}


	if (bFixNormals)
	{

		TSharedPtr<FDynamicMesh3> OpResultMesh(ExtractResult().Release()); // moved the unique pointer

		// Recompute the normals
		FEditNormalsOp EditNormalsOp;
		EditNormalsOp.OriginalMesh = OpResultMesh; // the tool works on a deep copy of this mesh.
		EditNormalsOp.bFixInconsistentNormals = true;
		EditNormalsOp.bInvertNormals = false;
		EditNormalsOp.bRecomputeNormals = true;
		EditNormalsOp.NormalCalculationMethod = ENormalCalculationMethod::AreaAngleWeighting;
		EditNormalsOp.SplitNormalMethod = ESplitNormalMethod::FaceNormalThreshold;
		EditNormalsOp.bAllowSharpVertices = true;
		EditNormalsOp.NormalSplitThreshold = 60.f;

		EditNormalsOp.SetTransform(FTransform(ResultTransform));

		EditNormalsOp.CalculateResult(Progress);

		ResultMesh = EditNormalsOp.ExtractResult(); // return the edit normals operator copy to this tool.
	}

}

float FVoxelBooleanMeshesOp::ComputeVoxelSize() const 
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
