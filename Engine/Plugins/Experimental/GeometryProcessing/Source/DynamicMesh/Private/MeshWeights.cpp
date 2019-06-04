// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshWeights.h"
#include "VectorUtil.h"


FVector3d FMeshWeights::UniformCentroid(const FDynamicMesh3 & mesh, int VertexIndex)
{
	FVector3d Centroid;
	mesh.GetVtxOneRingCentroid(VertexIndex, Centroid);
	return Centroid;
}


FVector3d FMeshWeights::MeanValueCentroid(const FDynamicMesh3 & mesh, int v_i)
{
	// based on equations in https://www.inf.usi.ch/hormann/papers/Floater.2006.AGC.pdf (formula 9)
	// refer to that paper for variable names/etc

	FVector3d vSum = FVector3d::Zero();
	double wSum = 0;
	FVector3d Vi = mesh.GetVertex(v_i);

	int v_j = FDynamicMesh3::InvalidID, opp_v1 = FDynamicMesh3::InvalidID, opp_v2 = FDynamicMesh3::InvalidID;
	int t1 = FDynamicMesh3::InvalidID, t2 = FDynamicMesh3::InvalidID;
	for (int eid : mesh.VtxEdgesItr(v_i) ) 
	{
		opp_v2 = FDynamicMesh3::InvalidID;
		mesh.GetVtxNbrhood(eid, v_i, v_j, opp_v1, opp_v2, t1, t2);

		FVector3d Vj = mesh.GetVertex(v_j);
		FVector3d vVj = (Vj - Vi);
		double len_vVj = vVj.Normalize();
		// [RMS] is this the right thing to do? if vertices are coincident,
		//   weight of this vertex should be very high!
		if (len_vVj < FMathd::ZeroTolerance)
			continue;
		FVector3d vVdelta = (mesh.GetVertex(opp_v1) - Vi).Normalized();
		double w_ij = VectorUtil::VectorTanHalfAngle(vVj, vVdelta);

		if (opp_v2 != FDynamicMesh3::InvalidID) {
			FVector3d vVgamma = (mesh.GetVertex(opp_v2) - Vi).Normalized();
			w_ij += VectorUtil::VectorTanHalfAngle(vVj, vVgamma);
		}

		w_ij /= len_vVj;

		vSum += w_ij * Vj;
		wSum += w_ij;
	}
	if (wSum < FMathd::ZeroTolerance)
		return Vi;
	return vSum / wSum;
}




FVector3d FMeshWeights::CotanCentroid(const FDynamicMesh3& mesh, int v_i)
{
	// based on equations in http://www.geometry.caltech.edu/pubs/DMSB_III.pdf

	FVector3d vSum = FVector3d::Zero();
	double wSum = 0;
	FVector3d Vi = mesh.GetVertex(v_i);

	int v_j = FDynamicMesh3::InvalidID, opp_v1 = FDynamicMesh3::InvalidID, opp_v2 = FDynamicMesh3::InvalidID;
	int t1 = FDynamicMesh3::InvalidID, t2 = FDynamicMesh3::InvalidID;
	bool bAborted = false;
	for (int eid : mesh.VtxEdgesItr(v_i)) 
	{
		opp_v2 = FDynamicMesh3::InvalidID;
		mesh.GetVtxNbrhood(eid, v_i, v_j, opp_v1, opp_v2, t1, t2);
		FVector3d Vj = mesh.GetVertex(v_j);

		FVector3d Vo1 = mesh.GetVertex(opp_v1);
		double cot_alpha_ij = VectorUtil::VectorCot(
			(Vi - Vo1).Normalized(), (Vj - Vo1).Normalized());
		if (cot_alpha_ij == 0) {
			bAborted = true;
			break;
		}
		double w_ij = cot_alpha_ij;

		if (opp_v2 != FDynamicMesh3::InvalidID) {
			FVector3d Vo2 = mesh.GetVertex(opp_v2);
			double cot_beta_ij = VectorUtil::VectorCot(
				(Vi - Vo2).Normalized(), (Vj - Vo2).Normalized());
			if (cot_beta_ij == 0) {
				bAborted = true;
				break;
			}
			w_ij += cot_beta_ij;
		}

		vSum += w_ij * Vj;
		wSum += w_ij;
	}
	if (bAborted || fabs(wSum) < FMathd::ZeroTolerance)
		return Vi;
	return vSum / wSum;
}



double FMeshWeights::VoronoiArea(const FDynamicMesh3& mesh, int v_i)
{
	// based on equations in http://www.geometry.caltech.edu/pubs/DMSB_III.pdf

	double areaSum = 0;
	FVector3d Vi = mesh.GetVertex(v_i);

	for (int tid : mesh.VtxTrianglesItr(v_i) ) 
	{
		FIndex3i t = mesh.GetTriangle(tid);
		int ti = (t[0] == v_i) ? 0 : ((t[1] == v_i) ? 1 : 2);
		FVector3d Vj = mesh.GetVertex(t[(ti + 1) % 3]);
		FVector3d Vk = mesh.GetVertex(t[(ti + 2) % 3]);

		if (VectorUtil::IsObtuse(Vi, Vj, Vk)) 
		{
			// if triangle is obtuse voronoi area is undefind and we just return portion of triangle area
			FVector3d Vij = Vj - Vi;
			FVector3d Vik = Vk - Vi;
			Vij.Normalize(); Vik.Normalize();
			double areaT = 0.5 * Vij.Cross(Vik).Length();
			areaSum += ( Vij.AngleR(Vik) > FMathd::HalfPi ) ?    // obtuse at v_i ?
				(areaT * 0.5) : (areaT * 0.25);

		}
		else 
		{
			// voronoi area
			FVector3d Vji = Vi - Vj;
			double dist_ji = Vji.Normalize();
			FVector3d Vki = Vi - Vk;
			double dist_ki = Vki.Normalize();
			FVector3d Vkj = (Vj - Vk).Normalized();

			double cot_alpha_ij = VectorUtil::VectorCot(Vki, Vkj);
			double cot_alpha_ik = VectorUtil::VectorCot(Vji, -Vkj);
			areaSum += dist_ji * dist_ji * cot_alpha_ij * 0.125;
			areaSum += dist_ki * dist_ki * cot_alpha_ik * 0.125;
		}
	}
	return areaSum;
}