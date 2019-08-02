// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"
#include "RenderUtils.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Levelset.h"
#include "Chaos/Framework/Parallel.h"

inline void CopyVectorToPosition(const FVector& InVector, FHairStrandsPositionFormat::Type& OutPosition)
{
	OutPosition.X = InVector.X;
	OutPosition.Y = InVector.Y;
	OutPosition.Z = InVector.Z;
}

inline void CopyPositionToVector(const FHairStrandsPositionFormat::Type& InPosition, FVector& OutVector )
{
	OutVector.X = InPosition.X;
	OutVector.Y = InPosition.Y;
	OutVector.Z = InPosition.Z;
}

void FHairStrandsDatas::BuildRenderingDatas(
	TArray<FHairStrandsPositionFormat::Type>& OutPackedPositions,
	TArray<FHairStrandsTangentFormat::Type>& OutPackedTangents) const
{
	if (GetNumCurves() > 0 && GetNumPoints() > 0)
	{
		OutPackedPositions.SetNum(GetNumPoints());
		OutPackedTangents.SetNum(GetNumPoints()*2);

		TArray<uint16>::TConstIterator CountIterator = StrandsCurves.CurvesCount.CreateConstIterator();
		TArray<float>::TConstIterator LengthIterator = StrandsCurves.CurvesLength.CreateConstIterator();
		TArray<float>::TConstIterator CoordUIterator = StrandsPoints.PointsCoordU.CreateConstIterator();
		TArray<float>::TConstIterator RadiusIterator = StrandsPoints.PointsRadius.CreateConstIterator();
		TArray<FVector>::TConstIterator PositionIterator = StrandsPoints.PointsPosition.CreateConstIterator();

		TArray<FHairStrandsPositionFormat::Type>::TIterator PackedPositionIterator = OutPackedPositions.CreateIterator();
		TArray<FHairStrandsTangentFormat::Type>::TIterator PackedTangentIterator = OutPackedTangents.CreateIterator();

		FRandomStream Random;
		for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex, ++CountIterator, ++LengthIterator)
		{
			const uint16& PointCount = *CountIterator;
			for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex, 
				++CoordUIterator, ++PositionIterator, ++RadiusIterator, ++PackedPositionIterator, ++PackedTangentIterator)
			{
				const uint32 PrevIndex = FMath::Max(0, PointIndex - 1);
				const uint32 NextIndex = FMath::Min(PointCount + 1, PointCount - 1);
				const FVector& PointPosition = *PositionIterator;

				const FVector ForwardDir0 = PointIndex > 0 ? (PointPosition - StrandsPoints.PointsPosition[PrevIndex]).GetSafeNormal() : FVector::ZeroVector;
				const FVector ForwardDir1 = PointIndex < PointCount - 1 ? (StrandsPoints.PointsPosition[NextIndex] - PointPosition).GetSafeNormal() : FVector::ZeroVector;
				const FVector TangentZ = (ForwardDir0 + ForwardDir1).GetSafeNormal();

				// Find quat from up (Z) vector to forward
				// Apply quat orthogonal vectors
				const FQuat DeltaQuat = FQuat::FindBetween(FVector(0, 0, -1), TangentZ);
				const FVector RightDir = DeltaQuat.RotateVector(FVector(0, 1, 0));
				const FVector UpDir = DeltaQuat.RotateVector(FVector(1, 0, 0));

				const FVector TangentX = RightDir ^ TangentZ;
				const FVector TangentY = RightDir;

				FHairStrandsPositionFormat::Type& PackedPosition = *PackedPositionIterator;
				CopyVectorToPosition(PointPosition, PackedPosition);
				PackedPosition.ControlPointType = (PointIndex == 0) ? 1u : (PointIndex == (PointCount - 1) ? 2u : 0u);
				PackedPosition.UCoord = FMath::Clamp((*CoordUIterator)*255.f, 0.f, 255.f);
				PackedPosition.NormalizedRadius = FMath::Clamp((*RadiusIterator) * 63.f, 0.f, 63.f);
				PackedPosition.NormalizedLength = FMath::Clamp((*LengthIterator) *255.f, 0.f, 255.f);
				PackedPosition.Seed = Random.RandHelper(255);

				FHairStrandsTangentFormat::Type& PackedTangentX = *PackedTangentIterator;
				PackedTangentX = TangentX;

				++PackedTangentIterator;

				FHairStrandsTangentFormat::Type& PackedTangentZ = *PackedTangentIterator;
				PackedTangentZ = TangentZ;
				PackedTangentZ.Vector.W = GetBasisDeterminantSignByte(TangentX, TangentY, TangentZ);
			}
		}
	}
}


