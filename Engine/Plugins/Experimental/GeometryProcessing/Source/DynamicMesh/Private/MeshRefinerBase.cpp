// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshRefinerBase.h"
#include "DynamicMeshAttributeSet.h"






/*
* Check if edge collapse will create a face-normal flip.
* Also checks if collapse would violate link condition, since we are iterating over one-ring anyway.
* This only checks one-ring of vid, so you have to call it twice, with vid and vother reversed, to check both one-rings
*/
bool FMeshRefinerBase::CheckIfCollapseCreatesFlipOrInvalid(int vid, int vother, const FVector3d& newv, int tc, int td) const
{
	FVector3d va = FVector3d::Zero(), vb = FVector3d::Zero(), vc = FVector3d::Zero();
	for (int tid : Mesh->VtxTrianglesItr(vid)) 
	{
		if (tid == tc || tid == td)
		{
			continue;
		}
		FIndex3i curt = Mesh->GetTriangle(tid);
		if (curt[0] == vother || curt[1] == vother || curt[2] == vother)
		{
			return true;		// invalid nbrhood for collapse
		}
		Mesh->GetTriVertices(tid, va, vb, vc);
		FVector3d ncur = (vb - va).Cross(vc - va);
		double sign = 0;
		if (curt[0] == vid) 
		{
			FVector3d nnew = (vb - newv).Cross(vc - newv);
			sign = ComputeEdgeFlipMetric(ncur, nnew);
		}
		else if (curt[1] == vid) 
		{
			FVector3d nnew = (newv - va).Cross(vc - va);
			sign = ComputeEdgeFlipMetric(ncur, nnew);
		}
		else if (curt[2] == vid) 
		{
			FVector3d nnew = (vb - va).Cross(newv - va);
			sign = ComputeEdgeFlipMetric(ncur, nnew);
		}
		else
		{
			check(false);   // this should never happen!
		}
		if (sign <= EdgeFlipTolerance)
		{
			return true;
		}
	}
	return false;
}




/**
 * Check if edge flip might reverse normal direction.
 * Not entirely clear on how to best implement this test. Currently checking if any normal-pairs are reversed.
 */
bool FMeshRefinerBase::CheckIfFlipInvertsNormals(int a, int b, int c, int d, int t0) const
{
	FVector3d vC = Mesh->GetVertex(c), vD = Mesh->GetVertex(d);
	FIndex3i tri_v = Mesh->GetTriangle(t0);
	int oa = a, ob = b;
	IndexUtil::OrientTriEdge(oa, ob, tri_v);
	FVector3d vOA = Mesh->GetVertex(oa), vOB = Mesh->GetVertex(ob);
	FVector3d n0 = VectorUtil::FastNormalDirection(vOA, vOB, vC);
	FVector3d n1 = VectorUtil::FastNormalDirection(vOB, vOA, vD);
	FVector3d f0 = VectorUtil::FastNormalDirection(vC, vD, vOB);
	if (ComputeEdgeFlipMetric(n0, f0) <= EdgeFlipTolerance || ComputeEdgeFlipMetric(n1, f0) <= EdgeFlipTolerance)
	{
		return true;
	}
	FVector3d f1 = VectorUtil::FastNormalDirection(vD, vC, vOA);
	if (ComputeEdgeFlipMetric(n0, f1) <= EdgeFlipTolerance || ComputeEdgeFlipMetric(n1, f1) <= EdgeFlipTolerance)
	{
		return true;
	}

	// this only checks if output faces are pointing towards eachother, which seems 
	// to still result in normal-flips in some cases
	//if (f0.Dot(f1) < 0)
	//    return true;

	return false;
}






/**
 * Figure out if we can collapse edge eid=[a,b] under current constraint set.
 * First we resolve vertex constraints using CanCollapseVertex(). However this
 * does not catch some topological cases at the edge-constraint level, which
 * which we will only be able to detect once we know if we are losing a or b.
 * See comments on CanCollapseVertex() for what collapse_to is for.
 */
bool FMeshRefinerBase::CanCollapseEdge(int eid, int a, int b, int c, int d, int tc, int td, int& collapse_to) const
{
	collapse_to = -1;
	if (Constraints == nullptr)
	{
		return true;
	}
	bool bVtx = CanCollapseVertex(eid, a, b, collapse_to);
	if (bVtx == false)
	{
		return false;
	}

	// when we lose a vtx in a collapse, we also lose two edges [iCollapse,c] and [iCollapse,d].
	// If either of those edges is constrained, we would lose that constraint.
	// This would be bad.
	int iCollapse = (collapse_to == a) ? b : a;
	if (c != IndexConstants::InvalidID) 
	{
		int ec = Mesh->FindEdgeFromTri(iCollapse, c, tc);
		if (Constraints->GetEdgeConstraint(ec).IsUnconstrained() == false)
		{
			return false;
		}
	}
	if (d != IndexConstants::InvalidID)
	{
		int ed = Mesh->FindEdgeFromTri(iCollapse, d, td);
		if (Constraints->GetEdgeConstraint(ed).IsUnconstrained() == false)
		{
			return false;
		}
	}

	return true;
}







