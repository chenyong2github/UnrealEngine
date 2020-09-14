// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/FFDLattice.h"
#include "DynamicMesh3.h"
#include "Util/ProgressCancel.h"

FFFDLattice::FFFDLattice(const FVector3i& InDims, const FDynamicMesh3& Mesh) :
	Dims(InDims)
{
	InitialBounds = Mesh.GetBounds();

	// Expand the initial bounding box to make the computation of which grid cell a mesh vertex is inside of a little 
	// less susceptible to numerical error issues.
	// TODO: derive a better padding value than this magic number, or allow user to set it (UETOOL-2362)
	constexpr float Padding = 10.0f;
	InitialBounds.Min -= FVector3d{ Padding, Padding, Padding };
	InitialBounds.Max += FVector3d{ Padding, Padding, Padding };
	
	ComputeInitialEmbedding(Mesh);
}

void FFFDLattice::GenerateInitialLatticePositions(TArray<FVector3d>& OutLatticePositions) const
{
	double DX = (InitialBounds.Max.X - InitialBounds.Min.X) / (Dims.X - 1);
	double DY = (InitialBounds.Max.Y - InitialBounds.Min.Y) / (Dims.Y - 1);
	double DZ = (InitialBounds.Max.Z - InitialBounds.Min.Z) / (Dims.Z - 1);

	int TotalNumLatticePoints = Dims.X * Dims.Y * Dims.Z;

	OutLatticePositions.SetNum(TotalNumLatticePoints);

	for (int i = 0; i < Dims.X; ++i)
	{
		double X = DX * i;
		for (int j = 0; j < Dims.Y; ++j)
		{
			double Y = DY * j;
			for (int k = 0; k < Dims.Z; ++k)
			{
				int PointID = ControlPointIndexFromCoordinates(i, j, k);
				double Z = DZ * k;

				OutLatticePositions[PointID] = InitialBounds.Min + FVector3d{ X,Y,Z };
			}
		}
	}
}


void FFFDLattice::GenerateLatticeEdges(TArray<FVector2i>& OutLatticeEdges) const
{
	OutLatticeEdges.Reset(3 * Dims.X * Dims.Y * Dims.Z);

	for (int i = 0; i < Dims.X; ++i)
	{
		for (int j = 0; j < Dims.Y; ++j)
		{
			for (int k = 0; k < Dims.Z; ++k)
			{
				int PointID = ControlPointIndexFromCoordinates(i, j, k);

				if (i + 1 < Dims.X)
				{
					int IPlusOne = ControlPointIndexFromCoordinates(i + 1, j, k);
					OutLatticeEdges.Add({ PointID, IPlusOne });
				}
				if (j + 1 < Dims.Y)
				{
					int JPlusOne = ControlPointIndexFromCoordinates(i, j + 1, k);
					OutLatticeEdges.Add({ PointID, JPlusOne });
				}
				if (k + 1 < Dims.Z)
				{
					int KPlusOne = ControlPointIndexFromCoordinates(i, j, k + 1);
					OutLatticeEdges.Add({ PointID, KPlusOne });
				}
			}
		}
	}
}


FVector3d FFFDLattice::ComputeTrilinearWeights(const FVector3d& QueryPoint, FVector3i& GridCoordinates) const
{
	double DX = (InitialBounds.Max.X - InitialBounds.Min.X) / (Dims.X - 1);
	double DY = (InitialBounds.Max.Y - InitialBounds.Min.Y) / (Dims.Y - 1);
	double DZ = (InitialBounds.Max.Z - InitialBounds.Min.Z) / (Dims.Z - 1);

	FVector3d GridPoint{
		((QueryPoint.X - InitialBounds.Min.X) / DX),
		((QueryPoint.Y - InitialBounds.Min.Y) / DY),
		((QueryPoint.Z - InitialBounds.Min.Z) / DZ) };

	// compute integer coordinates
	int X0 = (int)GridPoint.X;
	int Y0 = (int)GridPoint.Y;
	int Z0 = (int)GridPoint.Z;

	GridCoordinates = FVector3i{ X0, Y0, Z0 };

	FVector3d Weights = FVector3d{
		GridPoint.X - (double)X0,
		GridPoint.Y - (double)Y0,
		GridPoint.Z - (double)Z0 };

	return Weights;
}

void FFFDLattice::ComputeInitialEmbedding(const FDynamicMesh3& Mesh)
{
	VertexEmbeddings.Reset(Mesh.VertexCount());
	for (const FVector3d& VertexPosition : Mesh.VerticesItr())
	{
		FEmbedding Embedding;
		Embedding.CellWeighting = ComputeTrilinearWeights(VertexPosition, Embedding.LatticeCell);
		VertexEmbeddings.Add(Embedding);
	}

	check(VertexEmbeddings.Num() == Mesh.VertexCount());
}


