// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsDatas.h"
#include "RenderUtils.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Particles.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Levelset.h"
#include "Chaos/Framework/Parallel.h"
#include "UObject/PhysicsObjectVersion.h"
#include "Async/ParallelFor.h"

// For debug purpose
static float GHairInterpolationMetric_Distance = 1;
static float GHairInterpolationMetric_Angle = 0;
static float GHairInterpolationMetric_Length = 0;
static float GHairInterpolationMetric_AngleAttenuation = 5;
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Distance(TEXT("r.HairStrands.InterpolationMetric.Distance"), GHairInterpolationMetric_Distance, TEXT("Hair strands interpolation metric weights for distance"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Angle(TEXT("r.HairStrands.InterpolationMetric.Angle"), GHairInterpolationMetric_Angle, TEXT("Hair strands interpolation metric weights for angle"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Length(TEXT("r.HairStrands.InterpolationMetric.Length"), GHairInterpolationMetric_Length, TEXT("Hair strands interpolation metric weights for length"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_AngleAttenuation(TEXT("r.HairStrands.InterpolationMetric.AngleAttenuation"), GHairInterpolationMetric_AngleAttenuation, TEXT("Hair strands interpolation angle attenuation"));

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

void DecimateStrandData(const FHairStrandsDatas& InData, float DecimationPercentage, FHairStrandsDatas& OutData)
{
	// Pick randomly strand as guide 
	// Divide strands in buckets and pick randomly one stand per bucket
	const uint32 CurveCount = InData.StrandsCurves.Num();
	const uint32 OutCurveCount = FMath::Clamp(uint32(CurveCount * DecimationPercentage), 1u, CurveCount);

	const uint32 BucketSize = CurveCount / OutCurveCount;

	TArray<uint32> CurveIndices;
	CurveIndices.SetNum(OutCurveCount);

	uint32 OutTotalPointCount = 0;
	FRandomStream Random;
	for (uint32 BucketIndex = 0; BucketIndex < OutCurveCount; BucketIndex++)
	{
		const uint32 CurveIndex = BucketIndex * BucketSize;// +BucketSize * Random.FRand();
		CurveIndices[BucketIndex] = CurveIndex;
		OutTotalPointCount += InData.StrandsCurves.CurvesCount[CurveIndex];
	}

	OutData.StrandsCurves.SetNum(OutCurveCount);
	OutData.StrandsPoints.SetNum(OutTotalPointCount);
	OutData.HairDensity = InData.HairDensity;

	uint32 OutPointOffset = 0; 
	for (uint32 OutCurveIndex = 0; OutCurveIndex < OutCurveCount; OutCurveIndex++)
	{
		const uint32 InCurveIndex = CurveIndices[OutCurveIndex];
		OutData.StrandsCurves.CurvesCount[OutCurveIndex] = InData.StrandsCurves.CurvesCount[InCurveIndex];
		OutData.StrandsCurves.CurvesRootUV[OutCurveIndex] = InData.StrandsCurves.CurvesRootUV[InCurveIndex];
		OutData.StrandsCurves.CurvesOffset[OutCurveIndex] = OutPointOffset;

		const uint32 InPointOffset = InData.StrandsCurves.CurvesOffset[InCurveIndex];
		const uint32 OutPointCount = OutData.StrandsCurves.CurvesCount[OutCurveIndex];
		for (uint32 OutPointIndex = 0; OutPointIndex < OutPointCount; ++OutPointIndex, ++OutPointOffset)
		{
			OutData.StrandsPoints.PointsPosition[OutPointOffset] = InData.StrandsPoints.PointsPosition[OutPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsCoordU[OutPointOffset] = InData.StrandsPoints.PointsCoordU[OutPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsRadius[OutPointOffset] = InData.StrandsPoints.PointsRadius[OutPointIndex + InPointOffset] * InData.StrandsCurves.MaxRadius;
		}
	}

	OutData.BuildInternalDatas();
}

template<typename T>
void SwapValue(T& A, T& B)
{
	T Temp = A;
	A = B;
	B = Temp;
}

void FHairStrandsInterpolationDatas::SetNum(const uint32 NumCurves)
{
	PointsSimCurvesVertexWeights.SetNum(NumCurves);
	PointsSimCurvesVertexIndex.SetNum(NumCurves);
	PointsSimCurvesIndex.SetNum(NumCurves);
}

void FHairStrandsCurves::SetNum(const uint32 NumCurves)
{
	CurvesOffset.SetNum(NumCurves + 1);
	CurvesCount.SetNum(NumCurves);
	CurvesLength.SetNum(NumCurves);
	CurvesRootUV.SetNum(NumCurves);
}

void FHairStrandsPoints::SetNum(const uint32 NumPoints)
{
	PointsPosition.SetNum(NumPoints);
	PointsRadius.SetNum(NumPoints);
	PointsCoordU.SetNum(NumPoints);
}

void FHairStrandsInterpolationDatas::Reset()
{
	PointsSimCurvesVertexWeights.Reset();
	PointsSimCurvesVertexIndex.Reset();
	PointsSimCurvesIndex.Reset();
}

void FHairStrandsCurves::Reset()
{
	CurvesOffset.Reset();
	CurvesCount.Reset();
	CurvesLength.Reset();
	CurvesRootUV.Reset();
}

void FHairStrandsPoints::Reset()
{
	PointsPosition.Reset();
	PointsRadius.Reset();
	PointsCoordU.Reset();
}

void FHairStrandsInterpolationDatas::Serialize(FArchive& Ar)
{
	Ar << PointsSimCurvesVertexWeights;
	Ar << PointsSimCurvesVertexIndex;
	Ar << PointsSimCurvesIndex;
}

void FHairStrandsPoints::Serialize(FArchive& Ar)
{
	Ar << PointsPosition;
	Ar << PointsRadius;
	Ar << PointsCoordU;
}

void FHairStrandsCurves::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);

	Ar << CurvesCount;
	Ar << CurvesOffset;
	Ar << CurvesLength;
	Ar << CurvesRootUV;
	Ar << MaxLength;
	Ar << MaxRadius;

	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::HairAssetSerialization_V2)
	{
		Ar << CurvesGroupID;
	}
}

void FHairStrandsDatas::Serialize(FArchive& Ar)
{
	StrandsPoints.Serialize(Ar);
	StrandsCurves.Serialize(Ar);
	Ar << HairDensity;
	Ar << BoundingBox;
}

void FHairStrandsDatas::Reset()
{
	StrandsCurves.Reset();
	StrandsPoints.Reset();
}

struct FHairRoot
{
	FVector Position;
	uint32  VertexCount;
	FVector Normal;
	uint32  Index;
	float   Length;
};

struct FHairInterpolationMetric
{	
	// Total/combined metrics
	float Metric;

	// Debug info
	float DistanceMetric;
	float AngularMetric;
	float LengthMetric;

	float CosAngle;
	float Distance;
	float GuideLength;
	float RenderLength;

};

static inline FHairInterpolationMetric ComputeInterpolationMetric(const FHairRoot& RenderRoot, const FHairRoot& GuideRoot)
{
	FHairInterpolationMetric Out;
	Out.Distance = FVector::Dist(RenderRoot.Position, GuideRoot.Position);
	Out.CosAngle = FVector::DotProduct(RenderRoot.Normal, GuideRoot.Normal);
	Out.GuideLength = GuideRoot.Length;
	Out.RenderLength = RenderRoot.Length;

	// Metric takes into account the following properties to find guides which are close, share similar orientation, and 
	// have similar length for better interpolation
	// * distance
	// * orientation 
	// * length 
	const float AngularAttenuation = GHairInterpolationMetric_AngleAttenuation > 1 ? GHairInterpolationMetric_AngleAttenuation : 0;
	Out.DistanceMetric	= Out.Distance * GHairInterpolationMetric_Distance;
	Out.AngularMetric	= AngularAttenuation == 0 ? 0 : (FMath::Clamp((1 - FMath::Pow(Out.CosAngle, AngularAttenuation)), 0.f, 1.f) * GHairInterpolationMetric_Angle);
	Out.LengthMetric	= FMath::Abs(FMath::Max(Out.GuideLength / float(Out.RenderLength), Out.RenderLength / float(Out.GuideLength)) - 1) * GHairInterpolationMetric_Length; // Ratio
	Out.Metric			= Out.DistanceMetric + Out.AngularMetric + Out.LengthMetric;

	return Out;
}

template<uint32 NumSamples>
static inline FVector GetCurvePosition(const FHairStrandsDatas& CurvesDatas, const uint32 CurveIndex, const uint32 SampleIndex)
{
	const float PointCount = CurvesDatas.StrandsCurves.CurvesCount[CurveIndex]-1.0;
	const uint32 PointOffset = CurvesDatas.StrandsCurves.CurvesOffset[CurveIndex];

	const float CurvePoint = static_cast<float>(SampleIndex) * PointCount / (static_cast<float>(NumSamples)-1.0f);
	const uint32 PointPrev = (SampleIndex == 0) ? 0 : (SampleIndex == (NumSamples-1)) ? PointCount - 1 : floor(CurvePoint);
	const uint32 PointNext = PointPrev + 1;

	const float PointAlpha = CurvePoint - static_cast<float>(PointPrev);
	return CurvesDatas.StrandsPoints.PointsPosition[PointOffset+PointPrev]*(1.0f-PointAlpha) + 
		   CurvesDatas.StrandsPoints.PointsPosition[PointOffset+PointNext]*PointAlpha;
}

template<uint32 NumSamples>
static inline float ComputeCurvesMetric(const FHairStrandsDatas& RenderCurvesDatas, const uint32 RenderCurveIndex, 
										const FHairStrandsDatas& GuideCurvesDatas, const uint32 GuideCurveIndex, const float RootImportance, 
										const float ShapeImportance, const float ProximityImportance)
{
	const float AverageLength = FMath::Max(0.5f * (RenderCurvesDatas.StrandsCurves.CurvesLength[RenderCurveIndex] * RenderCurvesDatas.StrandsCurves.MaxLength +
											       GuideCurvesDatas.StrandsCurves.CurvesLength[GuideCurveIndex] * GuideCurvesDatas.StrandsCurves.MaxLength), SMALL_NUMBER);

	static const float DeltaCoord = 1.0f / static_cast<float>(NumSamples-1);

	const FVector& RenderRoot = RenderCurvesDatas.StrandsPoints.PointsPosition[RenderCurvesDatas.StrandsCurves.CurvesOffset[RenderCurveIndex]];
	const FVector& GuideRoot = GuideCurvesDatas.StrandsPoints.PointsPosition[GuideCurvesDatas.StrandsCurves.CurvesOffset[GuideCurveIndex]];

	float CurveProximityMetric = 0.0;
	float CurveShapeMetric = 0.0;
	for (uint32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		const FVector GuidePosition = GetCurvePosition<NumSamples>(GuideCurvesDatas, GuideCurveIndex, SampleIndex);
		const FVector RenderPosition = GetCurvePosition<NumSamples>(RenderCurvesDatas, RenderCurveIndex, SampleIndex);
		const float RootWeight = FMath::Exp(-RootImportance*SampleIndex*DeltaCoord);

		CurveProximityMetric += (GuidePosition - RenderPosition).Size() * RootWeight;
		CurveShapeMetric += (GuidePosition - GuideRoot - RenderPosition + RenderRoot).Size() * RootWeight;
	}
	CurveShapeMetric *= DeltaCoord / AverageLength;
	CurveProximityMetric *= DeltaCoord / AverageLength;

	return FMath::Exp(-ShapeImportance*CurveShapeMetric) * FMath::Exp(-ProximityImportance * CurveProximityMetric);
}

static inline void PrintInterpolationMetric(const FHairInterpolationMetric& In)
{
	UE_LOG(LogHairStrands, Log, TEXT("------------------------------------------------------------------------------------------"));
	UE_LOG(LogHairStrands, Log, TEXT("Total Metric = %f"), In.Metric);
	UE_LOG(LogHairStrands, Log, TEXT("Distance     = %f (%f)"), In.Distance, In.DistanceMetric);
	UE_LOG(LogHairStrands, Log, TEXT("Angle        = %f (%f)"), FMath::RadiansToDegrees(FMath::Acos(In.CosAngle)), In.AngularMetric);
	UE_LOG(LogHairStrands, Log, TEXT("Length       = %f/%f (%f)"), In.RenderLength, In.GuideLength, In.LengthMetric);
}

void FHairStrandsInterpolationDatas::BuildInterpolationDatas(
	const FHairStrandsDatas& SimStrandsData,
	const FHairStrandsDatas& RenStrandsData)
{
	SetNum(RenStrandsData.GetNumPoints());

	const bool bRandomizeInterpolation = true;
	const bool bUseUniqueGuide = false;
	const bool bPrintDebugMetric = false;

	typedef TArray<FHairRoot> FRoots;

	// Extract strand roots
	auto ExtractRoot = [](const FHairStrandsDatas& InData, TArray<FHairRoot>& OutRoots)
	{
		const uint32 CurveCount = InData.StrandsCurves.Num();
		OutRoots.Reserve(CurveCount);
		for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
		{
			const uint32 PointOffset = InData.StrandsCurves.CurvesOffset[CurveIndex];
			const uint32 PointCount  = InData.StrandsCurves.CurvesCount[CurveIndex];
			const float  CurveLength = InData.StrandsCurves.CurvesLength[CurveIndex] * InData.StrandsCurves.MaxLength;
			check(PointCount > 1);
			const FVector& P0 = InData.StrandsPoints.PointsPosition[PointOffset];
			const FVector& P1 = InData.StrandsPoints.PointsPosition[PointOffset+1];
			FVector N = (P1 - P0).GetSafeNormal();

			// Fallback in case the initial points are too close (this happens on certain assets)
			if (FVector::DotProduct(N, N) == 0)
			{
				N = FVector(0, 0, 1);
			}
			OutRoots.Add({ P0, PointCount, N, PointOffset, CurveLength });
		}
	};

	FRoots RenRoots, SimRoots;
	ExtractRoot(RenStrandsData, RenRoots);
	ExtractRoot(SimStrandsData, SimRoots);

	// Find k-closest guide:
	// N hairs, M guides. Complexity = MxN
	// #hair_todo: build an acceleration structure for fast(er) look up or at least make it run on parallel (e.g. with ParallelFor)
	uint32 TotalInvalidInterpolationCount = 0;
	const float MinWeightDistance = 0.0001f;
	const static uint32 GuideCount = 3;
	const static uint32 KGuideCount = GuideCount * 2;
	FRandomStream Random;
	const uint32 RenCurveCount = RenStrandsData.GetNumCurves();
	const uint32 SimCurveCount = SimStrandsData.GetNumCurves();
	uint32 RenGlobaPointIndex = 0;
	//ParallelFor(RenCurveCount, [this, &RenRoots, &SimRoots, &SimCurveCount, &RenStrandsData, &SimStrandsData](uint32 RenCurveIndex) {
	for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
	{
		const FHairRoot& StrandRoot = RenRoots[RenCurveIndex];

		int32 ClosestGuideIndices[GuideCount];
		{
			float KMinMetrics[KGuideCount];
			int32 KClosestGuideIndices[KGuideCount];
			for (uint32 ClearIt = 0; ClearIt < KGuideCount; ++ClearIt)
			{
				KMinMetrics[ClearIt] = FLT_MAX;
				KClosestGuideIndices[ClearIt] = -1;
			}

			ParallelFor(SimCurveCount, [this, &KMinMetrics, &KClosestGuideIndices, &RenCurveIndex, &RenStrandsData, &SimStrandsData, &StrandRoot, &SimRoots](uint32 SimCurveIndex) 
			//for (uint32 SimCurveIndex = 0; SimCurveIndex < SimCurveCount; ++SimCurveIndex)
			{
				const FHairRoot& GuideRoot = SimRoots[SimCurveIndex];
				//if ((StrandRoot.Position - GuideRoot.Position).Size() < 5.0f)
				{
					const float Metric = 1.0 - ComputeCurvesMetric<16>(RenStrandsData, RenCurveIndex, SimStrandsData, SimCurveIndex, 0.0f, 1.0f, 1.0f);
					if (Metric < KMinMetrics[KGuideCount - 1])
					{
						int32 LastGuideIndex = SimCurveIndex;
						float LastMetric = Metric;
						for (uint32 Index = 0; Index < KGuideCount; ++Index)
						{
							if (Metric < KMinMetrics[Index])
							{
								SwapValue(KClosestGuideIndices[Index], LastGuideIndex);
								SwapValue(KMinMetrics[Index], LastMetric);
							}
						}
					}
				}

				//const FHairRoot& GuideRoot = SimRoots[SimCurveIndex];
				//const FHairInterpolationMetric Metric = ComputeInterpolationMetric(StrandRoot, GuideRoot);

				//const bool bIsSameHemisphere = GHairInterpolationMetric_AngleAttenuation > 1 ? (Metric.CosAngle > 0) : true;
				//if (bIsSameHemisphere)
				//{
				//	// If the current guide is closer than the furtherest apart guide, we need to update 
				//	// the closest guide set
				//	if (Metric.Metric < KMinMetrics[KGuideCount - 1])
				//	{
				//		int32 LastGuideIndex = SimCurveIndex;
				//		float LastMetric = Metric.Metric;
				//		for (uint32 Index = 0; Index < KGuideCount; ++Index)
				//		{
				//			if (Metric.Metric < KMinMetrics[Index])
				//			{
				//				SwapValue(KClosestGuideIndices[Index], LastGuideIndex);
				//				SwapValue(KMinMetrics[Index], LastMetric);
				//			}
				//		}
				//	}
				//}
			});

			// Debug
			if (bPrintDebugMetric)
			{
				const FHairInterpolationMetric ClosestMetric = ComputeInterpolationMetric(StrandRoot, SimRoots[KClosestGuideIndices[0]]);
				const float Threshold = 20;
				if (ClosestMetric.Metric > Threshold)
				{
					PrintInterpolationMetric(ClosestMetric);
					++TotalInvalidInterpolationCount;
				}
			}

			// Randomize influence guide to break interpolation coherence, and create a more random/natural pattern
			{
				uint32 RandIndex0 = 0;
				uint32 RandIndex1 = 1;
				uint32 RandIndex2 = 2;
				/*if (bRandomizeInterpolation)
				{
					do
					{
						RandIndex0 = Random.RandRange(0, KGuideCount - 1);
						RandIndex1 = Random.RandRange(0, KGuideCount - 1);
						RandIndex2 = Random.RandRange(0, KGuideCount - 1);

					} while (RandIndex0 == RandIndex1 || RandIndex0 == RandIndex2 || RandIndex1 == RandIndex2);
				}*/
				ClosestGuideIndices[0] = KClosestGuideIndices[RandIndex0];
				ClosestGuideIndices[1] = KClosestGuideIndices[RandIndex1];
				ClosestGuideIndices[2] = KClosestGuideIndices[RandIndex2];

				if (bUseUniqueGuide)
				{
					ClosestGuideIndices[1] = KClosestGuideIndices[RandIndex0];
					ClosestGuideIndices[2] = KClosestGuideIndices[RandIndex0];
				}

				float MinMetrics[GuideCount];
				MinMetrics[0] = KMinMetrics[RandIndex0];
				MinMetrics[1] = KMinMetrics[RandIndex1];
				MinMetrics[2] = KMinMetrics[RandIndex2];


				while (!(MinMetrics[0] <= MinMetrics[1] && MinMetrics[1] <= MinMetrics[2]))
				{
					if (MinMetrics[0] > MinMetrics[1])
					{
						SwapValue(MinMetrics[0], MinMetrics[1]);
						SwapValue(ClosestGuideIndices[0], ClosestGuideIndices[1]);
					}

					if (MinMetrics[1] > MinMetrics[2])
					{
						SwapValue(MinMetrics[1], MinMetrics[2]);
						SwapValue(ClosestGuideIndices[1], ClosestGuideIndices[2]);
					}
				}

				// If there less than 3 valid guides, fill the rest with existing valid guides
				// This can happen due to the normal-orientation based rejection above
				if (ClosestGuideIndices[1] < 0)
				{
					ClosestGuideIndices[1] = ClosestGuideIndices[0];
					MinMetrics[1] = MinMetrics[0];
				}
				if (ClosestGuideIndices[2] < 0)
				{
					ClosestGuideIndices[2] = ClosestGuideIndices[1];
					MinMetrics[2] = MinMetrics[1];
				}

				check(ClosestGuideIndices[0] >= 0);
				check(ClosestGuideIndices[1] >= 0);
				check(ClosestGuideIndices[2] >= 0);
				check(MinMetrics[0] <= MinMetrics[1]);
				check(MinMetrics[1] <= MinMetrics[2]);
			}
		}


		const uint32 RendPointCount	= RenStrandsData.StrandsCurves.CurvesCount[RenCurveIndex];
		const uint32 RenOffset		= RenStrandsData.StrandsCurves.CurvesOffset[RenCurveIndex];
		const FVector& RenPointPositionRoot = RenStrandsData.StrandsPoints.PointsPosition[RenOffset];
		for (uint32 RenPointIndex = 0; RenPointIndex < RendPointCount; ++RenPointIndex, ++RenGlobaPointIndex)
		{
			const FVector& RenPointPosition = RenStrandsData.StrandsPoints.PointsPosition[RenPointIndex + RenOffset];
			const float RenPointDistance = RenStrandsData.StrandsPoints.PointsCoordU[RenPointIndex + RenOffset] * RenStrandsData.StrandsCurves.CurvesLength[RenCurveIndex] * RenStrandsData.StrandsCurves.MaxLength;

			float TotalWeight = 0;
			for (uint32 KIndex = 0; KIndex < GuideCount; ++KIndex)
			{

			#define WEIGHT_METHOD 0

				// Find the closest vertex on the guide which matches the strand vertex distance along its curve
			#if WEIGHT_METHOD == 0
				const uint32 SimCurveIndex = ClosestGuideIndices[KIndex];
				const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];
				float PrevSimPointDistance = 0;
				uint32 ClosestSimPointIndex = 0;
				const uint32 SimPointCount = SimStrandsData.StrandsCurves.CurvesCount[SimCurveIndex];
				for (uint32 SimPointIndex = 0; SimPointIndex < SimPointCount; ++SimPointIndex, ++ClosestSimPointIndex)
				{
					const float SimPointDistance = SimStrandsData.StrandsPoints.PointsCoordU[SimPointIndex + SimOffset] * SimStrandsData.StrandsCurves.CurvesLength[SimCurveIndex] * SimStrandsData.StrandsCurves.MaxLength;
					if (RenPointDistance >= PrevSimPointDistance && RenPointDistance <= SimPointDistance)
					{
						//const float GuideDistance = Guide.Points[SimPointIndex].Distance;
						const float D0 = FMath::Abs(PrevSimPointDistance - RenPointDistance);
						const float D1 = FMath::Abs(SimPointDistance - RenPointDistance);
						if (D0 > D1)
						{
							ClosestSimPointIndex = SimPointIndex;
						}
						break;
					}
					PrevSimPointDistance = SimPointDistance;					
				}
				check(SimPointCount > 0);
				ClosestSimPointIndex = FMath::Clamp(ClosestSimPointIndex, 0u, uint32(SimPointCount - 1));

				
				
				const FVector& SimPointPosition = SimStrandsData.StrandsPoints.PointsPosition[ClosestSimPointIndex + SimOffset];
				const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenPointPosition, SimPointPosition));
				PointsSimCurvesIndex[RenGlobaPointIndex][KIndex] = SimCurveIndex;
				PointsSimCurvesVertexIndex[RenGlobaPointIndex][KIndex] = ClosestSimPointIndex + SimOffset;
				PointsSimCurvesVertexWeights[RenGlobaPointIndex][KIndex] = Weight;
			#endif

				// Use only the root as a *constant* weight for deformation along each vertex
			#if WEIGHT_METHOD == 1
				const uint32 SimCurveIndex = ClosestGuideIndices[KIndex];
				const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];
				const FVector& SimRootPointPosition = SimStrandsData.StrandsPoints[SimOffset];
				const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenPointPosition, SimRootPointPosition));
				PointsSimCurvesIndex[RenGlobaPointIndex][KIndex] = SimCurveIndex;
				PointsSimCurvesVertexIndex[RenGlobaPointIndex][KIndex] = SimOffset;
				PointsSimCurvesVertexWeights[RenGlobaPointIndex][KIndex] = Weight;

			#endif

				// Use the *same vertex index* to match guide vertex with strand vertex
			#if WEIGHT_METHOD == 2
				check(SimPointCount > 0);
				const uint32 SimCurveIndex = ClosestGuideIndices[KIndex];
				const uint32 SimPointIndex = FMath::Clamp(RenPointIndex, 0, SimPointCount - 1);
				const FVector& SimPointPosition = SimStrandsData.StrandsPoints[SimPointIndex + SimOffset];
				const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenPointPosition, SimPointPosition));
				PointsSimCurvesIndex[RenGlobaPointIndex][KIndex] = SimCurveIndex;
				PointsSimCurvesVertexIndex[RenGlobaPointIndex][KIndex] = SimPointIndex + SimOffset;
				PointsSimCurvesVertexWeights[RenGlobaPointIndex][KIndex] = Weight;
			#endif

				TotalWeight += Weight;
			}

			for (int32 KIndex = 0; KIndex < GuideCount; ++KIndex)
			{
				PointsSimCurvesVertexWeights[RenGlobaPointIndex][KIndex] /= TotalWeight;
			}
		}
	}

	if (bPrintDebugMetric)
	{
		UE_LOG(LogHairStrands, Log, TEXT("Invalid interpolation count: %d/%d)"), TotalInvalidInterpolationCount, RenCurveCount);
	}
}

