// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshShapeGenerator.h"
#include "OrientedBoxTypes.h"
#include "Util/IndexUtil.h"

/**
 * Generate a sphere mesh, with UVs wrapped cylindrically
 */
class /*GEOMETRICOBJECTS_API*/ FSphereGenerator : public FMeshShapeGenerator
{
public:
	/** Radius */
	double Radius = 1;

	int NumPhi = 16; // number of vertices along vertical extent from north pole to south pole
	int NumTheta = 16; // number of vertices around circles

public:

	inline static FVector3d SphericalToCartesian(double r, double theta, double phi)
	{
		double Sphi = sin(phi);
		double Cphi = cos(phi);
		double Ctheta = cos(theta);
		double Stheta = sin(theta);

		return FVector3d(r * Ctheta * Sphi, r * Stheta * Sphi, r * Cphi);
	}

	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		// enforce sane values for vertex counts
		if (NumPhi < 3)
		{
			NumPhi = 3;
		}
		if (NumTheta < 3)
		{
			NumTheta = 3;
		}
		int32 NumVertices = (NumPhi - 2) * NumTheta + 2;
		int32 NumUVs = NumPhi * (NumTheta + 1);
		int32 NumTris = (NumPhi - 2) * NumTheta * 2;
		SetBufferSizes(NumVertices, NumTris, NumUVs, NumVertices);

		double Dphi = FMathd::Pi / double(NumPhi - 1);
		double Dtheta = FMathd::TwoPi / double(NumTheta);
		float DUVphi = 1.0 / float(NumPhi - 1);
		float DUVtheta = 1.0 / float(NumTheta);

		TArray<int32> Grid2d; Grid2d.SetNumUninitialized(NumTheta * NumPhi);
		for (int32 i = 0; i < NumTheta * NumPhi; ++i)
		{
			Grid2d[i] = -1;
		}


		auto GridInterface = [&Grid2d, this](int32 itheta, int32 jphi)->int32&
		{
			// make periodic in j
			while (itheta >= NumTheta) { itheta = itheta - NumTheta; }

			int32 offset = itheta + jphi * NumTheta;

			return Grid2d[offset];
		};

		int32 VtxIdx = 0;
		for (int32 p = 1; p < NumPhi - 1; ++p) // NB: this skips the poles.
		{
			double Phi = p * Dphi;

			for (int32 t = 0; t < NumTheta; ++t)
			{
				double Theta = t * Dtheta;
				FVector3d Pos = SphericalToCartesian(1., Theta, Phi);

				Vertices[VtxIdx] = Pos * Radius;
				Normals[VtxIdx] = FVector3f(Pos);
				NormalParentVertex[VtxIdx] = VtxIdx;
				GridInterface(t, p) = VtxIdx;
				VtxIdx++;
			}
		}


		// add a single point at the North Pole
		{
			FVector3d Pos = SphericalToCartesian(1., 0., 0.);
			Vertices[VtxIdx] = Pos * Radius;
			Normals[VtxIdx] = FVector3f(Pos);
			NormalParentVertex[VtxIdx] = VtxIdx;

			for (int32 t = 0; t < NumTheta; ++t)
			{
				GridInterface(t, 0) = VtxIdx;
			}

			VtxIdx++;
		}
		// add a single point at the south pole
		{
			FVector3d Pos = SphericalToCartesian(-1., 0., 0.);
			Vertices[VtxIdx] = Pos * Radius;
			Normals[VtxIdx] = FVector3f(Pos);
			NormalParentVertex[VtxIdx] = VtxIdx;

			for (int32 t = 0; t < NumTheta; ++t)
			{
				GridInterface(t, NumPhi - 1) = VtxIdx;
			}

			VtxIdx++;
		}

