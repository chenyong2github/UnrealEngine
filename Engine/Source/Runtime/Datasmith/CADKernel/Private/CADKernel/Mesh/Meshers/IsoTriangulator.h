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
//#define DEBUG_DELAUNAY
namespace CADKernel
{

class FGrid;
class FIntersectionSegmentTool;
class FFaceMesh;
struct FCell;


namespace IsoTriangulatorImpl
{
typedef TFunction<double(const FPoint2D&, const FPoint2D&, double)> GetSlopMethod;
typedef TFunction<FLoopNode* (FLoopNode*)> GetNextNodeMethod;
typedef TFunction<const FLoopNode* (const FLoopNode*)> GetNextConstNodeMethod;
typedef TFunction<const FLoopNode* (const FIsoSegment*)> GetSegmentToNodeMethod;
typedef TPair<FLoopNode*, FLoopNode*> FLoopSection;
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

	FGrid& Grid;
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

	bool bNeedCheckOrientation = false;

	static const double GeometricToMeshingToleranceFactor;
	const double GeometricTolerance;
	const double SquareGeometricTolerance;
	const double SquareGeometricTolerance2;
	const double MeshingTolerance;
	const double SquareMeshingTolerance;

public:

	FIsoTriangulator(FGrid& InGrid, TSharedRef<FFaceMesh> EntityMesh);

	/**
	 * Main method
	 * @return false if the tessellation failed
	 */
	bool Triangulate();

	void BuildNodes();

	bool FindLoopIntersectionAndFixIt();

	/**
	 * Build the segments of the loops and check if each loop is self intersecting.
	 * @return false if the loop is self intersecting
	 */
	void BuildLoopSegments();

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
	void ConnectCellSubLoopsByNeighborhood(FCell& cell);

	/**
	 * 2nd step
	 */
	void ConnectCellCornerToInnerLoop(FCell& Cell);

	/**
	 * 3rd step : IsoSegment linking
	 */
	void FindIsoSegmentToLinkOuterLoopNodes(FCell& Cell);

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
#ifdef UNUSED_TO_DELETE_
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
#endif

	/**
	 * The purpose of the method is select a minimal set of segments connecting loops together
	 * The final segments will be selected with SelectSegmentInCandidateSegments
	 */
	void ConnectCellSubLoopsByNeighborhood();  // to rename and clean

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
	 * Finalize the tessellation between inner grid boundary and loops.
	 * The final set of segments define a network
	 * Each minimal cycle is tessellated independently
	 */
	void TriangulateOverCycle(const EGridSpace Space);

	/**
	 * Find in the network a minimal cycle stating from a segment
	 */
	void FindCycle(FIsoSegment* StartSegment, bool bLeftSide, TArray<FIsoSegment*>& Cycle, TArray<bool>& CycleOrientation);

	/**
	 * Generate the "Delaunay" tessellation of the cycle.
	 * The algorithm is based on frontal process
	 */
	void MeshCycle(const EGridSpace Space, const TArray<FIsoSegment*>& cycle, const TArray<bool>& cycleOrientation);

	bool CanCycleBeMeshed(const TArray<FIsoSegment*>& Cycle, FIntersectionSegmentTool& CycleIntersectionTool);

	bool TryToRemoveSelfIntersectionByMovingTheClosedOusidePoint(const FIsoSegment& Segment0, const FIsoSegment& Segment1);
	bool TryToRemoveIntersectionByMovingTheClosedOusidePoint(const FIsoSegment& Segment0, const FIsoSegment& Segment1);
	void RemoveIntersectionByMovingOutsideSegmentNodeInside(const FIsoSegment& IntersectingSegment, const FIsoSegment& Segment);

	bool TryToRemoveIntersectionBySwappingSegments(FIsoSegment& Segment0, FIsoSegment& Segment1);                                  // ok

		/**
		 * Segment and the segment next (or before Segment) are intersecting IntersectingSegment
		 * The common node is moved
		 */
	bool TryToRemoveIntersectionOfTwoConsecutiveIntersectingSegments(const FIsoSegment& IntersectingSegment, FIsoSegment& Segment); // ok 

