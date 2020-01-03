// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Distance/DistPoint3Triangle3.h"
#include "Intersection/IntrRay3Triangle3.h"
#include "BoxTypes.h"
#include "IndexTypes.h"

template <class TriangleMeshType>
class TMeshQueries
{
public:
	TMeshQueries() = delete;

	/**
	 * construct a DistPoint3Triangle3 object for a Mesh triangle
	 */
	static FDistPoint3Triangle3d TriangleDistance(const TriangleMeshType& Mesh, int TriIdx, FVector3d Point)
	{
		check(Mesh.IsTriangle(TriIdx));
		FTriangle3d tri;
		Mesh.GetTriVertices(TriIdx, tri.V[0], tri.V[1], tri.V[2]);
		FDistPoint3Triangle3d q(Point, tri);
		q.GetSquared();
		return q;
	}

	/**
	 * convenience function to construct a IntrRay3Triangle3 object for a Mesh triangle
	 */
	static FIntrRay3Triangle3d TriangleIntersection(const TriangleMeshType& Mesh, int TriIdx, const FRay3d& Ray)
	{
		check(Mesh.IsTriangle(TriIdx));
		FTriangle3d tri;
		Mesh.GetTriVertices(TriIdx, tri.V[0], tri.V[1], tri.V[2]);
		FIntrRay3Triangle3d q(Ray, tri);
		q.Find();
		return q;
	}

	/**
	 * Compute triangle centroid
	 * @param Mesh Mesh with triangle
	 * @param TriIdx Index of triangle
	 * @return Computed centroid
	 */
	static FVector3d GetTriCentroid(const TriangleMeshType& Mesh, int TriIdx)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		return Triangle.Centroid();
	}

	/**
	 * Compute the normal, area, and centroid of a triangle all together
	 * @param Mesh Mesh w/ triangle
	 * @param TriIdx Index of triangle
	 * @param Normal Computed normal (returned by reference)
	 * @param Area Computed area (returned by reference)
	 * @param Centroid Computed centroid (returned by reference)
	 */
	static void GetTriNormalAreaCentroid(const TriangleMeshType& Mesh, int TriIdx, FVector3d& Normal, double& Area, FVector3d& Centroid)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		Centroid = Triangle.Centroid();
		Normal = VectorUtil::NormalArea(Triangle.V[0], Triangle.V[1], Triangle.V[2], Area);
	}

	static FVector2d GetVolumeArea(const TriangleMeshType& Mesh)
	{
		double Volume = 0.0;
		double Area = 0;
		for (int TriIdx = 0; TriIdx < Mesh.MaxTriangleID(); TriIdx++)
		{
			if (!Mesh.IsTriangle(TriIdx))
			{
				continue;
			}

			FVector3d V0, V1, V2;
			Mesh.GetTriVertices(TriIdx, V0, V1, V2);

			// Get cross product of edges and (un-normalized) normal vector.
			FVector3d V1mV0 = V1 - V0;
			FVector3d V2mV0 = V2 - V0;
			FVector3d N = V2mV0.Cross(V1mV0);

			Area += N.Length();

			double tmp0 = V0.X + V1.X;
			double f1x = tmp0 + V2.X;
			Volume += N.X * f1x;
		}

		return FVector2d(Volume * (1.0/6.0), Area * .5f);
	}

	static FAxisAlignedBox3d GetTriBounds(const TriangleMeshType& Mesh, int TID)
	{
		FIndex3i TriInds = Mesh.GetTriangle(TID);
		FVector3d MinV, MaxV, V = Mesh.GetVertex(TriInds.A);
		MinV = MaxV = V;
		for (int i = 1; i < 3; ++i)
		{
			V = Mesh.GetVertex(TriInds[i]);
			if (V.X < MinV.X)				MinV.X = V.X;
			else if (V.X > MaxV.X)			MaxV.X = V.X;
			if (V.Y < MinV.Y)				MinV.Y = V.Y;
			else if (V.Y > MaxV.Y)			MaxV.Y = V.Y;
			if (V.Z < MinV.Z)				MinV.Z = V.Z;
			else if (V.Z > MaxV.Z)			MaxV.Z = V.Z;
		}
		return FAxisAlignedBox3d(MinV, MaxV);
	}

	// brute force search for nearest triangle to Point
	static int FindNearestTriangle_LinearSearch(const TriangleMeshType& Mesh, const FVector3d& P)
	{
		int tNearest = IndexConstants::InvalidID;
		double fNearestSqr = TNumericLimits<double>::Max();
		for (int TriIdx : Mesh.TriangleIndicesItr())
		{
			double distSqr = TriDistanceSqr(Mesh, TriIdx, P);
			if (distSqr < fNearestSqr)
			{
				fNearestSqr = distSqr;
				tNearest = TriIdx;
			}
		}
		return tNearest;
	}

	/**
	 * Compute distance from Point to triangle in Mesh, with minimal extra objects/etc
	 */
	static double TriDistanceSqr(const TriangleMeshType& Mesh, int TriIdx, const FVector3d& Point)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		FDistPoint3Triangle3d Distance(Point, Triangle);
		return Distance.GetSquared();
	}

	// brute force search for nearest triangle intersection
	static int FindHitTriangle_LinearSearch(const TriangleMeshType& Mesh, const FRay3d& Ray)
	{
		int tNearestID = IndexConstants::InvalidID;
		double fNearestT = TNumericLimits<double>::Max();
		FTriangle3d Triangle;

		for (int TriIdx = 0; TriIdx < Mesh.MaxTriangleID(); TriIdx++)
		{
			if (!Mesh.IsTriangle(TriIdx))
			{
				continue;
			}
			Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(Ray, Triangle);
			if (Query.Find())
			{
				if (Query.RayParameter < fNearestT)
				{
					fNearestT = Query.RayParameter;
					tNearestID = TriIdx;
				}
			}
		}

		return tNearestID;
	}

	// brute force search for all triangle intersections, sorted
	static void FindHitTriangles_LinearSearch(const TriangleMeshType& Mesh, const FRay3d& Ray, TArray<TPair<float, int>>& SortedHitTriangles)
	{
		FTriangle3d Triangle;
		SortedHitTriangles.Empty();

		for (int TriIdx : Mesh.TriangleIndicesItr())
		{
			Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
			FIntrRay3Triangle3d Query(Ray, Triangle);
			if (Query.Find())
			{
				SortedHitTriangles.Emplace(Query.RayParameter, TriIdx);
			}
		}

		SortedHitTriangles.Sort([](const TPair<float, int>& A, const TPair<float, int>& B)
		{
			return A.Key < B.Key;
		});
	}

	/**
	 * convenience function to construct a IntrRay3Triangle3 object for a Mesh triangle
	 */
	static FIntrRay3Triangle3d RayTriangleIntersection(const TriangleMeshType& Mesh, int TriIdx, const FRay3d& Ray)
	{
		FTriangle3d Triangle;
		Mesh.GetTriVertices(TriIdx, Triangle.V[0], Triangle.V[1], Triangle.V[2]);

		FIntrRay3Triangle3d Query(Ray, Triangle);
		Query.Find();
		return Query;
	}
};