		int32 UVIdx = 0;
		auto UVIdxLookup = [this](int32 t, int32 p)
		{
			check(t >= 0 && t <= NumTheta);
			return p * (NumTheta+1) + t;
		};
		for (int32 p = 0; p < NumPhi; ++p) // NB: this skips the poles.
		{
			float Phi = p * DUVphi;

			for (int32 t = 0; t <= NumTheta; ++t)
			{
				float Theta = 1 - t * DUVtheta;
				UVs[UVIdx] = FVector2f(Theta, Phi);
				UVParentVertex[UVIdx] = GridInterface(t, p);
				ensure(UVIdxLookup(t, p) == UVIdx);

				UVIdx++;
			}
		}

		int32 TriIdx = 0, PolyIdx = 0;
		for (int32 p = 1; p < NumPhi - 2; ++p)
		{
			for (int32 t = 0; t < NumTheta; ++t)
			{
				// (p,t), (p, t+1), (p+1, t), (p+1, t+1) are the 4 corners of a quad.
				// counter clockwise.
				int32 Corners[4] = { GridInterface(t, p + 1), GridInterface(t + 1, p + 1), GridInterface(t + 1, p), GridInterface(t, p) };
				int32 UVCorners[4] = { UVIdxLookup(t, p + 1),   UVIdxLookup(t + 1, p + 1),   UVIdxLookup(t + 1, p),   UVIdxLookup(t, p) };

				// convert each quad into 2 triangles.

				SetTriangle(TriIdx, Corners[0], Corners[2], Corners[1]);
				SetTrianglePolygon(TriIdx, PolyIdx);
				SetTriangleUVs(TriIdx, UVCorners[0], UVCorners[2], UVCorners[1]);
				SetTriangleNormals(TriIdx, Corners[0], Corners[2], Corners[1]);
				TriIdx++;

				SetTriangle(TriIdx, Corners[2], Corners[0], Corners[3]);
				SetTrianglePolygon(TriIdx, PolyIdx);
				SetTriangleUVs(TriIdx, UVCorners[2], UVCorners[0], UVCorners[3]);
				SetTriangleNormals(TriIdx, Corners[2], Corners[0], Corners[3]);
				TriIdx++;
				PolyIdx++;
			}
		}

		// Do Triangles that connect to north pole
		{
			int32 p = 0; // North pole 
			for (int32 t = 0; t < NumTheta; ++t)
			{
				int32 Corners[3] = { GridInterface(t, p + 1), GridInterface(t + 1, p + 1), GridInterface(t + 1, p) };
				int32 UVCorners[3] = { UVIdxLookup(t, p + 1),   UVIdxLookup(t + 1, p + 1),   UVIdxLookup(t + 1, p) };

				SetTriangle(TriIdx, Corners[0], Corners[2], Corners[1]);
				SetTrianglePolygon(TriIdx, PolyIdx);
				SetTriangleUVs(TriIdx, UVCorners[0], UVCorners[2], UVCorners[1]);
				SetTriangleNormals(TriIdx, Corners[0], Corners[2], Corners[1]);
				TriIdx++;
				PolyIdx++;
			}
		}

		// Do Triangles that connect to south pole
		{
			int32 p = NumPhi - 2; // South pole 
			for (int32 t = 0; t < NumTheta; ++t)
			{
				int32 Corners[4] = { GridInterface(t, p + 1), GridInterface(t + 1, p), GridInterface(t, p) };
				int32 UVCorners[3] = { UVIdxLookup(t, p + 1),   UVIdxLookup(t + 1, p),   UVIdxLookup(t, p) };

				SetTriangle(TriIdx, Corners[0], Corners[2], Corners[1]);
				SetTrianglePolygon(TriIdx, PolyIdx);
				SetTriangleUVs(TriIdx, UVCorners[0], UVCorners[2], UVCorners[1]);
				SetTriangleNormals(TriIdx, Corners[0], Corners[2], Corners[1]);
				TriIdx++;
				PolyIdx++;
			}
		}

		return *this;
	}


};