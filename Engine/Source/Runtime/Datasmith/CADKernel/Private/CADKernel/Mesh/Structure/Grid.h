// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Mesh/MeshEnum.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/DefineForDebug.h"
#include "CADKernel/Mesh/Structure/ThinZone2D.h"
#include "CADKernel/UI/Display.h"


namespace CADKernel
{
	struct FCuttingPoint;
	struct FCuttingGrid;
	
	class FModelMesh;
	class FPoint;
	class FPoint2D;
	class FThinZone2DFinder;
	class FTopologicalFace;

	struct FGridChronos
	{
		FDuration DefineCuttingParametersDuration;
		FDuration GeneratePointCloudDuration;
		FDuration ProcessPointCloudDuration;
		FDuration FindInnerDomainPointsDuration;
		FDuration Build2DLoopDuration;
		FDuration RemovePointsClosedToLoopDuration;
		FDuration FindPointsCloseToLoopDuration;
		FDuration ScaleGridDuration;

		FGridChronos()
			: DefineCuttingParametersDuration(FChrono::Init())
			, GeneratePointCloudDuration(FChrono::Init())
			, ProcessPointCloudDuration(FChrono::Init())
			, FindInnerDomainPointsDuration(FChrono::Init())
			, Build2DLoopDuration(FChrono::Init())
			, RemovePointsClosedToLoopDuration(FChrono::Init())
			, FindPointsCloseToLoopDuration(FChrono::Init())
			, ScaleGridDuration(FChrono::Init())
		{}