void FHairStrandsDatas::BuildSimulationDatas(const uint32 StrandSize,
	TArray<FHairStrandsPositionFormat::Type>& OutNodesPositions,
	TArray<FHairStrandsWeightFormat::Type>& OutPointsWeights,
	TArray<FHairStrandsIndexFormat::Type>& OutPointsNodes) const
{
	OutNodesPositions.SetNum(GetNumCurves()*StrandSize);
	OutPointsWeights.SetNum(GetNumPoints());
	OutPointsNodes.SetNum(GetNumPoints());

	if (GetNumCurves() > 0 && GetNumPoints() > 0 && StrandSize > 1)
	{
		TArray<FVector>::TConstIterator PositionIterator = StrandsPoints.PointsPosition.CreateConstIterator();
		TArray<uint16>::TConstIterator CountIterator = StrandsCurves.CurvesCount.CreateConstIterator();
		TArray<float>::TConstIterator LengthIterator = StrandsCurves.CurvesLength.CreateConstIterator();

		TArray<FHairStrandsIndexFormat::Type>::TIterator NodesIterator = OutPointsNodes.CreateIterator();

		uint32 NumPoints = 0;
		for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex, ++LengthIterator, ++CountIterator)
		{
			const float SeparationLength = (*LengthIterator) * StrandsCurves.MaxLength / (StrandSize - 1);
			uint32 SampleOffset = CurveIndex * StrandSize;
			const uint16 EdgeCount = *CountIterator - 1;

			CopyVectorToPosition(*PositionIterator, OutNodesPositions[SampleOffset]);
			CopyVectorToPosition(*(PositionIterator + EdgeCount), OutNodesPositions[SampleOffset + StrandSize - 1]);

			FVector VertexNext = *PositionIterator;
			FVector VertexPrev = VertexNext;

			++PositionIterator;

			float StrandLength = 0.0;
			uint32 LocalCount = 2;
			for (uint16 EdgeIndex = 0; EdgeIndex < EdgeCount; ++EdgeIndex, ++PositionIterator, ++NodesIterator, ++NumPoints)
			{
				VertexPrev = VertexNext;
				VertexNext = *PositionIterator;

				*NodesIterator = SampleOffset;

				FVector EdgeDirection = VertexNext - VertexPrev;
				const float CurrentLength = StrandLength;
				const float EdgeLength = EdgeDirection.Size();
				StrandLength += EdgeLength;

				if (StrandLength > SeparationLength)
				{
					EdgeDirection /= EdgeLength;
					const uint32 NumNodes = StrandLength / SeparationLength;
					FVector EdgePosition = VertexPrev + EdgeDirection * (SeparationLength - CurrentLength);
					for (uint32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex, EdgePosition += EdgeDirection * SeparationLength)
					{
						if (LocalCount < StrandSize)
						{
							++SampleOffset;
							CopyVectorToPosition(EdgePosition, OutNodesPositions[SampleOffset]);
							++LocalCount;
							StrandLength -= SeparationLength;
						}
					}
				}
			}
			*NodesIterator = SampleOffset;
			++NodesIterator;
		}
		PositionIterator.Reset();
		NodesIterator.Reset();
		LengthIterator.Reset();
		CountIterator.Reset();

		TArray<float>::TIterator WeightsIterator = OutPointsWeights.CreateIterator();

		for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex, ++LengthIterator, ++CountIterator)
		{
			const uint16& StrandCount = *CountIterator;
			const float SeparationLength = (*LengthIterator) * StrandsCurves.MaxLength / (StrandSize - 1);
			const float LengthScale = 1.0 / (SeparationLength * SeparationLength);

			for (uint32 PointIndex = 0; PointIndex < StrandCount; ++PointIndex, ++PositionIterator, ++NodesIterator, ++WeightsIterator)
			{
				const uint32 NodeIndex = *NodesIterator;
				FVector PrevPosition, NextPosition;
				CopyPositionToVector(OutNodesPositions[NodeIndex], PrevPosition);
				CopyPositionToVector(OutNodesPositions[NodeIndex+1], NextPosition);
				const float PrevDist = (*PositionIterator - PrevPosition).SizeSquared() * LengthScale;
				const float NextDist = (*PositionIterator - NextPosition).SizeSquared() * LengthScale;

				float PrevWeight = (PrevDist < 1.0) ? (1.0 - PrevDist)* (1.0 - PrevDist)* (1.0 - PrevDist) : 0.0;
				float NextWeight = (NextDist < 1.0) ? (1.0 - NextDist)* (1.0 - NextDist)* (1.0 - NextDist) : 0.0;

				const float SumWeights = PrevWeight + NextWeight;
				if (SumWeights != 0.0)
				{
					PrevWeight /= SumWeights;
					NextWeight /= SumWeights;
				}
				*WeightsIterator = PrevWeight;
			}
		}
	}
}