void FHairStrandsInterpolationDatas::BuildRenderingDatas(
	TArray<FHairStrandsInterpolation0Format::Type>& OutPointsInterpolation0,
	TArray<FHairStrandsInterpolation1Format::Type>& OutPointsInterpolation1) const
{
	const uint32 PointCount = Num();
	if (PointCount == 0)
		return;
	auto LowerPart = [](uint32 Index) { return uint16(Index & 0xFFFF); };
	auto UpperPart = [](uint32 Index) { return uint8((Index >> 16) & 0xFF); };

	OutPointsInterpolation0.SetNum(PointCount*FHairStrandsInterpolation0Format::ComponentCount);
	OutPointsInterpolation1.SetNum(PointCount*FHairStrandsInterpolation1Format::ComponentCount);

	for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		const FIntVector& Indices = PointsSimCurvesVertexIndex[PointIndex];
		const FVector& Weights = PointsSimCurvesVertexWeights[PointIndex];

		FHairStrandsInterpolation0Format::Type& OutInterp0 = OutPointsInterpolation0[PointIndex];
		OutInterp0.Index0 = LowerPart(Indices[0]);
		OutInterp0.Index1 = LowerPart(Indices[1]);
		OutInterp0.Index2 = LowerPart(Indices[2]);
		OutInterp0.VertexWeight0 = Weights[0] * 255.f;
		OutInterp0.VertexWeight1 = Weights[1] * 255.f;

		FHairStrandsInterpolation1Format::Type& OutInterp1 = OutPointsInterpolation1[PointIndex];
		OutInterp1.VertexIndex0 = UpperPart(Indices[0]);
		OutInterp1.VertexIndex1 = UpperPart(Indices[1]);
		OutInterp1.VertexIndex2 = UpperPart(Indices[2]);
		OutInterp1.Pad0 = 0;
	}	
}