	bool RemoveNodeOfLoop(FLoopNode& NodeToRemove);
	void RemovePickOfLoop(FIsoSegment& Segment);

	bool RemovePickRecursively(FLoopNode* Node0, FLoopNode* Node1);

	bool CheckAndRemovePick(const FPoint2D& PreviousPoint, const FPoint2D& NodeToRemovePoint, const FPoint2D& NextPoint, FLoopNode& NodeToRemove)
	{
		double Slop = ComputeUnorientedSlope(NodeToRemovePoint, PreviousPoint, NextPoint);
		bool bRemoveNode = false;
		if (Slop < 0.03)
		{
			bRemoveNode = true;
		}
		else if (Slop < 0.1)
		{
			double SquareDistance1 = SquareDistanceOfPointToSegment(PreviousPoint, NodeToRemovePoint, NextPoint);
			double SquareDistance2 = SquareDistanceOfPointToSegment(NextPoint, NodeToRemovePoint, PreviousPoint);
			double MinSquareDistance = FMath::Min(SquareDistance1, SquareDistance2);
			if (MinSquareDistance < SquareGeometricTolerance2)
			{
				bRemoveNode = true;
			}
		}

		if (bRemoveNode)
		{
			return RemoveNodeOfLoop(NodeToRemove);
		}
		return false;
	};

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

	// Methods used by FindLoopIntersectionAndFixIt Step1
	void FindBestLoopExtremity(TArray<FLoopNode*>& BestStartNodeOfLoop);
	void FindLoopIntersections(const TArray<FLoopNode*>& LoopNodes, bool bForward, TArray<TPair<double, double>>& OutIntersections);
	EOrientation GetLoopOrientation(const FLoopNode* StartNode);
	void FixLoopOrientation(const TArray<FLoopNode*>& LoopNodes);
	void SwapLoopOrientation(int32 FirstSegmentIndex, int32 LastSegmentIndex);
	void MoveIntersectingSectionBehindOppositeSection(IsoTriangulatorImpl::FLoopSection IntersectingSection, IsoTriangulatorImpl::FLoopSection OppositeSection, IsoTriangulatorImpl::GetNextNodeMethod GetNext, IsoTriangulatorImpl::GetNextNodeMethod GetPrevious);
	void OffsetSegment(FIsoSegment& Segment, TSegment<FPoint2D>& Segment2D, TSegment<FPoint2D>& IntersectingSegment2D);
	void OffsetNode(FLoopNode& Node, TSegment<FPoint2D>& IntersectingSegment2D);

	/**
	 * If the new position of the node is closed to the previous or next node position, it will be delete
	 */
	void MoveNode(FLoopNode& NodeToMove, FPoint2D& ProjectedPoint);

	/**
	 * @return false if the SubLoop is bigger than the other part of the loop
	 */
	bool RemoveLoopIntersections(const TArray<FLoopNode*>& NodesOfLoop, TArray<TPair<double, double>>& Intersections, bool bForward);

	void RemoveLoopPicks(TArray<FLoopNode*>& NodesOfLoop, TArray<TPair<double, double>>& Intersections);

	void RemoveThePick(const TArray<FLoopNode*>& NodesOfLoop, const TPair<double, double>& Intersection, bool bForward);
	void SwapNodes(const TArray<FLoopNode*>& NodesOfLoop, const TPair<double, double>& Intersection, bool bForward);

	/**
	 * Case0: __     ___        __      ___
	 *          \   /             \    /
	 *           o o        =>     o  o
	 *        __/   \___		__/    \___
	 *
	 * Case1:    ____    ___        ____    ___
	 *           |   \  /   |       |   \  /   |
	 *           |    oo    |   =>  |    o     |
	 *           |___/  \___|       |          |
	 *                              |    o     |
	 * 								|___/  \___|
	 */
	bool SpreadCoincidentNodes(const TArray<FLoopNode*>& NodesOfLoop, TPair<double, double> Intersection);

	/**
	 * Case:  o_________o          o_________o
	 *             o         =>
	 *          __/ \___		        o
	 *								 __/ \___
	 */
	bool MovePickBehind(const TArray<FLoopNode*>& NodesOfLoop, TPair<double, double> Intersection, bool bKeyIsExtremity);

