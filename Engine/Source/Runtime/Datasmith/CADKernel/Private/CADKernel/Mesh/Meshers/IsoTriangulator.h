// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Factory.h"
#include "CADKernel/Math/Geometry.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Math/SlopeUtils.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IntersectionSegmentTool.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoNode.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator/IsoSegment.h"
#include "CADKernel/UI/Visu.h"

#include "CADKernel/Mesh/Meshers/IsoTriangulator/DefineForDebug.h"

//#define NEED_TO_CHECK_USEFULNESS
#define DEBUG_DELAUNAY
namespace CADKernel
{
	class FGrid;
	class FIntersectionSegmentTool;
	class FFaceMesh;
	struct FCell;

	typedef TFunction<double(const FPoint2D&, const FPoint2D&, double)> FGetSlop;

	/**
	 * for findNextSegment
	 */ 
	inline double ClockwiseSlop(const FPoint2D& StartPoint, const FPoint2D& EndPoint, double ReferenceSlope)
	{
		return 8. - ComputePositiveSlope(StartPoint, EndPoint, ReferenceSlope);
	}

	inline double CounterClockwiseSlop(const FPoint2D& StartPoint, const FPoint2D& EndPoint, double ReferenceSlope)
	{
		return ComputePositiveSlope(StartPoint, EndPoint, ReferenceSlope);
	}

	struct CADKERNEL_API FIsoTriangulatorChronos
	{
		FDuration TriangulateDuration1 = FChrono::Init();
		FDuration TriangulateDuration2 = FChrono::Init();
		FDuration TriangulateDuration3 = FChrono::Init();
		FDuration TriangulateDuration4 = FChrono::Init();
		FDuration TriangulateDuration = FChrono::Init();
		FDuration BuildIsoNodesDuration = FChrono::Init();
		FDuration BuildLoopSegmentsDuration = FChrono::Init();
		FDuration BuildLoopSegmentsCheckIntersectionDuration = FChrono::Init();
		FDuration BuildInnerSegmentsDuration = FChrono::Init();
		FDuration FindLoopSegmentOfInnerTriangulationDuration = FChrono::Init();
		FDuration FindSegmentIsoUVSurroundingSmallLoopDuration = FChrono::Init();
		FDuration FindIsoSegmentToLinkInnerToLoopDuration = FChrono::Init();
		FDuration FindInnerSegmentToLinkLoopToLoopDuration = FChrono::Init();
		FDuration FindSegmentToLinkLoopToLoopDuration = FChrono::Init();
		FDuration FindSegmentToLinkLoopToLoopByDelaunayDuration = FChrono::Init();
		FDuration FindSegmentToLinkInnerToLoopDuration = FChrono::Init();
		FDuration SelectSegmentToLinkInnerToLoopsDuration = FChrono::Init();
		FDuration TriangulateOverCycleDuration = FChrono::Init();
		FDuration TriangulateInnerNodesDuration = FChrono::Init();

		FIsoTriangulatorChronos()
		{}

