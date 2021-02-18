// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/FFDLattice.h"
#include "DynamicMesh3.h"
#include "Util/ProgressCancel.h"
#include "Async/ParallelFor.h"

FFFDLattice::FFFDLattice(const FVector3i& InDims, const FDynamicMesh3& Mesh, float Padding) :
	Dims(InDims)
{
	check(InDims.X > 1 && InDims.Y > 1 && InDims.Z > 1);

	InitialBounds = Mesh.GetBounds();
	check(!InitialBounds.IsEmpty());

	// Expand the initial bounding box to make the computation of which grid cell a mesh vertex is inside of a little 
	// less susceptible to numerical error issues.

	const float ClampedPadding = FMath::Clamp(Padding, 0.01f, 5.0f);
	const FVector3d Center = InitialBounds.Center();
	FVector3d Extents = InitialBounds.Extents();

	// Compute padding based on maximum component of the diagonal. Avoids problems when one or more component is zero.
	double MaxDiagonal = InitialBounds.Diagonal().MaxElement();
	Extents = Extents + (0.5 * ClampedPadding * MaxDiagonal);

	InitialBounds.Min = Center - Extents;
	InitialBounds.Max = Center + Extents;
	
	FVector3d Diag = InitialBounds.Diagonal();
	CellSize = Diag / FVector3d(Dims - 1);

	ComputeInitialEmbedding(Mesh);
}