	/**
	 * Two cases:
	 *    - the segments of the intersection a closed parallel and in same orientation. in this case, the sub-loop is a long pick. The sub-Loop is delete
	 *    - the loop is an inner loop closed to the border:
	 *       _____________                _____________
	 *   |  /             \			  |__/             \
	 *    \/               |     =>                     |
	 *    /\               |		   __               |
	 *   |  \_____________/			  |  \_____________/
	 *
	 */
	void TryToSwapSegmentsOrRemoveLoop(const TArray<FLoopNode*>& NodesOfLoop, const TPair<double, double>& Intersection, bool bForward);

	bool RemovePickToOutside(const TArray<FLoopNode*>& NodesOfLoop, const TPair<double, double>& Intersection, const TPair<double, double>& NextIntersection, bool bForward);

	/**
	 * @return false if the SubLoop is bigger than the other part of the loop
	 */
	bool RemoveIntersectionsOfSubLoop(const TArray<FLoopNode*>& LoopNodesFromStartNode, TArray<TPair<double, double>>& Intersections, int32 IntersectionIndex, int32 IntersectionCount, bool bForward);
	bool RemoveUniqueIntersection(const TArray<FLoopNode*>& LoopNodesFromStartNode, TPair<double, double> Intersection, bool bForward);
	//void RemoveSubLoop(const TArray<FLoopNode*>& NodesOfLoop, const TPair<double, double>& Intersection, bool bForward);
	void RemoveSubLoop(FLoopNode* StartNode, FLoopNode* EndNode, IsoTriangulatorImpl::GetNextNodeMethod GetNext);

	/**
	 * Check if the sub-loop is not bigger than the main loop.
	 * If the segment count of the SubLoop is bigger than 1/3 of the segment count of the main loop
	 * the length of each part are compute.
	 * If the sub-loop length is bigger than the main loop^, there is a problem (problem orientation ?)
	 * The surface meshing is canceled to avoid a bigger problem during the meshing step, that could cause a process crash.
	 */
	bool IsSubLoopBiggerThanMainLoop(const TArray<FLoopNode*>& LoopNodesFromStartNode, const TPair<double, double>& Intersection, bool bForward);

	void RemoveIntersectionByMovingOutsideNodeInside(const FIsoSegment& IntersectingSegment, FLoopNode& NodeToMove);

	bool CheckMainLoopConsistency();

	// Methods used by FindLoopIntersectionAndFixIt Step2
	void FillIntersectionToolWithOuterLoop();

	void FixIntersectionBetweenLoops();

	FIsoSegment* FindNextSegment(EGridSpace Space, const FIsoSegment* StartSegment, const FIsoNode* StartNode, IsoTriangulatorImpl::GetSlopMethod GetSlop) const;



#ifdef UNUSED_TO_DELETE_
	void CreateLoopToLoopSegment(FLoopNode* NodeA, const FPoint2D& ACoordinates, FLoopNode* NodeB, const FPoint2D& BCoordinates, TArray<FIsoSegment*>& NewSegments);
#endif
	// ==========================================================================================
	// 	   Create segments
	// ==========================================================================================

	/**
	 *  SubLoopA                  SubLoopB
	 *      --X---X             X-----X--
	 *             \           /
	 *              \         /
	 *               X=======X
	 *              /         \
	 *             /           \
	 *      --X---X             X-----X--
	 *
	 *     ======= ShortestSegment
	 */
	void TryToConnectTwoSubLoopsWithShortestSegment(FCell& Cell, const TArray<FLoopNode*>& SubLoopA, const TArray<FLoopNode*>& SubLoopB);

	void TryToConnectTwoLoopsWithIsocelesTriangle(FCell& Cell, const TArray<FLoopNode*>& SubLoopA, const TArray<FLoopNode*>& SubLoopB);

	/**
	 *    --X------X-----X--  <-- SubLoopA
	 *             I
	 *             I    <- The most isoSegment
	 *             I
	 *    ----X----X--------  <-- SubLoopB
	 *
	 */
	void TryToConnectTwoSubLoopsWithTheMostIsoSegment(FCell& Cell, const TArray<FLoopNode*>& SubLoopA, const TArray<FLoopNode*>& SubLoopB);

