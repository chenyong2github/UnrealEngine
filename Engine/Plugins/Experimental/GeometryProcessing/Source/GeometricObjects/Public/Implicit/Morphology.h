// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp / gsShapeModels MeshMorphologyOp

#pragma once

#include "MeshAdapter.h"

#include "Spatial/MeshAABBTree3.h"

#include "Implicit/SweepingMeshSDF.h"
#include "Implicit/GridInterpolant.h"
#include "Implicit/ImplicitFunctions.h"

#include "Generators/MarchingCubes.h"
#include "MeshQueries.h"




template<typename TriangleMeshType>
class TImplicitMorphology
{
public:
	/** Morphology operation types */
	enum class EMorphologyOp
	{
		/** Expand the shapes outward */
		Dilate = 0,

		/** Shrink the shapes inward */
		Contract = 1,

		/** Dilate and then contract, to delete small negative features (sharp inner corners, small holes) */
		Close = 2,

		/** Contract and then dilate, to delete small positive features (sharp outer corners, small isolated pieces) */
		Open = 3
	};

	virtual ~TImplicitMorphology()
	{
	}

	///
	/// Inputs
	///
	const TriangleMeshType* Source = nullptr;
	TMeshAABBTree3<TriangleMeshType>* SourceSpatial = nullptr;
	EMorphologyOp MorphologyOp = EMorphologyOp::Dilate;

	// Distance of offset; should be positive
	double Distance = 1.0;

	// size of the cells used when sampling the distance field
	double GridCellSize = 1.0;

	// size of the cells used when meshing the output (marching cubes' cube size)
	double MeshCellSize = 1.0;

	// Set cell sizes to hit the target voxel counts along the max dimension of the bounds
	void SetCellSizesAndDistance(FAxisAlignedBox3d Bounds, double DistanceIn, int TargetInputVoxelCount, int TargetOutputVoxelCount)
	{
		Distance = DistanceIn;
		GridCellSize = (Bounds.MaxDim() + Distance * 2.0) / double(TargetInputVoxelCount);
		MeshCellSize = (Bounds.MaxDim() + Distance * 2.0) / double(TargetOutputVoxelCount);
	}

	/** if this function returns true, we should abort calculation */
	TFunction<bool(void)> CancelF = []()
	{
		return false;
	};

protected:
	// Stores result (returned as a const FMeshShapeGenerator)
	FMarchingCubes MarchingCubes;

	/// Intermediate -- result of first pass, could potentially be re-used if recomputed w/ equal or smaller Distance
	TSweepingMeshSDF<TriangleMeshType> ComputedSDF;

public:
	bool Validate()
	{
		bool bValidMeshAndSpatial = Source != nullptr && SourceSpatial != nullptr && SourceSpatial->IsValid();
		bool bValidParams = Distance > 0 && GridCellSize > 0 && MeshCellSize > 0 && FMath::IsFinite(MeshCellSize);
		return bValidMeshAndSpatial && bValidParams;
	}