		void PrintTimeElapse() const
		{
			FDuration IsoTriangulerDuration = FChrono::Init();
			IsoTriangulerDuration += BuildIsoNodesDuration;
			IsoTriangulerDuration += BuildLoopSegmentsDuration;
			IsoTriangulerDuration += BuildInnerSegmentsDuration;
			IsoTriangulerDuration += FindLoopSegmentOfInnerTriangulationDuration;
			IsoTriangulerDuration += FindIsoSegmentToLinkInnerToLoopDuration;
			IsoTriangulerDuration += FindSegmentToLinkLoopToLoopDuration;
			IsoTriangulerDuration += FindSegmentToLinkLoopToLoopByDelaunayDuration;
			IsoTriangulerDuration += FindSegmentToLinkInnerToLoopDuration;
			IsoTriangulerDuration += SelectSegmentToLinkInnerToLoopsDuration;
			IsoTriangulerDuration += TriangulateOverCycleDuration;
			IsoTriangulerDuration += TriangulateInnerNodesDuration;

			FChrono::PrintClockElapse(Log, TEXT(""), TEXT("IsoTrianguler"), IsoTriangulerDuration);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate"), TriangulateDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("BuildIsoNodes"), BuildIsoNodesDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("BuildLoopSegments"), BuildLoopSegmentsDuration);
			FChrono::PrintClockElapse(Log, TEXT("      "), TEXT("BuildLoopSegments Check intersection"), BuildLoopSegmentsCheckIntersectionDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("BuildInnerSegments"), BuildInnerSegmentsDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("FindLoopSegmentOfInnerTriangulation"), FindLoopSegmentOfInnerTriangulationDuration);
			FChrono::PrintClockElapse(Log, TEXT("      "), TEXT("FindSegmentIsoUVSurroundingSmallLoop"), FindSegmentIsoUVSurroundingSmallLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Find IsoSegment ToLink InnerToLoop"), FindIsoSegmentToLinkInnerToLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Find Segment ToLink LoopToLoop by Delaunay"), FindSegmentToLinkLoopToLoopByDelaunayDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Find Segment ToLink LoopToLoop"), FindSegmentToLinkLoopToLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Find Segment ToLink InnerToLoop"), FindSegmentToLinkInnerToLoopDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Select Segment ToLink InnerToLoop"), SelectSegmentToLinkInnerToLoopsDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Mesh Over Cycle"), TriangulateOverCycleDuration);
			FChrono::PrintClockElapse(Log, TEXT("    "), TEXT("Mesh Inner Nodes"), TriangulateInnerNodesDuration);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate1"), TriangulateDuration1);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate2"), TriangulateDuration2);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate3"), TriangulateDuration3);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate4"), TriangulateDuration4);
			FChrono::PrintClockElapse(Log, TEXT("  "), TEXT("Triangulate "), TriangulateDuration);
		}
	};


	class FIsoTriangulator
	{
		friend class FParametricMesher;
	
	protected:

		const FGrid& Grid;
		TSharedRef<FFaceMesh> Mesh;

		TArray<int32> LoopStartIndex;
		TArray<FLoopNode> LoopNodes;
		int32 LoopNodeCount = 0;
		TArray<FLoopNode*> SortedLoopNodes;

		/**
		 * GlobalIndexToIsoInnerNodes contains only inner nodes of the grid, if GlobalIndexToIsoInnerNodes[Index] == null, the point is outside the domain
		 */
		TArray<FIsoInnerNode*> GlobalIndexToIsoInnerNodes;
		
		/**
		 * Static array of InnerNodes. Only used for allocation needs 
		 */
		TArray<FIsoInnerNode> InnerNodes;
		int32 InnerNodeCount = 0;



		TFactory<FIsoSegment> IsoSegmentFactory;

		TArray<FIsoSegment*> LoopSegments;
		TArray<FIsoSegment*> ThinZoneSegments;
		TArray<FIsoSegment*> FinalInnerSegments;
		TArray<FIsoSegment*> InnerToOuterSegments;

		/**
		 * Tool to check if a segment intersect or not existing segments.
		 * To be optimal, depending on the segment, only a subset of segment is used. 
		 * Checks intersection with loop.
		 */
		FIntersectionSegmentTool LoopSegmentsIntersectionTool;
		FIntersectionSegmentTool InnerSegmentsIntersectionTool;
		FIntersectionSegmentTool InnerToLoopSegmentsIntersectionTool;
		FIntersectionSegmentTool InnerToOuterSegmentsIntersectionTool;


		/**
		 * Define all the lower left index of grid node that the upper cell is surrounding a loop
		 * This is set in FindSegmentIsoUVSurroundingSmallLoop
		 * and use in TriangulateInnerNodes to don't generate the both cell triangles 
		 */
		TArray<int32> IndexOfLowerLeftInnerNodeSurroundingALoop;

		/**
		 * Segments to link inner to boundary and boundary to boundary
		 * From FindIsoSegmentToLinkInnerToBoundary
		 * SelectSegmentInCandidateSegments
		 */
		TArray<FIsoSegment*> FinalToLoops;

		/**
		 *
		 */
		TArray<FIsoSegment*> CandidateSegments;

		/**
		 * Segments to link boundary to boundary and to complete the mesh
		 */
		TArray<FIsoSegment*> CandidateLoopToLoopSegments;

		/**
		 * Segments to link inner to boundary and to complete the mesh
		 */
		TArray<FIsoSegment*> CandidateInnerToLoopSegments;

		TArray<FIsoSegment*> NewTestSegments;

		bool bDisplay = false;

	public:

		FIsoTriangulator(const FGrid& InGrid, TSharedRef<FFaceMesh> EntityMesh);

		/**
		 * Main method
		 * @return false if the tessellation failed
		 */
		bool Triangulate();

		void BuildNodes();

		/**
		 * Build the segments of the loops and check if each loop is self intersecting.
		 * @return false if the loop is self intersecting
		 */
		bool BuildLoopSegments();

		void BuildInnerSegments();

		/**
		 * Add temporary loops defining thin zones to avoid the tessellation of these zone.
		 * These zones are tessellated in a specific process
		 */
		void BuildThinZoneSegments();

		/**
		 * Fill mesh node data (Position, normal, UV, Index) of the FFaceMesh object
		 */
		void FillMeshNodes();

		/**
		 * Build the Inner Segments Intersection Tool used to check if a candidate segment crosses the inner mesh
		 * 
		 * The minimal set of segments of the intersection tool is the boundaries of the inner triangulation.
		 * 
		 * https://docs.google.com/presentation/d/1qUVOH-2kU_QXBVKyRUcdDy1Y6WGkcaJCiaS8wGjSZ6M/edit?usp=sharing
		 * Slide "Boundary Segments Of Inner Triangulation"
		 */
		void BuildInnerSegmentsIntersectionTool();

		/**
		 * The purpose of the method is to add surrounding segments (boundary of an unitary inner grid cell) to the small loop to intersection tool to prevent traversing inner segments
		 * A loop is inside inner segments
		 *									|			 |
		 *								   -----------------
		 *									|	 XXX	 |
		 *									|	XXXXX	 |
		 *									|	 XXX	 |
		 *								   -----------------
		 *									|			 |
		 * https://docs.google.com/presentation/d/1qUVOH-2kU_QXBVKyRUcdDy1Y6WGkcaJCiaS8wGjSZ6M/edit?usp=sharing
		 * Slide "Find Inner Grid Cell Surrounding Small Loop"
		 * This method finalizes BuildInnerSegmentsIntersectionTool
		 */	
		void FindInnerGridCellSurroundingSmallLoop();

#ifdef NEED_TO_CHECK_USEFULNESS
		/**
		 * 
		 */
		void CompleteIsoSegmentLoopToLoop();
#endif

		void ConnectCellLoops();
		void FindCellContainingBoundaryNodes(TArray<FCell>& Cells);

		/**
		 * The closest loops are connected together
		 * To do it, a Delaunay triangulation of the loop barycenters is realized.
		 * Each edge of this mesh defined a near loops pair
		 * The shortest segment is then build between this pair of loops
		 */
		void ConnectCellLoopsByNeighborhood(FCell& cell);

		/**
		 * 2nd step
		 */
		void ConnectCellCornerToInnerLoop(FCell& Cell);

		/**
		 * 3rd step : IsoSegment linking 
		 */
		void FindIsoSegmentToLinkOuterLoopNodes(FCell& Cell);
		void FindIsoSegmentToLinkOuterLoopNodes2(FCell& Cell);

		/**
		 * 4th step : If their is no segment candidate  
		 */
		void FindSegmentToLinkOuterLoopNodes(FCell& Cell);

		/**
		 * 5th step : try to connect outer loop Extremities
		 */
		void AddSementToLinkOuterLoopExtremities(FCell& Cell);

		/**
		 * The goal of this algorithm is to connect iso U (or V) aligned loop nodes as soon as they are nearly in the same iso V (or U) strip.
		 * I.e.:
		 * - Iso U aligned: NodeA.U = NodeB.U +/-TolU 
		 * - In the same strip: each node of the segment has the same index "i" that verify: isoV[i] - TolV < Node.V < isoV[i+1] + TolV
		 */
		void FindIsoSegmentToLinkLoopToLoop();
		void LastChanceToCreateSegmentInCell(FCell& Cell);

		/**
		 * The goal of this algorithm is to connect inner node (node of the grid UV) to iso aligned loop node when they are in the same iso V (or U) strip.
		 * I.e.:
		 * - Iso U aligned: NodeA.U < NodeB.U  
		 * - In the same strip: each node of the segment has the same index "i" that verify: isoV[i-1] - TolV < BoundaryNode.V or BoundaryNode.V < isoV[i+1] + TolV
		 * https://docs.google.com/presentation/d/1qUVOH-2kU_QXBVKyRUcdDy1Y6WGkcaJCiaS8wGjSZ6M/edit?usp=sharing
		 * Slide "Find Iso Segment To Link Inner To Loop"
		 */
		void FindIsoSegmentToLinkInnerToLoop();

		/**
		 * The purpose of the method is select a minimal set of segments connecting loops together
		 * The final segments will be selected with SelectSegmentInCandidateSegments
		 */
		void ConnectCellLoopsByNeighborhood();  // to rename and clean

#ifdef TODELETE
		void FindSegmentToLinkLoopToLoop();
#endif

		/**
		 * The purpose of the method is select a minimal set of segments
		 * The final segments will be selected with SelectSegmentInCandidateSegments
		 */
		void FindCandidateSegmentsToLinkInnerToLoop();


		void FindCandidateSegmentsToLinkInnerAndLoop();

		/**
		 * Complete the final set of segments with the best subset of segments in CandidateSegments
		 */
		void SelectSegmentInCandidateSegments();

		/**
		 * The goal of this algorithm is to connect unconnected inner segment extremity i.e. extremity with one or less connected segment to the closed boundary node
		 */
		void ConnectUnconnectedInnerSegments();

		/**
		 * Finalize the tesselation between inner grid boundary and loops.
		 * The final set of segments define a network
		 * Each minimal cycle is tessellated independently
		 */
		void TriangulateOverCycle(const EGridSpace Space);

		/**
		 * Find in the network a minimal cylce stating from a segment
		 */
		void FindCycle(FIsoSegment* StartSegment, bool bLeftSide, TArray<FIsoSegment*>& Cycle, TArray<bool>& CycleOrientation);

		/**
		 * Generate the "Delaunay" tessellation of the cycle. 
		 * The algorithm is based on frontal process
		 */
		void MeshCycle(const EGridSpace Space, const TArray<FIsoSegment*>& cycle, const TArray<bool>& cycleOrientation);

		/**
		 * Finalization of the mesh by the tessellation of the inner grid
		 */
		void TriangulateInnerNodes();

		/**
		 * Sorted loop node array used to have efficient loop proximity node research
		 */
		void SortLoopNodes()
		{
			SortedLoopNodes.Reserve(LoopNodes.Num());
			for (FLoopNode& LoopNode : LoopNodes)
			{
				SortedLoopNodes.Add(&LoopNode);
			}

			Algo::Sort(SortedLoopNodes, [this](const FLoopNode* Node1, const FLoopNode* Node2)
				{
					const FPoint2D& Node1Coordinates = Node1->Get2DPoint(EGridSpace::Default2D, Grid);
					const FPoint2D& Node2Coordinates = Node2->Get2DPoint(EGridSpace::Default2D, Grid);
					return (Node1Coordinates.U + Node1Coordinates.V) < (Node2Coordinates.U + Node2Coordinates.V);
				});
		}

			
	private:

		FIsoSegment* FindNextSegment(EGridSpace Space, const FIsoSegment* StartSegment, const FIsoNode* StartNode, FGetSlop GetSlop) const;

		//void CreateLoopToLoopSegment(FLoopNode* NodeA, const FPoint2D& ACoordinates, FLoopNode* NodeB, const FPoint2D& BCoordinates, TArray<FIsoSegment*>& NewSegments);

		// ==========================================================================================
		// 	   Create segments
		// ==========================================================================================
		void TryToConnectTwoLoopsWithShortestSegment(FCell& Cell, int32 IndexLoopA, int32 IndexLoopB);
		void TryToConnectTwoLoopsWithShortestSegment(FCell& Cell, const TArray<FLoopNode*>& LoopA, int32 IndexLoopB);
		void TryToConnectTwoLoopsWithShortestSegment(FCell& Cell, const TArray<FLoopNode*>& LoopA, const TArray<FLoopNode*>& LoopB);
		void TryToConnectTwoLoopsWithTheMostIsoSegment(FCell& Cell, const TArray<FLoopNode*>& LoopA, const TArray<FLoopNode*>& LoopB);
		void TryToCreateSegment(FCell& Cell, FLoopNode* NodeA, const FPoint2D& ACoordinates, FIsoNode* NodeB, const FPoint2D& BCoordinates, const double FlatAngle);

#ifdef CADKERNEL_DEV
	public:
		FIsoTriangulatorChronos Chronos;

		void DisplayPixels(TArray<uint8>& Pixel) const;

		void DisplayPixel(const int32 Index) const;
		void DisplayPixel(const int32 IndexU, const int32 IndexV) const;

		void DisplayIsoNodes(EGridSpace Space) const;
		void Display(EGridSpace Space, const FIsoNode& Node, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::BluePoint) const;
		void Display(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::Element) const;
		void Display(EGridSpace Space, const FIsoSegment& Segment, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::Element, bool bDisplayOrientation = false) const;
		void Display(EGridSpace Space, const TCHAR* Message, const TArray<FIsoSegment*>& Segment, bool bDisplayNode, bool bDisplayOrientation = false, EVisuProperty Property = EVisuProperty::Element) const;

		void DisplayCells(const TArray<FCell>& Cells) const;
		void DisplayCell(const FCell& Cell) const;

		void DisplayTriangle(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, const FIsoNode& NodeC) const;
#endif

	};


} // namespace CADKernel