	/**
	 *    X--X------X-----X--  <-- SubLoop
	 *    |         I
	 *    |         I   <- The most isoSegment
	 *    |         I
	 *    X----X----X--------
	 *
	 */
	void TryToConnectVertexSubLoopWithTheMostIsoSegment(FCell& Cell, const TArray<FLoopNode*>& SubLoop);
	bool TryToCreateSegment(FCell& Cell, FLoopNode* NodeA, const FPoint2D& ACoordinates, FIsoNode* NodeB, const FPoint2D& BCoordinates, const double FlatAngle);

#ifdef CADKERNEL_DEV
public:
	FIsoTriangulatorChronos Chronos;

	void DisplayPixels(TArray<uint8>& Pixel) const;

	void DisplayPixel(const int32 Index) const;
	void DisplayPixel(const int32 IndexU, const int32 IndexV) const;

	void DisplayIsoNodes(EGridSpace Space) const;
	void Display(EGridSpace Space, const FIsoNode& Node, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::BluePoint) const;
	void Display(FString Message, EGridSpace Space, const TArray<const FIsoNode*>& Points, EVisuProperty Property) const;

	void Display(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::Element) const;
	void Display(EGridSpace Space, const FIsoSegment& Segment, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::Element, bool bDisplayOrientation = false) const;
	void Display(EGridSpace Space, const TCHAR* Message, const TArray<FIsoSegment*>& Segment, bool bDisplayNode, bool bDisplayOrientation = false, EVisuProperty Property = EVisuProperty::Element) const;

	void DisplayLoops(EGridSpace Space, const TCHAR* Message, const TArray<FLoopNode>& Nodes, bool bDisplayNode, EVisuProperty Property = EVisuProperty::Element) const;
	void DisplayLoops(const TCHAR* Message, bool bOneNode = true, bool bSplitBySegment = false, bool bDisplayNext = false, bool bDisplayPrevious = false) const;
	void DisplayLoop(EGridSpace Space, const TCHAR* Message, const TArray<FLoopNode*>& Nodes, bool bDisplayNode, EVisuProperty Property = EVisuProperty::Element) const;

	void DisplayCycle(const TArray<FIsoSegment*>& Cycle, const TCHAR* Message) const;

	void DisplayCells(const TArray<FCell>& Cells) const;
	void DisplayCell(const FCell& Cell) const;
	void DrawCellBoundary(int32 Index, EVisuProperty Property) const;