void FHairStrandsDatas::BuildInternalDatas()
{
	if (GetNumCurves() > 0 && GetNumPoints() > 0)
	{
		TArray<FVector>::TIterator PositionIterator = StrandsPoints.PointsPosition.CreateIterator();
		TArray<float>::TIterator RadiusIterator = StrandsPoints.PointsRadius.CreateIterator();
		TArray<float>::TIterator CoordUIterator = StrandsPoints.PointsCoordU.CreateIterator();

		TArray<uint16>::TIterator CountIterator = StrandsCurves.CurvesCount.CreateIterator();
		TArray<uint32>::TIterator OffsetIterator = StrandsCurves.CurvesOffset.CreateIterator();
		TArray<float>::TIterator LengthIterator = StrandsCurves.CurvesLength.CreateIterator();

		StrandsPoints.MaxRadius = 0.0;
		StrandsCurves.MaxLength = 0.0;

		uint32 StrandOffset = 0;
		*OffsetIterator = StrandOffset; ++OffsetIterator;

		for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex, ++OffsetIterator, ++LengthIterator, ++CountIterator)
		{
			const uint16& StrandCount = *CountIterator;

			StrandOffset += StrandCount;
			*OffsetIterator = StrandOffset;

			float StrandLength = 0.0;
			FVector PreviousPosition(0.0, 0.0, 0.0);
			for (uint32 PointIndex = 0; PointIndex < StrandCount; ++PointIndex, ++PositionIterator, ++RadiusIterator, ++CoordUIterator)
			{
				if (PointIndex > 0)
				{
					StrandLength += (*PositionIterator - PreviousPosition).Size();
				}
				*CoordUIterator = StrandLength;
				PreviousPosition = *PositionIterator;

				StrandsPoints.MaxRadius = FMath::Max(StrandsPoints.MaxRadius, *RadiusIterator);
			}
			*LengthIterator = StrandLength;
			StrandsCurves.MaxLength = FMath::Max(StrandsCurves.MaxLength, StrandLength);
		}

		CountIterator.Reset();
		LengthIterator.Reset();
		RadiusIterator.Reset();
		CoordUIterator.Reset();

		for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex, ++LengthIterator, ++CountIterator)
		{
			const uint16& StrandCount = *CountIterator;

			for (uint32 PointIndex = 0; PointIndex < StrandCount; ++PointIndex, ++RadiusIterator, ++CoordUIterator)
			{
				*CoordUIterator /= *LengthIterator;
				*RadiusIterator /= StrandsPoints.MaxRadius;
			}
			*LengthIterator /= StrandsCurves.MaxLength;
		}
	}
}