void FFFDLattice::GetDeformedMeshVertexPositions(const TArray<FVector3d>& LatticeControlPoints, 
												 TArray<FVector3d>& OutVertexPositions, 
												 bool bCubic, 
												 FProgressCancel* Progress) const
{
	int NumVertices = VertexEmbeddings.Num();
	OutVertexPositions.SetNumZeroed(NumVertices);

	for (int VertexID = 0; VertexID < NumVertices; ++VertexID)
	{
		if (bCubic)
		{
			OutVertexPositions[VertexID] = InterpolatedPositionCubic(VertexEmbeddings[VertexID], LatticeControlPoints);
		}
		else
		{
			OutVertexPositions[VertexID] = InterpolatedPosition(VertexEmbeddings[VertexID], LatticeControlPoints);
		}

		// Every once in a while, check for cancellation
		if (Progress && (VertexID % 1000 == 0) && Progress->Cancelled())
		{
			return;
		}
	}
}


void FFFDLattice::GetValuePair(int I, int J, int K, FVector3d& A, FVector3d& B, 
							   const TArray<FVector3d>& LatticeControlPoints) const
{
	int IndexA = ControlPointIndexFromCoordinates(I, J, K);
	check(IndexA < LatticeControlPoints.Num());
	check(IndexA >= 0);
	A = LatticeControlPoints[IndexA];

	int IndexB = ControlPointIndexFromCoordinates(I + 1, J, K);
	B = LatticeControlPoints[IndexB];
}


static float CubicBSplineKernel(float A)
{
	// Using cubic kernel f(a) =
	//	(4 - 6a^2 + 3|a|^3) / 6		for 0 <= |a| < 1
	//	(2 - |a|)^3 / 6				for 1 <= |a| < 2
	//	0							otherwise
	//
	// So at a = {-2, -1, 0, 1, 2}, f(a) = {0, 1/6, 4/6, 1/6, 0}, and is piecewise cubic in between.

	float AbsA = FMath::Abs(A);
	if (AbsA < 1.0f)
	{
		float ASquared = AbsA * AbsA;
		return (4.0f - 6.0f * ASquared + 3.0f * ASquared * AbsA) / 6.0f;
	}
	else if (AbsA < 2.0f)
	{
		float TwoMinusAbsA = (2.0f - AbsA);
		return TwoMinusAbsA * TwoMinusAbsA * TwoMinusAbsA / 6.0f;
	}

	return 0.0f;
}