	void DisplayTriangle(EGridSpace Space, const FIsoNode& NodeA, const FIsoNode& NodeB, const FIsoNode& NodeC) const;
#endif

};

namespace IsoTriangulatorImpl
{

/**
 * Criteria to find the optimal "Delaunay" triangle starting from the segment AB to a set of point P
 * A "Delaunay" triangle is an equilateral triangle
 * The optimal value is the smallest value.
 */
inline double CotangentCriteria(const FPoint& APoint, const FPoint& BPoint, const FPoint& PPoint, FPoint& OutNormal)
{
	const double BigValue = HUGE_VALUE;

	FPoint PA = APoint - PPoint;
	FPoint PB = BPoint - PPoint;

	// the ratio between the scalar product PA.PB (=|PA| |PB| cos (A,P,B) )
	// with the norm of the cross product |PA^PB| (=|PA| |PB| |sin(A,P,B)|)
	// is compute. 
	double ScalareProduct = PA * PB;
	OutNormal = PA ^ PB;
	double NormOFScalarProduct = sqrt(OutNormal * OutNormal);

	// PPoint is aligned with (A,B)
	if (NormOFScalarProduct < SMALL_NUMBER)
	{
		return BigValue;
	}

	// return Cotangent value 
	return ScalareProduct / NormOFScalarProduct;
}

inline double CotangentCriteria(const FPoint2D& APoint, const FPoint2D& BPoint, const FPoint2D& PPoint)
{
	const double BigValue = HUGE_VALUE;

	FPoint2D PA = APoint - PPoint;
	FPoint2D PB = BPoint - PPoint;

	// the ratio between the scalar product PA.PB (=|PA| |PB| cos (A,P,B) )
	// with the norm of the cross product |PA^PB| (=|PA| |PB| |sin(A,P,B)|)
	// is compute. 
	double ScalareProduct = PA * PB;
	double OutNormal = PA ^ PB;
	double NormOFPointProduct = FMath::Abs(OutNormal);

	if (NormOFPointProduct < SMALL_NUMBER)
	{
		// PPoint is aligned with (A,B)
		return BigValue;
	}

	// return Cotangent value 
	return ScalareProduct / NormOFPointProduct;
}

template<class PointType>
inline double IsoscelesCriteria(const PointType& APoint, const PointType& BPoint, const PointType& IsoscelesVertex)
{
	double Coord = CoordinateOfProjectedPointOnSegment(IsoscelesVertex, APoint, BPoint, false);
	return FMath::Abs(Coord - 0.5);
}

template<class PointType>
inline double EquilateralCriteria(const PointType& SegmentA, const PointType& SegmentB, const PointType& Point)
{
	double Criteria1 = FMath::Abs(CoordinateOfProjectedPointOnSegment(SegmentA, SegmentB, Point, false) - 0.5);
	double Criteria2 = FMath::Abs(CoordinateOfProjectedPointOnSegment(Point, SegmentA, SegmentB, false) - 0.5);
	double Criteria3 = FMath::Abs(CoordinateOfProjectedPointOnSegment(SegmentB, Point, SegmentA, false) - 0.5);
	return Criteria1 + Criteria2 + Criteria3;
}

inline FLoopNode* GetNextNodeImpl(FLoopNode* Node)
{
	return &Node->GetNextNode();
}

inline FLoopNode* GetPreviousNodeImpl(FLoopNode* Node)
{
	return &Node->GetPreviousNode();
}

inline const FLoopNode* GetNextConstNodeImpl(const FLoopNode* Node)
{
	return &Node->GetNextNode();
}

inline const FLoopNode* GetPreviousConstNodeImpl(const FLoopNode* Node)
{
	return &Node->GetPreviousNode();
}

inline const FLoopNode* GetFirstNode(const FIsoSegment* Segment)
{
	return (const FLoopNode*)&Segment->GetFirstNode();
};

inline const FLoopNode* GetSecondNode(const FIsoSegment* Segment)
{
	return (const FLoopNode*)&Segment->GetSecondNode();
};

inline void GetLoopNodeStartingFrom(FLoopNode* StartNode, bool bForward, TArray<FLoopNode*>& Loop)
{
	using namespace IsoTriangulatorImpl;
	GetNextNodeMethod GetNext = bForward ? GetNextNodeImpl : GetPreviousNodeImpl;
	FLoopNode* Node = StartNode;
	Loop.Add(StartNode);
	for (Node = GetNext(Node); Node != StartNode; Node = GetNext(Node))
	{
		Loop.Add(Node);
	}
}

inline FLoopNode* GetNodeAt(const TArray<FLoopNode*>& NodesOfLoop, const int32 Index)
{
	FLoopNode* Node = NodesOfLoop[Index];
#ifdef DEBUG_LOOP_INTERSECTION_AND_FIX_IT		
	Wait(Node->IsDelete());
#endif
	ensureCADKernel(!Node->IsDelete());
	return Node;
};

inline int32 NextIndex(const int32 NodeCount, int32 StartIndex)
{
	++StartIndex;
	return (StartIndex >= NodeCount) ? 0 : StartIndex;
};

inline void RemoveDeletedNodes(TArray<FLoopNode*>& NodesOfLoop)
{
	int32 Index = NodesOfLoop.IndexOfByPredicate([](FLoopNode* Node) { return Node->IsDelete(); });
	if (Index == INDEX_NONE)
	{
		return;
	}
	int32 NewIndex = Index;
	for (; Index < NodesOfLoop.Num(); ++Index)
	{
		if (!NodesOfLoop[Index]->IsDelete())
		{
			NodesOfLoop[NewIndex++] = NodesOfLoop[Index];
		}
	}
	NodesOfLoop.SetNum(NewIndex);
}

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


} // namespace FIsoTriangulatorImpl

} // namespace CADKernel