	const FMeshShapeGenerator& Generate()
	{
		MarchingCubes.Reset();
		if (!ensure(Validate()))
		{
			// give up and return and empty result on invalid parameters
			return MarchingCubes;
		}

		double UnsignedOffset = FMathd::Abs(Distance);
		double SignedOffset = UnsignedOffset;
		switch (MorphologyOp)
		{
		case TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Dilate:
		case TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close:
			SignedOffset = -SignedOffset;
			break;
		}

		ComputeFirstPass(UnsignedOffset, SignedOffset);

		if (MorphologyOp == TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close || MorphologyOp == TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Open)
		{
			ComputeSecondPass(UnsignedOffset, -SignedOffset);
		}

		return MarchingCubes;
	}

protected:
	void ComputeFirstPass(double UnsignedOffset, double SignedOffset)
	{
		ComputedSDF.Mesh = Source;

		ComputedSDF.Spatial = SourceSpatial;
		ComputedSDF.ComputeMode = TSweepingMeshSDF<TriangleMeshType>::EComputeModes::NarrowBand_SpatialFloodFill;


		ComputedSDF.CellSize = GridCellSize;
		ComputedSDF.NarrowBandMaxDistance = UnsignedOffset + ComputedSDF.CellSize;
		ComputedSDF.ExactBandWidth = FMath::CeilToInt(ComputedSDF.NarrowBandMaxDistance / ComputedSDF.CellSize);

		// for meshes with long triangles relative to the width of the narrow band, don't use the AABB tree
		double AvgEdgeLen = TMeshQueries<TriangleMeshType>::AverageEdgeLength(*Source);
		if (!ComputedSDF.ShouldUseSpatial(ComputedSDF.ExactBandWidth, ComputedSDF.CellSize, AvgEdgeLen))
		{
			ComputedSDF.Spatial = nullptr;
			ComputedSDF.ComputeMode = TSweepingMeshSDF<TriangleMeshType>::EComputeModes::NarrowBandOnly;
		}

		ComputedSDF.Compute(SourceSpatial->GetBoundingBox());

		TTriLinearGridInterpolant<TSweepingMeshSDF<TriangleMeshType>> Interpolant = ComputedSDF.MakeInterpolant();

		MarchingCubes.IsoValue = SignedOffset;
		MarchingCubes.Bounds = SourceSpatial->GetBoundingBox();
		MarchingCubes.Bounds.Expand(GridCellSize);
		if (MarchingCubes.IsoValue < 0)
		{
			MarchingCubes.Bounds.Expand(ComputedSDF.NarrowBandMaxDistance);
		}
		MarchingCubes.RootMode = ERootfindingModes::SingleLerp;
		MarchingCubes.CubeSize = MeshCellSize;

		MarchingCubes.CancelF = CancelF;

		if (CancelF())
		{
			return;
		}

		MarchingCubes.Implicit = [Interpolant](const FVector3d& Pt)
		{
			return -Interpolant.Value(Pt);
		};
		MarchingCubes.Generate();

		// TODO: refactor FMarchingCubes to not retain the implicit function, or refactor this function so the implicit function isn't invalid after returning,
		/// ..... then remove this line
		MarchingCubes.Implicit = nullptr;
	}

	void ComputeSecondPass(double UnsignedOffset, double SignedOffset)
	{
		if (MarchingCubes.Triangles.Num() == 0)
		{
			MarchingCubes.Reset();
			return;
		}

		TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d> MCAdapter(&MarchingCubes.Vertices, &MarchingCubes.Triangles);
		TMeshAABBTree3<TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d>> SecondSpatial(&MCAdapter, false);

		TSweepingMeshSDF<TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d>> SecondSDF;
		SecondSDF.Mesh = &MCAdapter;
		SecondSDF.CellSize = GridCellSize;
		SecondSDF.Spatial = nullptr;

		FAxisAlignedBox3d Bounds = MarchingCubes.Bounds;
		Bounds.Expand(MeshCellSize); // (because mesh may spill one cell over bounds)

		SecondSDF.NarrowBandMaxDistance = UnsignedOffset + SecondSDF.CellSize;
		SecondSDF.ExactBandWidth = FMath::CeilToInt(SecondSDF.NarrowBandMaxDistance / SecondSDF.CellSize);

		if (SecondSDF.ExactBandWidth > 1) // for larger band width, prefer using the AABB tree to do one distance per cell.  TODO: tune?
		{
			SecondSpatial.Build();
			SecondSDF.Spatial = &SecondSpatial;
			SecondSDF.ComputeMode = TSweepingMeshSDF<TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d>>::EComputeModes::NarrowBand_SpatialFloodFill;
			Bounds = SecondSpatial.GetBoundingBox(); // Use the tighter bounds from the AABB tree since we have it
		}
		else
		{
			SecondSDF.ComputeMode = TSweepingMeshSDF<TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d>>::EComputeModes::NarrowBandOnly;
		}


		if (CancelF())
		{
			return;
		}

		SecondSDF.Compute(Bounds);
		TTriLinearGridInterpolant<TSweepingMeshSDF<TIndexVectorMeshArrayAdapter<FIndex3i, double, FVector3d>>> Interpolant = SecondSDF.MakeInterpolant();

		MarchingCubes.Reset();
		MarchingCubes.IsoValue = SignedOffset;
		MarchingCubes.Bounds = Bounds;
		MarchingCubes.Bounds.Expand(GridCellSize);
		if (MarchingCubes.IsoValue < 0)
		{
			MarchingCubes.Bounds.Expand(ComputedSDF.NarrowBandMaxDistance);
		}


		if (CancelF())
		{
			return;
		}

		MarchingCubes.Implicit = [Interpolant](const FVector3d& Pt)
		{
			return -Interpolant.Value(Pt);
		};

		MarchingCubes.Generate();

		// TODO: refactor FMarchingCubes to not retain the implicit function, or refactor this function so the implicit function isn't invalid after returning,
		/// ..... then remove this line
		MarchingCubes.Implicit = nullptr;
	}
};