void FFFDLattice::GenerateInitialLatticePositions(TArray<FVector3d>& OutLatticePositions) const
{
	int TotalNumLatticePoints = Dims.X * Dims.Y * Dims.Z;
	OutLatticePositions.SetNum(TotalNumLatticePoints);

	for (int i = 0; i < Dims.X; ++i)
	{
		double X = CellSize.X * i;
		for (int j = 0; j < Dims.Y; ++j)
		{
			double Y = CellSize.Y * j;
			for (int k = 0; k < Dims.Z; ++k)
			{
				int PointID = ControlPointIndexFromCoordinates(i, j, k);
				double Z = CellSize.Z * k;

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
	FVector3d GridPoint = (QueryPoint - InitialBounds.Min) / CellSize;
	GridCoordinates = FVector3i(GridPoint);
	FVector3d Weights = GridPoint - FVector3d(GridCoordinates);
	return Weights;
}


void FFFDLattice::ComputeInitialEmbedding(const FDynamicMesh3& Mesh, FLatticeExecutionInfo ExecutionInfo)
{
	int MaxVertexID = Mesh.MaxVertexID();
	VertexEmbeddings.SetNum(MaxVertexID);

	EParallelForFlags ParallelForFlags = ExecutionInfo.bParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;

	ParallelFor(MaxVertexID,
				[this, &Mesh, MaxVertexID](int VertexID)
	{
		if (!Mesh.IsVertex(VertexID))
		{
			return;
		}

		FVector3d VertexPosition = Mesh.GetVertex(VertexID);

		FEmbedding& Embedding = VertexEmbeddings[VertexID];
		Embedding.CellWeighting = ComputeTrilinearWeights(VertexPosition, Embedding.LatticeCell);
	},
				ParallelForFlags);

}


void FFFDLattice::GetDeformedMeshVertexPositions(const TArray<FVector3d>& LatticeControlPoints,
												 TArray<FVector3d>& OutVertexPositions,
												 ELatticeInterpolation Interpolation,
												 FLatticeExecutionInfo ExecutionInfo,
												 FProgressCancel* Progress) const
{
	int MaxVertexID = VertexEmbeddings.Num();
	OutVertexPositions.SetNumZeroed(MaxVertexID);

	EParallelForFlags ParallelForFlags = ExecutionInfo.bParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
	ParallelForFlags |= EParallelForFlags::BackgroundPriority;

	bool bCancelled = false;

	check(VertexEmbeddings.Num() == OutVertexPositions.Num());

	auto InterpolationJob = [this, &OutVertexPositions, &LatticeControlPoints, &bCancelled, ExecutionInfo, MaxVertexID, Interpolation, Progress]
	(int VertexID)
	{
		// Every once in a while, check for cancellation
		if ((VertexID % ExecutionInfo.CancelCheckSize == 0) && Progress && Progress->Cancelled())
		{
			bCancelled = true;
		}

		if (bCancelled || VertexEmbeddings[VertexID].LatticeCell[0] < 0)
		{
			return;
		}

		if (Interpolation == ELatticeInterpolation::Cubic)
		{
			OutVertexPositions[VertexID] = InterpolatedPositionCubic(VertexEmbeddings[VertexID], LatticeControlPoints);
		}
		else
		{
			OutVertexPositions[VertexID] = InterpolatedPosition(VertexEmbeddings[VertexID], LatticeControlPoints);
		}
	};

	ParallelFor(MaxVertexID, InterpolationJob, ParallelForFlags);

}

void FFFDLattice::GetRotatedOverlayNormals(const TArray<FVector3d>& LatticeControlPoints,
										   const FDynamicMeshNormalOverlay* NormalOverlay,
										   TArray<FVector3f>& OutNormals,
										   ELatticeInterpolation Interpolation,
										   FLatticeExecutionInfo ExecutionInfo,
										   FProgressCancel* Progress) const
{
	int ElementCount = NormalOverlay->ElementCount();
	OutNormals.SetNumZeroed(ElementCount);

	EParallelForFlags ParallelForFlags = ExecutionInfo.bParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
	ParallelForFlags |= EParallelForFlags::BackgroundPriority;

	bool bCancelled = false;
	auto InterpolationJob = [this, &OutNormals, &NormalOverlay, &LatticeControlPoints, &bCancelled, ExecutionInfo, Interpolation, Progress]
	(int OverlayElementID)
	{
		// Every once in a while, check for cancellation
		if ((OverlayElementID % ExecutionInfo.CancelCheckSize == 0) && Progress && Progress->Cancelled())
		{
			bCancelled = true;
		}

		int ParentVertexID = NormalOverlay->GetParentVertex(OverlayElementID);

		if (bCancelled || VertexEmbeddings[ParentVertexID].LatticeCell[0] < 0)
		{
			return;
		}

		FMatrix3d Jacobian = (Interpolation == ELatticeInterpolation::Linear) ?
			LinearInterpolationJacobian(VertexEmbeddings[ParentVertexID], LatticeControlPoints) :
			CubicInterpolationJacobian(VertexEmbeddings[ParentVertexID], LatticeControlPoints);

		// Typically we'd do transpose(inv(J)), however if a lattice cell inverts, the determinant is negative and the 
		// resulting normal will be flipped. So we instead multiply by det(J)*transpose(inv(J)) to get the sign right.
		FMatrix3d InvJacobian = Jacobian.DeterminantTimesInverseTranspose();
		OutNormals[OverlayElementID] = FMatrix3f(InvJacobian) * NormalOverlay->GetElement(OverlayElementID);
		OutNormals[OverlayElementID].Normalize();
	};

	ParallelFor(ElementCount, InterpolationJob, ParallelForFlags);
}


void FFFDLattice::GetRotatedMeshVertexNormals(const TArray<FVector3d>& LatticeControlPoints,
											  const TArray<FVector3f>& OriginalNormals,
											  TArray<FVector3f>& OutNormals,
											  ELatticeInterpolation Interpolation,
											  FLatticeExecutionInfo ExecutionInfo,
											  FProgressCancel* Progress) const
{
	int MaxVertexID = OriginalNormals.Num();
	OutNormals.SetNumZeroed(MaxVertexID);

	EParallelForFlags ParallelForFlags = ExecutionInfo.bParallel ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
	ParallelForFlags |= EParallelForFlags::BackgroundPriority;

	bool bCancelled = false;

	check(VertexEmbeddings.Num() == OutNormals.Num());

	auto InterpolationJob = [this, &OutNormals, &OriginalNormals, &LatticeControlPoints, &bCancelled, ExecutionInfo, MaxVertexID, Interpolation, Progress]
	(int VertexID)
	{
		// Every once in a while, check for cancellation
		if ((VertexID % ExecutionInfo.CancelCheckSize == 0) && Progress && Progress->Cancelled())
		{
			bCancelled = true;
		}

		if (bCancelled || VertexEmbeddings[VertexID].LatticeCell[0] < 0)
		{
			return;
		}

		FMatrix3d Jacobian = (Interpolation == ELatticeInterpolation::Linear) ?
			LinearInterpolationJacobian(VertexEmbeddings[VertexID], LatticeControlPoints) :
			CubicInterpolationJacobian(VertexEmbeddings[VertexID], LatticeControlPoints);

		// Typically we'd do transpose(inv(J)), however if a lattice cell inverts, the determinant is negative and the 
		// resulting normal will be flipped. So we instead multiply by det(J)*transpose(inv(J)) to get the sign right.
		FMatrix3d InvJacobian = Jacobian.DeterminantTimesInverseTranspose();
		OutNormals[VertexID] = FMatrix3f( InvJacobian ) * OriginalNormals[VertexID];
		OutNormals[VertexID].Normalize();
	};

	ParallelFor(MaxVertexID, InterpolationJob, ParallelForFlags);
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


static float CubicBSplineKernelDerivative(float A)
{
	// f'(a) =
	//	a/2*(3|a| - 4)		            for 0 <= |a| < 1
	//	-(a*(2-|a|)^2) / (2*|a|) 		for 1 <= |a| < 2
	//	0								otherwise

	float AbsA = FMath::Abs(A);
	if (AbsA < 1.0f)
	{
		return A / 2.0f * (3.0f * AbsA - 4.0f);
	}
	else if (AbsA < 2.0f)
	{
		float TwoMinusAbsA = (2.0f - AbsA);
		return -(A * TwoMinusAbsA * TwoMinusAbsA) / (2.0f * AbsA);
	}

	return 0.0f;
}



FVector3d FFFDLattice::InterpolatedPositionCubic(const FEmbedding& VertexEmbedding, 
												 const TArray<FVector3d>& LatticeControlPoints) const
{
	double T = VertexEmbedding.CellWeighting.X;
	double U = VertexEmbedding.CellWeighting.Y;
	double V = VertexEmbedding.CellWeighting.Z;

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

				int i = VertexEmbedding.LatticeCell.X + DI;
				int j = VertexEmbedding.LatticeCell.Y + DJ;
				int k = VertexEmbedding.LatticeCell.Z + DK;

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

FMatrix3d FFFDLattice::LinearInterpolationJacobian(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const
{
	// TODO: This was written for clarity and correctness. Could definitely be faster.

	// Partial wrt x
	FEmbedding XFloor = VertexEmbedding;
	XFloor.CellWeighting.X = 0.0;
	FEmbedding XCeil = VertexEmbedding;
	XCeil.CellWeighting.X = 1.0;
	FVector3d PartialX = InterpolatedPosition(XCeil, LatticeControlPoints) - InterpolatedPosition(XFloor, LatticeControlPoints);

	// Partial wrt y
	FEmbedding YFloor = VertexEmbedding;
	YFloor.CellWeighting.Y = 0.0;
	FEmbedding YCeil = VertexEmbedding;
	YCeil.CellWeighting.Y = 1.0;
	FVector3d PartialY = InterpolatedPosition(YCeil, LatticeControlPoints) - InterpolatedPosition(YFloor, LatticeControlPoints);

	// Partial wrt z
	FEmbedding ZFloor = VertexEmbedding;
	ZFloor.CellWeighting.Z = 0.0;
	FEmbedding ZCeil = VertexEmbedding;
	ZCeil.CellWeighting.Z = 1.0;
	FVector3d PartialZ = InterpolatedPosition(ZCeil, LatticeControlPoints) - InterpolatedPosition(ZFloor, LatticeControlPoints);

	FMatrix3d Mat(PartialX, PartialY, PartialZ, false);

	return Mat;
}

FMatrix3d FFFDLattice::CubicInterpolationJacobian(const FEmbedding& VertexEmbedding, const TArray<FVector3d>& LatticeControlPoints) const
{
	// TODO: This was written for clarity and correctness. Could definitely be faster.

	double T = VertexEmbedding.CellWeighting.X;
	double U = VertexEmbedding.CellWeighting.Y;
	double V = VertexEmbedding.CellWeighting.Z;

	FMatrix3d Sum = FMatrix3d::Zero();

	for (int DI = -1; DI <= 2; ++DI)
	{
		double WeightX = CubicBSplineKernel(T - DI);
		double DWeightXDX = CubicBSplineKernelDerivative(T - DI);

		for (int DJ = -1; DJ <= 2; ++DJ)
		{
			double WeightY = CubicBSplineKernel(U - DJ);
			double DWeightYDY = CubicBSplineKernelDerivative(U - DJ);

			for (int DK = -1; DK <= 2; ++DK)
			{
				double WeightZ = CubicBSplineKernel(V - DK);
				double DWeightZDZ = CubicBSplineKernelDerivative(V - DK);

				double DWeightDX = DWeightXDX * WeightY * WeightZ;
				double DWeightDY = WeightX * DWeightYDY * WeightZ;
				double DWeightDZ = WeightX * WeightY * DWeightZDZ;

				int i = VertexEmbedding.LatticeCell.X + DI;
				int j = VertexEmbedding.LatticeCell.Y + DJ;
				int k = VertexEmbedding.LatticeCell.Z + DK;

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

				Sum += FMatrix3d(DWeightDX * LatticePoint, DWeightDY * LatticePoint, DWeightDZ * LatticePoint, false);
			}
		}
	}

	return Sum;
}

FVector3d FFFDLattice::ClosestLatticePosition(const FVector3i& VirtualControlPointIndex, const TArray<FVector3d>& LatticeControlPoints) const
{
	// Clamp to valid lattice index
	FVector3i NearestControlPointIndex = Max(Min(VirtualControlPointIndex, Dims - 1), FVector3i(0, 0, 0));

	return LatticeControlPoints[ControlPointIndexFromCoordinates(NearestControlPointIndex)];
}

FVector3d FFFDLattice::ExtrapolatedLatticePosition(const FVector3i& VirtualControlPointIndex, const TArray<FVector3d>& LatticeControlPoints) const
{
	// Use the location of the nearest control point and the location of a control point in the opposite direction of 
	// the extrapolation to get the extrapolated location.

	FVector3i NearestControlPointIndex = Max(Min(VirtualControlPointIndex, Dims - 1), FVector3i(0, 0, 0));
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