		void PrintTimeElapse() const
		{
			FDuration GridDuration = FChrono::Elapse(FChrono::Now());
			GridDuration += DefineCuttingParametersDuration;
			GridDuration += GeneratePointCloudDuration;
			GridDuration += FindInnerDomainPointsDuration;
			GridDuration += Build2DLoopDuration;
			GridDuration += RemovePointsClosedToLoopDuration;
			GridDuration += FindPointsCloseToLoopDuration;
			FChrono::PrintClockElapse(Log, TEXT(""), TEXT("Grid"), GridDuration);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("DefineCuttingParameters"), DefineCuttingParametersDuration);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("GeneratePointCloud"), GeneratePointCloudDuration);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("GenerateDomainPoints"), ProcessPointCloudDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("FindInnerDomainPointsDuration"), FindInnerDomainPointsDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("FindPointsCloseToLoop"), FindPointsCloseToLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("RemovePointsClosedToLoop"), RemovePointsClosedToLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Build2DLoopDuration"), Build2DLoopDuration);
		}
	};

	class FGrid : public FHaveStates
	{
	protected:

		const TSharedRef<FTopologicalFace> Face;
		const FSurfacicTolerance FaceTolerance;

		TSharedRef<FModelMesh> MeshModel;

		FThinZone2DFinder ThinZoneFinder;

		/**
		 * 2D Coordinate of Loop's nodes in each space
		 */
		TArray<TArray<FPoint2D>> FaceLoops2D[EGridSpace::EndGridSpace];

		/**
		 * 3D Coordinate of Loop nodes in each space
		 */
		TArray<TArray<FPoint>> FaceLoops3D;

		/**
		 * Surface Normal at each boundary nodes
		 */
		TArray<TArray<FPoint>> NormalsOfFaceLoops;

		TArray<TArray<int32>> NodeIdsOfFaceLoops;

		/**
		 * grid point cloud size
		 */ 
		int32 CuttingCount[2] = {0 , 0};
		int32 CuttingSize = 0;

		/**
		 * count of node inside the face i.e. node inside the external loop and outer the inner loops 
		 */
		int32 CountOfInnerNodes = 0;

		/*
		 * Cutting coordinates of the face respecting the meshing criteria
		 */
		const FCoordinateGrid& CuttingCoordinates;
		FCoordinateGrid UniformCuttingCoordinates;

		/*
		 * Maximum difference of coordinate along the specified axis of two successive cutting points
		 */
		FPoint2D MaxDeltaUV = { 0., 0. };

		/**
		 * Maximum 3d distance along each axis of two successive cutting points
		 */
		FPoint2D MaxElementSize = { 0., 0. };

		double MinOfMaxElementSize = 0;

		/**
		 * Array to flag each points as inside the face (if IsInsideDomain[Index] = 1) or outer 
		 * @see IsNodeInsideFace
		 */
		TArray<char> IsInsideFace;

		/**
		 * Array to flag each points as close to a loop of the face if IsCloseToLoop[Index] = 1
		 * @see IsNodeCloseToLoop
		 */
		TArray<char> IsCloseToLoop;

		/**
		 * 2D Coordinate of grid nodes in each space
		 */
		TArray<FPoint2D> Points2D[EGridSpace::EndGridSpace];

		/**
		 * 3D Coordinate of inner nodes
		 */
		TArray<FPoint> Points3D;

		/**
		 * Surface Normal at each inner nodes
		 */
		TArray<FPoint> Normals;

	public:
		FGridChronos Chronos;

	public:
		FGrid(TSharedRef<FTopologicalFace>& InFace, TSharedRef<FModelMesh>& InShellMesh);

		virtual ~FGrid() = default;

		// ======================================================================================================================================================================================================================
		// Meshing tools =========================================================================================================================================================================================================
		// ======================================================================================================================================================================================================================
		
		/**
		 * Return true if the grid is not consistent to build a mesh as composed of only two border nodes.
		 */
		bool CheckIfDegenerated();

		/**
		 * Defines the cutting coordinate of the grid according to mesh criteria and the existing mesh of bordering edges (loop's edges)
		 * @see GetPreferredUVCoordinatesFromNeighbours
		 */
		void DefineCuttingParameters();
		void DefineCuttingParameters(EIso Iso, FCuttingGrid& Neighbors);

		/**
		 * Computes the 2d Points, 3D points and Normals of the grid.
		 * call of DefineCuttingParameters is mandatory called before
		 * @return false if the grid is degenerated
		 */
		bool GeneratePointCloud();

		/**
		 * Process the generated point cloud to compute the scaled parametric spaces and to identify outer points of the face
		 * @see IsNodeInsideFace
		 * @see Points2D
		 */
		void ProcessPointCloud();

		/**
		 * Convert an array of points of "DefaultParametric" space into a scaled parametric space
		 * @see FThinZone2DFinder::BuildBoundarySegments()
		 */
		void TransformPoints(EGridSpace DestinationSpace, const TArray<FPoint2D>& InPointsToScale, TArray<FPoint2D>& OutScaledPoints) const;

	protected:

		/**
		 * Builds the scaled parametric spaces 
		 * @see Points2D
		 * @see GeneratePointCloud (ended GeneratePointCloud process)
		 */
		void ScaleGrid();

		/**
		 * Projects loop's points in the scaled parametric spaces
		 * @see FaceLoops2D
		 * @see ProcessPointClound (called in ProcessPointClound)
		 */
		void ScaleLoops();

		/**
		 * Convert Coordinate of "DefaultParametric" space into a scaled parametric space
		 * @see ScaleLoops
		 */
		void ComputeNewCoordinate(const TArray<FPoint2D>& NewGrid, int32 IndexU, int32 IndexV, const FPoint2D& InPoint, FPoint2D& OutNewScaledPoint) const
		{
			OutNewScaledPoint =
				NewGrid[(IndexV + 0) * CuttingCount[EIso::IsoU] + (IndexU + 0)] * (CuttingCoordinates[EIso::IsoU][IndexU + 1] - InPoint.U) * (CuttingCoordinates[EIso::IsoV][IndexV + 1] - InPoint.V) -
				NewGrid[(IndexV + 0) * CuttingCount[EIso::IsoU] + (IndexU + 1)] * (CuttingCoordinates[EIso::IsoU][IndexU + 0] - InPoint.U) * (CuttingCoordinates[EIso::IsoV][IndexV + 1] - InPoint.V) -
				NewGrid[(IndexV + 1) * CuttingCount[EIso::IsoU] + (IndexU + 0)] * (CuttingCoordinates[EIso::IsoU][IndexU + 1] - InPoint.U) * (CuttingCoordinates[EIso::IsoV][IndexV + 0] - InPoint.V) +
				NewGrid[(IndexV + 1) * CuttingCount[EIso::IsoU] + (IndexU + 1)] * (CuttingCoordinates[EIso::IsoU][IndexU + 0] - InPoint.U) * (CuttingCoordinates[EIso::IsoV][IndexV + 0] - InPoint.V);
			OutNewScaledPoint /= (CuttingCoordinates[EIso::IsoU][IndexU + 1] - CuttingCoordinates[EIso::IsoU][IndexU + 0]) * (CuttingCoordinates[EIso::IsoV][IndexV + 1] - CuttingCoordinates[EIso::IsoV][IndexV + 0]);
		};

		/**
		 * @see ProcessPointClound (called in ProcessPointClound)
		 */
		void FindInnerFacePoints();

		/**
		 * Define if a node is closes to a loop i.e. the loop cross the space [[IndexU - 1, IndexU + 1], [IndexV - 1, IndexV + 1]]
		 * This algorithm is inspired of Bresenham's line algorithm
		 * @see IsNodeCloseToLoop
		 * @see ProcessPointClound (called in ProcessPointClound)
		 */
		void FindPointsCloseToLoop();

		/**
		 * Find all node close to the boundary.
		 * For each node, the 3d distance to the boundary is estimated
		 * If this distance is bigger than the Tolerance, the node is remove
		 * Removing a node can make its neighbor removable,
		 * Each removed node make its direct neighbor removable, the process is recursive as long as it is possible to remove a node
		 *
		 * @see ProcessPointClound (called in ProcessPointClound)
		 */
		void RemovePointsClosedToLoop();


		/**
		 * Gets the cutting coordinates of the existing mesh of bordering edges (loop's edges)
		 * @see DefineCuttingParameters (called in DefineCuttingParameters)
		 */
		void GetPreferredUVCoordinatesFromNeighbours(FCuttingGrid& NeighboursCutting);

		void ComputeMaxElementSize();
		void ComputeMaxDeltaUV();

		/**
		 * @ return false if the mesh of the loop is degenerated
		 */
		bool GetMeshOfLoops();

	public:

		// ======================================================================================================================================================================================================================
		// ThinZone properties for meshing scheduling optimisation ==============================================================================================================================================================
		// ======================================================================================================================================================================================================================

		void SearchThinZones();
		const TArray<FThinZone2D>& GetThinZones() const
		{
			return ThinZoneFinder.GetThinZones();
		}

		// ======================================================================================================================================================================================================================
		// GET Methodes =========================================================================================================================================================================================================
		// ======================================================================================================================================================================================================================

		constexpr const int32 GetCuttingCount(EIso Iso) const
		{
			return CuttingCount[Iso];
		}

		/**
		 * Return the number of points (inner and outer points) of the grid (i.e. CuttingCount[IsoU] x CuttingCount[IsoV]).
		 */
		const int32 GetTotalCuttingCount() const
		{
			return CuttingSize;
		}

		const double GetTolerance(EIso Iso) const
		{
			return FaceTolerance[Iso];
		}

		/**
		 * @return true if the node is inner the external loop and outer the inner loops 
		 */
		const bool IsNodeInsideFace(int32 IndexU, int32 IndexV) const
		{
			return IsInsideFace[GobalIndex(IndexU, IndexV)] == 1;
		}

		/**
		 * @return true if the node is inner the external loop and outer the inner loops
		 */
		const bool IsNodeInsideFace(int32 Index) const
		{
			return IsInsideFace[Index] == 1;
		}

		/**
		 * @return the count of inner nodes
		 */
		const int32 InnerNodesCount() const
		{
			return CountOfInnerNodes;
		}

		/**
		 * @return true if the node is close to a loop i.e. the loop cross the space [[IndexU - 1, IndexU + 1], [IndexV - 1, IndexV + 1]]
		 */
		const bool IsNodeCloseToLoop(int32 IndexU, int32 IndexV) const
		{
			return IsCloseToLoop[GobalIndex(IndexU, IndexV)] == 1;
		}

		/**
		 * @return true if the node is close to a loop i.e. the loop cross the space [[IndexU - 1, IndexU + 1], [IndexV - 1, IndexV + 1]]
		 */
		const bool IsNodeCloseToLoop(int32 Index) const
		{
			return IsCloseToLoop[Index] == 1;
		}

		/**
		 * @return the FPoint2D (parametric coordinates) of the point at the Index of the grid in the defined grid space 
		 * @see EGridSpace
		 * @see Points2D
		 */
		const FPoint2D& GetInner2DPoint(EGridSpace Space, int32 Index) const
		{
			return Points2D[(int32)Space][Index];
		}

		/**
		 * @return the FPoint2D (parametric coordinates) of the point at the Index of the grid in the defined grid space @see EGridSpace
		 */
		const FPoint2D& GetInner2DPoint(EGridSpace Space, int32 IndexU, int32 IndexV) const
		{
			return Points2D[(int32)Space][GobalIndex(IndexU, IndexV)];
		}

		/**
		 * @return the FPoint (3D coordinates) of the point at the Index of the grid
		 */
		const FPoint& GetInner3DPoint(int32 Index) const
		{
			return Points3D[Index];
		}

		/**
		 * @return the FPoint (3D coordinates) of the point at the Index of the grid
		 */
		const FPoint& GetInner3DPoint(int32 IndexU, int32 IndexV) const
		{
			return Points3D[GobalIndex(IndexU, IndexV)];
		}

		/**
		 * @return the normal of the surface at the point at the Index of the grid
		 */
		const FPoint& GetPointNormal(int32 IndexU, int32 IndexV) const
		{
			return Normals[GobalIndex(IndexU, IndexV)];
		}

		/**
		 * @return the normal of the surface at the point at the Index of the grid
		 */
		const FPoint& GetPointNormal(int32 Index) const
		{
			return Normals[Index];
		}

		constexpr const TArray<double>& GetCuttingCoordinatesAlongIso(EIso Iso) const
		{
			return CuttingCoordinates[Iso];
		}

		const FCoordinateGrid& GetCuttingCoordinates() const
		{
			return CuttingCoordinates;
		}

		constexpr const TArray<double>& GetUniformCuttingCoordinatesAlongIso(EIso Iso) const
		{
			return UniformCuttingCoordinates[Iso];
		}

		const FCoordinateGrid& GetUniformCuttingCoordinates() const
		{
			return UniformCuttingCoordinates;
		}

		/**
		 * @return the array of 3d points of the grid
		 */
		TArray<FPoint>& GetInner3DPoints()
		{
			return Points3D;
		}

		/**
		 * @return the array of 3d points of the grid
		 */
		const TArray<FPoint>& GetInner3DPoints() const
		{
			return Points3D;
		}

		/**
		 * @return the array of 2d points of the grid in the defined space
		 */
		const TArray<FPoint2D>& GetInner2DPoints(EGridSpace Space) const
		{
			return Points2D[(int32)Space];
		}

		/**
		 * @return the array of normal of the points of the grid
		 */
		TArray<FPoint>& GetNormals()
		{
			return Normals;
		}

		TSharedRef<FTopologicalFace> GetFace() const
		{
			return Face;
		}

		const TArray<TArray<int32>>& GetNodeIdsOfFaceLoops() const
		{
			return NodeIdsOfFaceLoops;
		}

		const FPoint2D& GetLoop2DPoint(EGridSpace Space, int32 LoopIndex, int32 Index) const
		{
			return FaceLoops2D[(int32)Space][LoopIndex][Index];
		}

		const FPoint& GetLoop3DPoint(int32 LoopIndex, int32 Index) const
		{
			return FaceLoops3D[LoopIndex][Index];
		}

		const int32 GetLoopCount() const
		{
			return FaceLoops2D[0].Num();
		}

		/**
		 * @return the array of array of 2d points of the loops according to the defined grid space
		 */
		const TArray<TArray<FPoint2D>>& GetLoops2D(EGridSpace Space) const
		{
			return FaceLoops2D[(int32)Space];
		}

		/**
		 * @return the array of array of 3d points of the loops
		 */
		TArray<TArray<FPoint>>& GetLoops3D()
		{
			return FaceLoops3D;
		}

		/**
		 * @return the array of array of 3d points of the loops
		 */
		const TArray<TArray<FPoint>>& GetLoops3D() const
		{
			return FaceLoops3D;
		}

		/**
		 * @return the array of array of normal of the points of the loops
		 */
		const TArray<TArray<FPoint>>& GetLoopNormals() const
		{
			return NormalsOfFaceLoops;
		}

		/**
		 * @return the Index of the position in the arrays of a point [IndexU, IndexV] of the grid
		 */
		int32 GobalIndex(int32 IndexU, int32 IndexV) const
		{
			return IndexV * CuttingCount[EIso::IsoU] + IndexU;
		}

		/**
		 * @return the maximum difference of coordinate along the specified axis of two two successive points
		 */
		double GetMaxDeltaU(EIso Iso) const
		{
			return MaxDeltaUV[Iso];
		}

		/**
		 * @return the minimal size of the mesh elements in the grid
		 */
		double GetMinElementSize() const
		{
			return MinOfMaxElementSize;
		}

		// ======================================================================================================================================================================================================================
		// Display Methodes   ================================================================================================================================================================================================
		// ======================================================================================================================================================================================================================
		bool bDisplay = false;

		void DisplayGridPoints(EGridSpace DisplaySpace) const;
		void DisplayGridInnerPoints(EGridSpace DisplaySpace, TCHAR* Message) const;

		template<typename TPoint>
		void DisplayPoints(FString Message, const TArray<TPoint>& Points) const
		{
			if (!bDisplay)
			{
				return;
			}

			Open3DDebugSession(Message);
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				DisplayPoint(Points[Index], Index);
			}
			Close3DDebugSession();
		}

		template<typename TPoint>
		void DisplayInnerDomainPoints(FString Message, const TArray<TPoint>& Points) const
		{
			if (!bDisplay)
			{
				return;
			}

			Open3DDebugSession(Message);
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				if (IsInsideFace[Index])
				{
					if (IsCloseToLoop[Index])
					{
						DisplayPoint(Points[Index], EVisuProperty::BluePoint, Index);
					}
					else
					{
						DisplayPoint(Points[Index], EVisuProperty::OrangePoint, Index);
					}
				}
			}
			Close3DDebugSession();
		}

		template<typename TPoint>
		void DisplayLoop(FString Message, const TArray<TArray<TPoint>>& Loops, bool bDisplayNodes, bool bMakeGroup) const
		{
			if (!bDisplay)
			{
				return;
			}

			Open3DDebugSession(*Message);
			int32 LoopIndex = 0;
			for (const TArray<TPoint>& Loop : Loops)
			{
				if (bMakeGroup)
				{
					Open3DDebugSession(FString::Printf(TEXT("Loop %d"), LoopIndex++));
				}

				const TPoint* FirstSegmentPoint = &Loop[0];
				if (bDisplayNodes)
				{
					DisplayPoint(*FirstSegmentPoint, EVisuProperty::BluePoint, 0);
				}

				for (int32 Index = 1; Index < Loop.Num(); ++Index)
				{
					const TPoint* SecondSegmentPoint = &Loop[Index];
					DisplaySegment(*FirstSegmentPoint, *SecondSegmentPoint);
					if (bDisplayNodes)
					{
						DisplayPoint(*SecondSegmentPoint, EVisuProperty::BluePoint, Index);
					}
					FirstSegmentPoint = SecondSegmentPoint;
				}
				DisplaySegment(*FirstSegmentPoint, *Loop.begin());
				if (bMakeGroup)
				{
					Close3DDebugSession();
				}
			}
			Close3DDebugSession();
		}

		void DisplayFindPointsCloseToLoop(EGridSpace DisplaySpace) const;
		void PrintTimeElapse() const;
	};

}