void FHairStrandsDatas::AttachStrandsRoots(UStaticMesh* StaticMesh, const FMatrix& TransformMatrix)
{
	/*if (StaticMesh)
	{
		FTriMeshCollisionData CollisionData;
		StaticMesh->GetPhysicsTriMeshData(&CollisionData, true);

		Chaos::TParticles<float, 3> Particles =  MoveTemp(*reinterpret_cast<TArray<Chaos::TVector<float, 3>>*>(&CollisionData.Vertices));
		TArray<Chaos::TVector<int32, 3>> Triangles = MoveTemp(*reinterpret_cast<TArray<Chaos::TVector<int32, 3>>*>(&CollisionData.Indices)); 

		TUniquePtr<Chaos::TTriangleMeshImplicitObject<float>> TriangleMesh = MakeUnique<Chaos::TTriangleMeshImplicitObject<float>>(MoveTemp(Particles), MoveTemp(Triangles));

		TArray<int32> TriangleIndices;
		TriangleIndices.Init(-1, GetNumCurves());

		TArray<Chaos::TVector<float, 2>> BarycentricCoordinates;
		BarycentricCoordinates.Init(Chaos::TVector<float, 2>(0, 0), GetNumCurves());

		Chaos::PhysicsParallelFor(GetNumCurves(), [&](int32 CurveIndex) {

			const FVector& RootPosition = StrandsPoints.PointsPosition[StrandsCurves.CurvesOffset[CurveIndex]];
			const float Dist = TriangleMesh->FindClosestPoint(RootPosition, TriangleIndices[CurveIndex], BarycentricCoordinates[CurveIndex]);
		});

		// build cell domain
		int32 MaxAxisSize = 50;
		int MaxAxis = TriangleMesh->MLocalBoundingBox.LargestAxis();
		Chaos::TVector<float, 3> Extents = TriangleMesh->MLocalBoundingBox.Extents();
		Chaos::TVector<int32, 3> Counts(MaxAxisSize * Extents[0] / Extents[MaxAxis], MaxAxisSize * Extents[1] / Extents[MaxAxis], MaxAxisSize * Extents[2] / Extents[MaxAxis]);
		Counts[0] = Counts[0] < 1 ? 1 : Counts[0];
		Counts[1] = Counts[1] < 1 ? 1 : Counts[1]; 
		Counts[2] = Counts[2] < 1 ? 1 : Counts[2];

		Chaos::TUniformGrid<float, 3> Grid(TriangleMesh->MLocalBoundingBox.Min(), TriangleMesh->MLocalBoundingBox.Max(), Counts, 1);
		Chaos::FErrorReporter ErrorReporter;
		Chaos::TTriangleMesh<float> CollisionMesh(MoveTemp(TriangleMesh->MElements));
		Chaos::TLevelSet<float, 3> LevelSet(ErrorReporter, Grid, TriangleMesh->MParticles, CollisionMesh, 0, false);

		for (uint32 CurveIndex = 0; CurveIndex < 10; ++CurveIndex)
		{
			for (uint32 PointIndex = StrandsCurves.CurvesOffset[CurveIndex]; PointIndex < StrandsCurves.CurvesOffset[CurveIndex + 1]; ++PointIndex)
			{
				const FVector RootPosition = StrandsPoints.PointsPosition[PointIndex];
				UE_LOG(LogHairStrands, Log, TEXT("Signed Distance = %d %d %f"), CurveIndex, PointIndex, LevelSet.SignedDistance(RootPosition));
			}
		}
	}*/
}
