// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp Reducer

#pragma once

#include "MeshRefinerBase.h"
#include "QuadricError.h"
#include "Util/IndexPriorityQueue.h"

#include "DynamicMeshAttributeSet.h"


enum class ESimplificationResult
{
	Ok_Collapsed = 0,
	Ignored_CannotCollapse = 1,
	Ignored_EdgeIsFullyConstrained = 2,
	Ignored_EdgeTooLong = 3,
	Ignored_Constrained = 4,
	Ignored_CreatesFlip = 5,
	Failed_OpNotSuccessful = 6,
	Failed_NotAnEdge = 7
};

/**
 * Implementation of Garland & Heckbert Quadric Error Metric (QEM) Triangle Mesh Simplification
 */
template <typename QuadricErrorType>
class TMeshSimplification : public FMeshRefinerBase
{
public:

	typedef QuadricErrorType  FQuadricErrorType;

	/**
	 * If true, we try to find position for collapsed vertices that minimizes quadric error.
	 * If false we just use midpoints, which is actually significantly slower, because it results
	 * in may more points that would cause a triangle flip, which are then rejected.
	 */
	bool bMinimizeQuadricPositionError = true;

	/** if true, we try to keep boundary vertices on boundary. You probably want this. */
	bool bPreserveBoundaryShape = true;



	TMeshSimplification(FDynamicMesh3* m) : FMeshRefinerBase(m)
	{
		NormalOverlay = nullptr;
		if (m->Attributes())
		{
			NormalOverlay = m->Attributes()->PrimaryNormals();
		}
	}


	/**
	 * Simplify mesh until we reach a specific triangle count.
	 * Note that because triangles are removed in pairs, the resulting count may be TriangleCount-1
	 * @param TriangleCount the target triangle count
	 */
	virtual void SimplifyToTriangleCount(int TriangleCount);

	/**
	 * Simplify mesh until it has a specific vertex count
	 * @param VertexCount the target vertex count
	 */
	virtual void SimplifyToVertexCount(int VertexCount);

	/**
	 * Simplify mesh until no edges smaller than min length remain. This is not a great criteria.
	 * @param MinEdgeLength collapse any edge longer than this
	 */
	virtual void SimplifyToEdgeLength(double MinEdgeLength);

	/**
	 * Simplify mesh until the quadric error of an edge collapse exceeds the specified criteria.
	 * @param MaxError collapse an edge if the corresponding quadric error exceeds this 
	 */
	virtual void SimplifyToMaxError(double MaxError);

	/** 
	 * Does N rounds of collapsing edges longer than fMinEdgeLength. Does not use Quadrics or priority queue.
	 * This is a quick way to get triangle count down on huge meshes (eg like marching cubes output). 
	 * @param MinEdgeLength collapse any edge longer than this
	 * @param Rounds number of collapse rounds
	 * @param MeshIsClosedHint if you know the mesh is closed, this pass this true to avoid some precomputes
	 */
	virtual void FastCollapsePass(double MinEdgeLength, int Rounds = 1, bool bMeshIsClosedHint = false);



protected:

	TMeshSimplification()		// for subclasses that extend our behavior
	{
	}

	// this just lets us write more concise code
	bool EnableInlineProjection() const { return ProjectionMode == ETargetProjectionMode::Inline; }

	float MaxErrorAllowed = FLT_MAX;
	double MinEdgeLength = FMathd::MaxReal;
	int TargetCount = INT_MAX;
	enum class ETargetModes
	{
		TriangleCount = 0,
		VertexCount = 1,
		MinEdgeLength = 2,
		MaxError  = 3
	};
	ETargetModes SimplifyMode = ETargetModes::TriangleCount;



	/** Top-level function that does the simplification */
	virtual void DoSimplify();



	// StartEdges() and GetNextEdge() control the iteration over edges that will be refined.
	// Default here is to iterate over entire mesh->
	// Subclasses can override these two functions to restrict the affected edges (eg EdgeLoopRemesher)

	// We are using a modulo-index loop to break symmetry/pathological conditions. 
	const int ModuloPrime = 31337;     // any prime will do...
	int MaxEdgeID = 0;
	virtual int StartEdges() 
	{
		MaxEdgeID = Mesh->MaxEdgeID();
		return 0;
	}

	virtual int GetNextEdge(int CurEdgeID, bool& bDoneOut) 
	{
		int new_eid = (CurEdgeID + ModuloPrime) % MaxEdgeID;
		bDoneOut = (new_eid == 0);
		return new_eid;
	}




	TArray<FQuadricErrorType> vertQuadrics;
	virtual void InitializeVertexQuadrics();

	TArray<double> triAreas;
	TArray<FQuadricErrorType> triQuadrics;
	virtual void InitializeTriQuadrics();

	FDynamicMeshNormalOverlay* NormalOverlay;

	FQuadricErrorType ComputeFaceQuadric(const int tid, FVector3d& nface, FVector3d& c, double& Area) const;
	
	// internal class for priority queue
	struct QEdge 
	{
		int eid;
		FQuadricErrorType q;
		FVector3d collapse_pt;