/**
 * Resolve vertex constraints for collapsing edge eid=[a,b]. Generally we would
 * collapse a to b, and set the new position as 0.5*(v_a+v_b). However if a *or* b
 * are constrained, then we want to keep that vertex and collapse to its position.
 * This vertex (a or b) will be returned in collapse_to, which is -1 otherwise.
 * If a *and* b are constrained, then things are complicated (and documented below).
 */
bool FMeshRefinerBase::CanCollapseVertex(int eid, int a, int b, int& collapse_to) const
{
	collapse_to = -1;
	if (Constraints == nullptr)
	{
		return true;
	}
	FVertexConstraint ca, cb;
	Constraints->GetVertexConstraint(a, ca);
	Constraints->GetVertexConstraint(b, cb);

	// no constraint at all
	if (ca.Fixed == false && cb.Fixed == false && ca.Target == nullptr && cb.Target == nullptr)
	{
		return true;
	}

	// handle a or b fixed
	if (ca.Fixed == true && cb.Fixed == false) 
	{
		// if b is fixed to a target, and it is different than a's target, we can't collapse
		if (cb.Target != nullptr && cb.Target != ca.Target)
		{
			return false;
		}
		collapse_to = a;
		return true;
	}
	if (cb.Fixed == true && ca.Fixed == false) 
	{
		if (ca.Target != nullptr && ca.Target != cb.Target)
		{
			return false;
		}
		collapse_to = b;
		return true;
	}
	// if both fixed, and options allow, treat this edge as unconstrained (eg collapse to midpoint)
	// [RMS] tried picking a or b here, but something weird happens, where
	//   eg cylinder cap will entirely erode away. Somehow edge lengths stay below threshold??
	if (AllowCollapseFixedVertsWithSameSetID
		&& ca.FixedSetID >= 0
		&& ca.FixedSetID == cb.FixedSetID) 
	{
		return true;
	}

	// handle a or b w/ target
	if (ca.Target != nullptr && cb.Target == nullptr) 
	{
		collapse_to = a;
		return true;
	}
	if (cb.Target != nullptr && ca.Target == nullptr) 
	{
		collapse_to = b;
		return true;
	}
	// if both vertices are on the same target, and the edge is on that target,
	// then we can collapse to either and use the midpoint (which will be projected
	// to the target). *However*, if the edge is not on the same target, then we 
	// cannot collapse because we would be changing the constraint topology!
	if (cb.Target != nullptr && ca.Target != nullptr && ca.Target == cb.Target) 
	{
		if (Constraints->GetEdgeConstraint(eid).Target == ca.Target)
		{
			return true;
		}
	}

	return false;
}






void FMeshRefinerBase::RuntimeDebugCheck(int eid)
{
	if (DebugEdges.Contains(eid))
		ensure(false);
}

void FMeshRefinerBase::DoDebugChecks(bool bEndOfPass)
{
	if (DEBUG_CHECK_LEVEL == 0)
		return;

	DebugCheckVertexConstraints();

	if ((DEBUG_CHECK_LEVEL > 2) || (bEndOfPass && DEBUG_CHECK_LEVEL > 1))
	{
		Mesh->CheckValidity(true);
		DebugCheckUVSeamConstraints();
	}
}


void FMeshRefinerBase::DebugCheckUVSeamConstraints()
{
	// verify UV constraints (temporary?)
	if (Mesh->HasAttributes() && Mesh->Attributes()->PrimaryUV() != nullptr && Constraints != nullptr)
	{
		for (int eid : Mesh->EdgeIndicesItr())
		{
			if (Mesh->Attributes()->PrimaryUV()->IsSeamEdge(eid))
			{
				auto cons = Constraints->GetEdgeConstraint(eid);
				check(cons.IsUnconstrained() == false);
			}
		}
		for (int vid : Mesh->VertexIndicesItr())
		{
			if (Mesh->Attributes()->PrimaryUV()->IsSeamVertex(vid))
			{
				auto cons = Constraints->GetVertexConstraint(vid);
				check(cons.Fixed == true);
			}
		}
	}
}


void FMeshRefinerBase::DebugCheckVertexConstraints()
{
	if (Constraints == nullptr)
	{
		return;
	}
	auto AllVtxConstraints = Constraints->GetVertexConstraints();
	for (const TPair<int, FVertexConstraint>& vc : AllVtxConstraints)
	{
		int vid = vc.Key;
		if (vc.Value.Target != nullptr)
		{
			FVector3d curpos = Mesh->GetVertex(vid);
			FVector3d projected = vc.Value.Target->Project(curpos, vid);
			check((curpos - projected).SquaredLength() < 0.0001f);
		}
	}
}