FVector3d FFFDLattice::InterpolatedPositionCubic(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const
{
	int X0 = VertexEmbedding.LatticeCell.X;
	int Y0 = VertexEmbedding.LatticeCell.Y;
	int Z0 = VertexEmbedding.LatticeCell.Z;

	double T = VertexEmbedding.CellWeighting.X;
	double U = VertexEmbedding.CellWeighting.Y;
	double V = VertexEmbedding.CellWeighting.Z;

	double DX = (InitialBounds.Max.X - InitialBounds.Min.X) / (Dims.X - 1);
	double DY = (InitialBounds.Max.Y - InitialBounds.Min.Y) / (Dims.Y - 1);
	double DZ = (InitialBounds.Max.Z - InitialBounds.Min.Z) / (Dims.Z - 1);

	FVector3d Sum{ 0.0f, 0.0f, 0.0f };

	// TODO: This can probably be replaced with some relatively simple linear algebra

	for (int DI = -1; DI <= 2; ++DI)
	{
		double WeightX = CubicBSplineKernel(T - DI);

		for (int DJ = -1; DJ <= 2; ++DJ)
		{
			double WeightY = CubicBSplineKernel(U - DJ);

			for (int DK = -1; DK <= 2; ++DK)
			{
				double WeightZ = CubicBSplineKernel(V - DK);
				double Weight = WeightX * WeightY * WeightZ;

				int i = X0 + DI;
				int j = Y0 + DJ;
				int k = Z0 + DK;

				FVector3d LatticePoint;
				if (i < 0 || i >= Dims.X || j < 0 || j >= Dims.Y || k < 0 || k >= Dims.Z)
				{
					// Get the extrapolated position for a "virtual" control point outside of the deformed lattice
					LatticePoint = ExtrapolatedLatticePosition({ i,j,k }, LatticeControlPoints);
				}
				else
				{
					int PointIndex = ControlPointIndexFromCoordinates(i, j, k);
					LatticePoint = LatticeControlPoints[PointIndex];
				}

				Sum += Weight * LatticePoint;
			}
		}
	}

	return Sum;
}

FVector3d FFFDLattice::InterpolatedPosition(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const
{
	// TODO: See if we can refactor TTriLinearGridInterpolant to make that usable in this class

	// Trilinear interpolation:
	//		V### is grid cell corner index
	//		AlphaN is [0,1] fraction of point in cell along N'th dimension  
	// return
	//		V000 * (1 - AlphaX) * (1 - AlphaY) * (1 - AlphaZ) +
	//		V001 * (1 - AlphaX) * (1 - AlphaY) * (AlphaZ) +
	//		V010 * (1 - AlphaX) * (AlphaY) * (1 - AlphaZ) +
	//		V011 * (1 - AlphaX) * (AlphaY) * (AlphaZ) +
	//		V100 * (AlphaX) * (1 - AlphaY) * (1 - AlphaZ) +
	//		V101 * (AlphaX) * (1 - AlphaY) * (AlphaZ) +
	//		V110 * (AlphaX) * (AlphaY) * (1 - AlphaZ) +
	//		V111 * (AlphaX) * (AlphaY) * (AlphaZ);

	int X0 = VertexEmbedding.LatticeCell.X;
	int Y0 = VertexEmbedding.LatticeCell.Y;
	int Y1 = Y0 + 1;
	int Z0 = VertexEmbedding.LatticeCell.Z;
	int Z1 = Z0 + 1;

	double AlphaX = VertexEmbedding.CellWeighting.X;
	double AlphaY = VertexEmbedding.CellWeighting.Y;
	double AlphaZ = VertexEmbedding.CellWeighting.Z;
	double OneMinusAlphaX = 1.0 - AlphaX;

	FVector3d Sum{ 0,0,0 };

	FVector3d FV000, FV100;
	GetValuePair(X0, Y0, Z0, FV000, FV100, LatticeControlPoints);
	double YZ = (1 - AlphaY) * (1 - AlphaZ);
	Sum = (OneMinusAlphaX * FV000 + AlphaX * FV100) * YZ;

	FVector3d FV001, FV101;
	GetValuePair(X0, Y0, Z1, FV001, FV101, LatticeControlPoints);
	YZ = (1 - AlphaY) * (AlphaZ);
	Sum += (OneMinusAlphaX * FV001 + AlphaX * FV101) * YZ;

	FVector3d FV010, FV110;
	GetValuePair(X0, Y1, Z0, FV010, FV110, LatticeControlPoints);
	YZ = (AlphaY) * (1 - AlphaZ);
	Sum += (OneMinusAlphaX * FV010 + AlphaX * FV110) * YZ;

	FVector3d FV011, FV111;
	GetValuePair(X0, Y1, Z1, FV011, FV111, LatticeControlPoints);
	YZ = (AlphaY) * (AlphaZ);
	Sum += (OneMinusAlphaX * FV011 + AlphaX * FV111) * YZ;

	return Sum;
}

FVector3d FFFDLattice::ClosestLatticePosition(const FVector3i& VirtualControlPointIndex, const TArray<FVector3d>& LatticeControlPoints) const
{
	FVector3i NearestControlPointIndex;
	NearestControlPointIndex.X = FMath::Clamp(VirtualControlPointIndex.X, 0, Dims.X - 1);
	NearestControlPointIndex.Y = FMath::Clamp(VirtualControlPointIndex.Y, 0, Dims.Y - 1);
	NearestControlPointIndex.Z = FMath::Clamp(VirtualControlPointIndex.Z, 0, Dims.Z - 1);
	return LatticeControlPoints[ControlPointIndexFromCoordinates(NearestControlPointIndex)];
}

FVector3d FFFDLattice::ExtrapolatedLatticePosition(const FVector3i& VirtualControlPointIndex, const TArray<FVector3d>& LatticeControlPoints) const
{
	// Use the location of the nearest control point and the location of a control point in the opposite direction of 
	// the extrapolation to get the extrapolated location.

	FVector3i NearestControlPointIndex;
	NearestControlPointIndex.X = FMath::Clamp(VirtualControlPointIndex.X, 0, Dims.X - 1);
	NearestControlPointIndex.Y = FMath::Clamp(VirtualControlPointIndex.Y, 0, Dims.Y - 1);
	NearestControlPointIndex.Z = FMath::Clamp(VirtualControlPointIndex.Z, 0, Dims.Z - 1);

	FVector3i Delta = VirtualControlPointIndex - NearestControlPointIndex;
	check(Delta != FVector3i::Zero());

	FVector3i TraceBackControlPointIndex = NearestControlPointIndex - Delta;
	check(TraceBackControlPointIndex.X >= 0 && TraceBackControlPointIndex.X < Dims.X);
	check(TraceBackControlPointIndex.Y >= 0 && TraceBackControlPointIndex.Y < Dims.Y);
	check(TraceBackControlPointIndex.Z >= 0 && TraceBackControlPointIndex.Z < Dims.Z);

	const FVector3d& A = LatticeControlPoints[ControlPointIndexFromCoordinates(TraceBackControlPointIndex)];
	const FVector3d& B = LatticeControlPoints[ControlPointIndexFromCoordinates(NearestControlPointIndex)];

	FVector3d Position = B + (B - A);
	return Position;
}