void FHairStrandsDatas::BuildRenderingDatas(
	TArray<FHairStrandsPositionFormat::Type>& OutPackedPositions,
	TArray<FHairStrandsAttributeFormat::Type>& OutPackedAttributes) const
{
	if (!(GetNumCurves() > 0 && GetNumPoints() > 0))
		return;

	OutPackedPositions.SetNum(GetNumPoints()*FHairStrandsPositionFormat::ComponentCount);
	OutPackedAttributes.SetNum(GetNumPoints()*FHairStrandsAttributeFormat::ComponentCount);
	
	const FVector HairBoxCenter = BoundingBox.GetCenter();

	FRandomStream Random;
	for (uint32 CurveIndex = 0; CurveIndex < GetNumCurves(); ++CurveIndex)
	{
		const float CurveSeed = Random.RandHelper(255);
		const int32 IndexOffset = StrandsCurves.CurvesOffset[CurveIndex];
		const uint16& PointCount = StrandsCurves.CurvesCount[CurveIndex];
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const uint32 PrevIndex = FMath::Max(0, PointIndex - 1);
			const uint32 NextIndex = FMath::Min(PointCount + 1, PointCount - 1);
			const FVector& PointPosition = StrandsPoints.PointsPosition[PointIndex + IndexOffset];

			const float CoordU = StrandsPoints.PointsCoordU[PointIndex + IndexOffset];
			const float NormalizedRadius = StrandsPoints.PointsRadius[PointIndex + IndexOffset];
			const float NormalizedLength = CoordU * StrandsCurves.CurvesLength[CurveIndex];

			FHairStrandsPositionFormat::Type& PackedPosition = OutPackedPositions[PointIndex + IndexOffset];
			CopyVectorToPosition(PointPosition - HairBoxCenter, PackedPosition);
			PackedPosition.ControlPointType = (PointIndex == 0) ? 1u : (PointIndex == (PointCount - 1) ? 2u : 0u);
			PackedPosition.NormalizedRadius = uint8(FMath::Clamp(NormalizedRadius * 63.f, 0.f, 63.f));
			PackedPosition.NormalizedLength = uint8(FMath::Clamp(NormalizedLength *255.f, 0.f, 255.f));
			
			const FVector2D RootUV = StrandsCurves.CurvesRootUV[CurveIndex];
			FHairStrandsAttributeFormat::Type& PackedAttributes = OutPackedAttributes[PointIndex + IndexOffset];
			PackedAttributes.RootU = uint32(FMath::Clamp(RootUV.X, 0.f, 1.f) * 0xFF);
			PackedAttributes.RootV = uint32(FMath::Clamp(RootUV.Y, 0.f, 1.f) * 0xFF);
			PackedAttributes.UCoord = FMath::Clamp(CoordU * 255.f, 0.f, 255.f);
			PackedAttributes.Seed = CurveSeed;
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

///////////////////////////////////////////////////////////////////////////////////////////////////
// Auto-generate Root UV data if not loaded
static FVector2D SignNotZero(const FVector2D& v)
{
	return FVector2D((v.X >= 0.0) ? +1.0 : -1.0, (v.Y >= 0.0) ? +1.0 : -1.0);
}

// A Survey of Efficient Representations for Independent Unit Vectors
// Reference: http://jcgt.org/published/0003/02/01/paper.pdf
// Assume normalized input. Output is on [-1, 1] for each component.
static FVector2D SphericalToOctahedron(const FVector& v)
{
	// Project the sphere onto the octahedron, and then onto the xy plane
	FVector2D p = FVector2D(v.X, v.Y) * (1.0 / (abs(v.X) + abs(v.Y) + abs(v.Z)));
	// Reflect the folds of the lower hemisphere over the diagonals
	return (v.Z <= 0.0) ? ((FVector2D(1, 1) - FVector2D(abs(p.Y), abs(p.X))) * SignNotZero(p)) : p;
}

static void ComputeRootUV(FHairStrandsCurves& Curves, FHairStrandsPoints& Points)//; FStrands& Strands)
{
	TArray<FVector> RootPoints;
	const uint32 CurveCount = Curves.Num();
	RootPoints.Reserve(Curves.Num());
	FVector MinAABB(MAX_FLT, MAX_FLT, MAX_FLT);
	FVector MaxAABB(-MAX_FLT, -MAX_FLT, -MAX_FLT);
	FMatrix Rotation = FRotationMatrix::Make(FRotator(0, 0, -90));
	for (uint32 CurveIndex=0; CurveIndex< CurveCount; ++CurveIndex)
	{
		const uint32 Offset = Curves.CurvesOffset[CurveIndex];
		check(Offset < uint32(Points.PointsPosition.Num()));
		const FVector P = Rotation.TransformPosition(Points.PointsPosition[Offset]);

		RootPoints.Add(P);
		MinAABB.X = FMath::Min(P.X, MinAABB.X);
		MinAABB.Y = FMath::Min(P.Y, MinAABB.Y);
		MinAABB.Z = FMath::Min(P.Z, MinAABB.Z);

		MaxAABB.X = FMath::Max(P.X, MaxAABB.X);
		MaxAABB.Y = FMath::Max(P.Y, MaxAABB.Y);
		MaxAABB.Z = FMath::Max(P.Z, MaxAABB.Z);
	}

	// Compute sphere bound
	const FVector Extent = MaxAABB - MinAABB;
	FSphere SBound;
	SBound.Center = (MaxAABB + MinAABB) * 0.5f;
	SBound.W = FMath::Max(Extent.X, FMath::Max(Extent.Y, Extent.Z));

	// Project root point onto the bounding sphere and map it onto 
	// an octahedron, which is unfold onto the unit space [0,1]^2
	TArray<FVector2D> RootUVs;
	RootUVs.Reserve(Curves.Num());
	FVector2D MinUV(MAX_FLT, MAX_FLT);
	FVector2D MaxUV(-MAX_FLT, -MAX_FLT);
	for (const FVector& RootP : RootPoints)
	{
		FVector D = RootP - SBound.Center;
		D.Normalize();
		FVector2D UV = SphericalToOctahedron(D);
		UV += FVector2D(1, 1);
		UV *= 0.5f;
		RootUVs.Add(UV);

		MinUV.X = FMath::Min(UV.X, MinUV.X);
		MinUV.Y = FMath::Min(UV.Y, MinUV.Y);

		MaxUV.X = FMath::Max(UV.X, MaxUV.X);
		MaxUV.Y = FMath::Max(UV.Y, MaxUV.Y);
	}

	// Find the minimal UV space cover by root point, and 
	// offsets/scales it to maximize UV space
	const FVector2D UVScale(1 / (MaxUV.X - MinUV.X), 1 / (MaxUV.Y - MinUV.Y));
	const FVector2D UVOffset(-MinUV.X, -MinUV.Y);
	uint32 Index = 0;
	for (FVector2D& RootUV : Curves.CurvesRootUV)
	{
		RootUV = (RootUVs[Index++] + UVOffset) * UVScale;
	}
}

void FHairStrandsDatas::BuildInternalDatas(bool bComputeRootUV)
{
	BoundingBox.Min = {  FLT_MAX,  FLT_MAX ,  FLT_MAX };
	BoundingBox.Max = { -FLT_MAX, -FLT_MAX , -FLT_MAX };

	if (GetNumCurves() > 0 && GetNumPoints() > 0)
	{
		TArray<FVector>::TIterator PositionIterator = StrandsPoints.PointsPosition.CreateIterator();
		TArray<float>::TIterator RadiusIterator = StrandsPoints.PointsRadius.CreateIterator();
		TArray<float>::TIterator CoordUIterator = StrandsPoints.PointsCoordU.CreateIterator();

		TArray<uint16>::TIterator CountIterator = StrandsCurves.CurvesCount.CreateIterator();
		TArray<uint32>::TIterator OffsetIterator = StrandsCurves.CurvesOffset.CreateIterator();
		TArray<float>::TIterator LengthIterator = StrandsCurves.CurvesLength.CreateIterator();

		StrandsCurves.MaxRadius = 0.0;
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
				BoundingBox += *PositionIterator;

				if (PointIndex > 0)
				{
					StrandLength += (*PositionIterator - PreviousPosition).Size();
				}
				*CoordUIterator = StrandLength;
				PreviousPosition = *PositionIterator;

				StrandsCurves.MaxRadius = FMath::Max(StrandsCurves.MaxRadius, *RadiusIterator);
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
				*RadiusIterator /= StrandsCurves.MaxRadius;
			}
			*LengthIterator /= StrandsCurves.MaxLength;
		}

		if (bComputeRootUV)
		{
			ComputeRootUV(StrandsCurves, StrandsPoints);
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