		QEdge() { eid = 0; }

		QEdge(int edge_id, const FQuadricErrorType& qin, const FVector3d& pt) 
		{
			eid = edge_id;
			q = qin;
			collapse_pt = pt;
		}
	};

	TArray<QEdge> EdgeQuadrics;
	FIndexPriorityQueue EdgeQueue;

	struct FEdgeError
	{
		float error;
		int eid;
		bool operator<(const FEdgeError& e2) const
		{
			return error < e2.error;
		}
	};

	virtual void InitializeQueue();

	// return point that minimizes quadric error for edge [ea,eb]
	FVector3d OptimalPoint(int eid, const FQuadricErrorType& q, int ea, int eb);
	
	FVector3d GetProjectedPoint(const FVector3d& pos)
	{
		if (EnableInlineProjection() && ProjTarget != nullptr)
		{
			return ProjTarget->Project(pos);
		}
		return pos;
	}


	// update queue weight for each edge in vertex one-ring
	virtual void UpdateNeighbours(int vid, FIndex2i removedTris, FIndex2i opposingVerts);


	virtual void Reproject() 
	{
		ProfileBeginProject();
		if (ProjTarget != nullptr && ProjectionMode == ETargetProjectionMode::AfterRefinement)
		{
			FullProjectionPass();
			DoDebugChecks();
		}
		ProfileEndProject();
	}





	bool bHaveBoundary;
	TArray<bool> IsBoundaryVtxCache;
	void Precompute(bool bMeshIsClosed = false);

	inline bool IsBoundaryVertex(int vid) const
	{
		return IsBoundaryVtxCache[vid];
	}





	ESimplificationResult CollapseEdge(int edgeID, FVector3d vNewPos, int& collapseToV);



	// subclasses can override these to implement custom behavior...
	virtual void OnEdgeCollapse(int edgeID, int va, int vb, const FDynamicMesh3::FEdgeCollapseInfo& collapseInfo)
	{
		// this is for subclasses...
	}





	// Project vertices onto projection target. 
	virtual void FullProjectionPass();

	virtual void ProjectVertex(int vID, IProjectionTarget* targetIn);

	// used by collapse-edge to get projected position for new vertex
	virtual FVector3d GetProjectedCollapsePosition(int vid, const FVector3d& vNewPos);

	virtual void ApplyToProjectVertices(const TFunction<void(int)>& apply_f);



	/*
	 * testing/debug/profiling stuff
	 */
protected:


	//
	// profiling functions, turn on ENABLE_PROFILING to see output in console
	// 
	int COUNT_COLLAPSES;
	int COUNT_ITERATIONS;
	//Stopwatch AllOpsW, SetupW, ProjectW, CollapseW;

	virtual void ProfileBeginPass() 
	{
		if (ENABLE_PROFILING) 
		{
			COUNT_COLLAPSES = 0;
			COUNT_ITERATIONS = 0;
			//AllOpsW = new Stopwatch();
			//SetupW = new Stopwatch();
			//ProjectW = new Stopwatch();
			//CollapseW = new Stopwatch();
		}
	}

	virtual void ProfileEndPass()
	{
		if (ENABLE_PROFILING) 
		{
			//System.Console.WriteLine(string.Format(
			//	"ReducePass: T {0} V {1} collapses {2}  iterations {3}", mesh->TriangleCount, mesh->VertexCount, COUNT_COLLAPSES, COUNT_ITERATIONS
			//));
			//System.Console.WriteLine(string.Format(
			//	"           Timing1: setup {0} ops {1} project {2}", Util.ToSecMilli(SetupW.Elapsed), Util.ToSecMilli(AllOpsW.Elapsed), Util.ToSecMilli(ProjectW.Elapsed)
			//));
		}
	}

	virtual void ProfileBeginOps()
	{
		//if (ENABLE_PROFILING) AllOpsW.Start();
	}
	virtual void ProfileEndOps()
	{
		//if (ENABLE_PROFILING) AllOpsW.Stop();
	}
	virtual void ProfileBeginSetup()
	{
		//if (ENABLE_PROFILING) SetupW.Start();
	}
	virtual void ProfileEndSetup()
	{
		//if (ENABLE_PROFILING) SetupW.Stop();
	}

	virtual void ProfileBeginProject()
	{
		//if (ENABLE_PROFILING) ProjectW.Start();
	}
	virtual void ProfileEndProject() 
	{
		//if (ENABLE_PROFILING) ProjectW.Stop();
	}

	virtual void ProfileBeginCollapse() 
	{
		//if (ENABLE_PROFILING) CollapseW.Start();
	}
	virtual void ProfileEndCollapse() 
	{
		//if (ENABLE_PROFILING) CollapseW.Stop();
	}


};


// The simplifier
typedef TMeshSimplification< FAttrBasedQuadricErrord >  FAttrMeshSimplification;
typedef TMeshSimplification < FVolPresQuadricErrord >   FVolPresMeshSimplification;
typedef TMeshSimplification< FQuadricErrord >           FQEMSimplification;