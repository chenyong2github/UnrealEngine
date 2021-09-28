// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBuilder.h"
#include "GroomAsset.h"
#include "GroomComponent.h"
#include "GroomSettings.h"
#include "HairDescription.h"
#include "GroomSettings.h"

#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/ScopedSlowTask.h"

DEFINE_LOG_CATEGORY_STATIC(LogGroomBuilder, Log, All);

#define LOCTEXT_NAMESPACE "GroomBuilder"

// For debug purpose
static float GHairInterpolationMetric_Distance = 1;
static float GHairInterpolationMetric_Angle = 0;
static float GHairInterpolationMetric_Length = 0;
static float GHairInterpolationMetric_AngleAttenuation = 5;
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Distance(TEXT("r.HairStrands.InterpolationMetric.Distance"), GHairInterpolationMetric_Distance, TEXT("Hair strands interpolation metric weights for distance"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Angle(TEXT("r.HairStrands.InterpolationMetric.Angle"), GHairInterpolationMetric_Angle, TEXT("Hair strands interpolation metric weights for angle"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_Length(TEXT("r.HairStrands.InterpolationMetric.Length"), GHairInterpolationMetric_Length, TEXT("Hair strands interpolation metric weights for length"));
static FAutoConsoleVariableRef CVarHairInterpolationMetric_AngleAttenuation(TEXT("r.HairStrands.InterpolationMetric.AngleAttenuation"), GHairInterpolationMetric_AngleAttenuation, TEXT("Hair strands interpolation angle attenuation"));

static int32 GHairClusterBuilder_MaxVoxelResolution = 256;
static FAutoConsoleVariableRef CVarHairClusterBuilder_MaxVoxelResolution(TEXT("r.HairStrands.ClusterBuilder.MaxVoxelResolution"), GHairClusterBuilder_MaxVoxelResolution, TEXT("Max voxel resolution used when building hair strands cluster data to avoid too long building time (default:128).  "));

FString FGroomBuilder::GetVersion()
{
	// Important to update the version when groom building changes
	return TEXT("1c");
}

namespace HairStrandsBuilder
{
	FVector2D SignNotZero(const FVector2D& v)
	{
		return FVector2D((v.X >= 0.0) ? +1.0 : -1.0, (v.Y >= 0.0) ? +1.0 : -1.0);
	}

	// A Survey of Efficient Representations for Independent Unit Vectors
	// Reference: http://jcgt.org/published/0003/02/01/paper.pdf
	// Assume normalized input. Output is on [-1, 1] for each component.
	FVector2D SphericalToOctahedron(const FVector& v)
	{
		// Project the sphere onto the octahedron, and then onto the xy plane
		FVector2D p = FVector2D(v.X, v.Y) * (1.0 / (abs(v.X) + abs(v.Y) + abs(v.Z)));
		// Reflect the folds of the lower hemisphere over the diagonals
		return (v.Z <= 0.0) ? ((FVector2D(1, 1) - FVector2D(abs(p.Y), abs(p.X))) * SignNotZero(p)) : p;
	}

	// Auto-generate Root UV data if not loaded
	void ComputeRootUV(FHairStrandsCurves& Curves, FHairStrandsPoints& Points)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairStrandsBuilder::ComputeRootUV);

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

	/** Build the internal points and curves data */
	void BuildInternalData(FHairStrandsDatas& HairStrands, bool bComputeRootUV)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairStrandsBuilder::BuildInternalData);

		FHairStrandsCurves& Curves = HairStrands.StrandsCurves;
		FHairStrandsPoints& Points = HairStrands.StrandsPoints;

		HairStrands.BoundingBox.Min = {  FLT_MAX,  FLT_MAX ,  FLT_MAX };
		HairStrands.BoundingBox.Max = { -FLT_MAX, -FLT_MAX , -FLT_MAX };

		if (HairStrands.GetNumCurves() > 0 && HairStrands.GetNumPoints() > 0)
		{
			TArray<FVector>::TIterator PositionIterator = Points.PointsPosition.CreateIterator();
			TArray<float>::TIterator RadiusIterator = Points.PointsRadius.CreateIterator();
			TArray<float>::TIterator CoordUIterator = Points.PointsCoordU.CreateIterator();

			TArray<uint16>::TIterator CountIterator = Curves.CurvesCount.CreateIterator();
			TArray<uint32>::TIterator OffsetIterator = Curves.CurvesOffset.CreateIterator();
			TArray<float>::TIterator LengthIterator = Curves.CurvesLength.CreateIterator();

			Curves.MaxRadius = 0.0;
			Curves.MaxLength = 0.0;

			uint32 StrandOffset = 0;
			*OffsetIterator = StrandOffset; ++OffsetIterator;

			for (uint32 CurveIndex = 0; CurveIndex < HairStrands.GetNumCurves(); ++CurveIndex, ++OffsetIterator, ++LengthIterator, ++CountIterator)
			{
				const uint16& StrandCount = *CountIterator;

				StrandOffset += StrandCount;
				*OffsetIterator = StrandOffset;

				float StrandLength = 0.0;
				FVector PreviousPosition(0.0, 0.0, 0.0);
				for (uint32 PointIndex = 0; PointIndex < StrandCount; ++PointIndex, ++PositionIterator, ++RadiusIterator, ++CoordUIterator)
				{
					HairStrands.BoundingBox += *PositionIterator;

					if (PointIndex > 0)
					{
						StrandLength += (*PositionIterator - PreviousPosition).Size();
					}
					*CoordUIterator = StrandLength;
					PreviousPosition = *PositionIterator;

					Curves.MaxRadius = FMath::Max(Curves.MaxRadius, *RadiusIterator);
				}
				*LengthIterator = StrandLength;
				Curves.MaxLength = FMath::Max(Curves.MaxLength, StrandLength);
			}

			CountIterator.Reset();
			LengthIterator.Reset();
			RadiusIterator.Reset();
			CoordUIterator.Reset();

			for (uint32 CurveIndex = 0; CurveIndex < HairStrands.GetNumCurves(); ++CurveIndex, ++LengthIterator, ++CountIterator)
			{
				const uint16& StrandCount = *CountIterator;

				for (uint32 PointIndex = 0; PointIndex < StrandCount; ++PointIndex, ++RadiusIterator, ++CoordUIterator)
				{
					*CoordUIterator /= *LengthIterator;
					*RadiusIterator /= Curves.MaxRadius;
				}
				*LengthIterator /= Curves.MaxLength;
			}

			if (bComputeRootUV)
			{
				ComputeRootUV(Curves, Points);
			}
		}
	}

	inline void CopyVectorToPosition(const FVector& InVector, FHairStrandsPositionFormat::Type& OutPosition)
	{
		OutPosition.X = InVector.X;
		OutPosition.Y = InVector.Y;
		OutPosition.Z = InVector.Z;
	}

	/** Build the packed datas for gpu rendering/simulation */
	void BuildRenderData(FHairStrandsDatas& HairStrands, const TArray<uint8>& RandomSeeds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairStrandsBuilder::BuildRenderData);

		uint32 NumCurves = HairStrands.GetNumCurves();
		uint32 NumPoints = HairStrands.GetNumPoints();
		if (!(NumCurves > 0 && NumPoints > 0))
			return;

		TArray<FHairStrandsPositionFormat::Type>& OutPackedPositions = HairStrands.RenderData.Positions;
		TArray<FHairStrandsAttributeFormat::Type>& OutPackedAttributes = HairStrands.RenderData.Attributes;
		TArray<FHairStrandsMaterialFormat::Type>& OutPackedMaterials = HairStrands.RenderData.Materials;

		OutPackedPositions.SetNum(NumPoints * FHairStrandsPositionFormat::ComponentCount);
		OutPackedAttributes.SetNum(NumPoints * FHairStrandsAttributeFormat::ComponentCount);
		OutPackedMaterials.SetNum(NumPoints * FHairStrandsMaterialFormat::ComponentCount);

		const FVector HairBoxCenter = HairStrands.BoundingBox.GetCenter();

		FHairStrandsCurves& Curves = HairStrands.StrandsCurves;
		FHairStrandsPoints& Points = HairStrands.StrandsPoints;

		const bool bSeedValid = RandomSeeds.Num() > 0;
		for (uint32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
		{
			const uint8 CurveSeed = bSeedValid ? RandomSeeds[CurveIndex] : 0;
			const int32 IndexOffset = Curves.CurvesOffset[CurveIndex];
			const uint16& PointCount = Curves.CurvesCount[CurveIndex];
			for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
			{
				const uint32 PrevIndex = FMath::Max(0, PointIndex - 1);
				const uint32 NextIndex = FMath::Min(PointCount + 1, PointCount - 1);
				const FVector& PointPosition = Points.PointsPosition[PointIndex + IndexOffset];

				const float CoordU = Points.PointsCoordU[PointIndex + IndexOffset];
				const float NormalizedRadius = Points.PointsRadius[PointIndex + IndexOffset];
				const float NormalizedLength = CoordU * Curves.CurvesLength[CurveIndex];

				FHairStrandsPositionFormat::Type& PackedPosition = OutPackedPositions[PointIndex + IndexOffset];
				CopyVectorToPosition(PointPosition - HairBoxCenter, PackedPosition);
				PackedPosition.ControlPointType = (PointIndex == 0) ? 1u : (PointIndex == (PointCount - 1) ? 2u : 0u);
				PackedPosition.NormalizedRadius = uint8(FMath::Clamp(NormalizedRadius * 63.f, 0.f, 63.f));
				PackedPosition.NormalizedLength = uint8(FMath::Clamp(NormalizedLength *255.f, 0.f, 255.f));

				const FVector2D RootUV = Curves.CurvesRootUV[CurveIndex];
				FHairStrandsAttributeFormat::Type& PackedAttributes = OutPackedAttributes[PointIndex + IndexOffset];
				PackedAttributes.Unused0 = 0;
				PackedAttributes.Unused1 = 0;
				PackedAttributes.UCoord = FMath::Clamp(CoordU * 255.f, 0.f, 255.f);
				PackedAttributes.Seed = CurveSeed;

				// Root UV support UDIM texture coordinate but limit the spans of the UDIM to be in 256x256 instead of 9999x9999.
				// The internal UV coords are also limited to 8bits, which means if sampling need to be super precise, this is no enough.
				const FVector2D TextureRootUV(FMath::Fractional(RootUV.X), FMath::Fractional(RootUV.Y));
				const FVector2D TextureIndexUV = RootUV - TextureRootUV;
				PackedAttributes.RootU  = uint32(FMath::Clamp(TextureRootUV.X*255.f, 0.f, 255.f));
				PackedAttributes.RootV  = uint32(FMath::Clamp(TextureRootUV.Y*255.f, 0.f, 255.f));
				PackedAttributes.IndexU = uint32(FMath::Clamp(TextureIndexUV.X, 0.f, 255.f));
				PackedAttributes.IndexV = uint32(FMath::Clamp(TextureIndexUV.Y, 0.f, 255.f));

				FHairStrandsMaterialFormat::Type& Material = OutPackedMaterials[PointIndex + IndexOffset];
				// Cheap sRGB encoding instead of PointsBaseColor.ToFColor(true), as this makes the decompression 
				// cheaper on GPU (since R8G8B8A8 sRGB format used/exposed not exposed)
				Material.BaseColorR = FMath::Clamp(uint32(FMath::Sqrt(Points.PointsBaseColor[PointIndex + IndexOffset].R) * 0xFF), 0u, 0xFFu);
				Material.BaseColorG = FMath::Clamp(uint32(FMath::Sqrt(Points.PointsBaseColor[PointIndex + IndexOffset].G) * 0xFF), 0u, 0xFFu);
				Material.BaseColorB = FMath::Clamp(uint32(FMath::Sqrt(Points.PointsBaseColor[PointIndex + IndexOffset].B) * 0xFF), 0u, 0xFFu);
				Material.Roughness  = FMath::Clamp(uint32(Points.PointsRoughness[PointIndex + IndexOffset] * 0xFF), 0u, 0xFFu);
			}
		}
	}

	void BuildRenderData(FHairStrandsDatas& HairStrands)
	{
		TArray<uint8> RandomSeeds;
		BuildRenderData(HairStrands, RandomSeeds);
	}

} // namespace HairStrandsBuilder

namespace HairInterpolationBuilder
{
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

	inline FHairInterpolationMetric ComputeInterpolationMetric(const FHairRoot& RenderRoot, const FHairRoot& GuideRoot)
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
	inline FVector GetCurvePosition(const FHairStrandsDatas& CurvesDatas, const uint32 CurveIndex, const uint32 SampleIndex)
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
	inline float ComputeCurvesMetric(const FHairStrandsDatas& RenderCurvesDatas, const uint32 RenderCurveIndex, 
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

	inline void PrintInterpolationMetric(const FHairInterpolationMetric& In)
	{
		UE_LOG(LogGroomBuilder, Log, TEXT("------------------------------------------------------------------------------------------"));
		UE_LOG(LogGroomBuilder, Log, TEXT("Total Metric = %f"), In.Metric);
		UE_LOG(LogGroomBuilder, Log, TEXT("Distance     = %f (%f)"), In.Distance, In.DistanceMetric);
		UE_LOG(LogGroomBuilder, Log, TEXT("Angle        = %f (%f)"), FMath::RadiansToDegrees(FMath::Acos(In.CosAngle)), In.AngularMetric);
		UE_LOG(LogGroomBuilder, Log, TEXT("Length       = %f/%f (%f)"), In.RenderLength, In.GuideLength, In.LengthMetric);
	}

	template<typename T>
	void SwapValue(T& A, T& B)
	{
		T Temp = A;
		A = B;
		B = Temp;
	}

	struct FMetrics
	{
		static const uint32 Count = 3;
		float KMinMetrics[Count];
		int32 KClosestGuideIndices[Count];
	};

	struct FClosestGuides
	{
		static const uint32 Count = 3;
		int32 Indices[Count];
	};

	// Randomize influence guide to break interpolation coherence, and create a more random/natural pattern
	static void SelectFinalGuides(
		FClosestGuides& ClosestGuides, 
		const FIntVector& RandomIndices,
		const FMetrics& InMetric, 
		const bool bRandomizeInterpolation, 
		const bool bUseUniqueGuide)
	{
		FMetrics Metric = InMetric;
		check(Metric.KClosestGuideIndices[0] >= 0);

		// If some indices are invalid (for instance, found a valid single guide, fill in the rest with the valid ones)
		if (Metric.KClosestGuideIndices[1] < 0)
		{
			Metric.KClosestGuideIndices[1] = Metric.KClosestGuideIndices[0];
			Metric.KMinMetrics[1] = Metric.KMinMetrics[0];
		}
		if (Metric.KClosestGuideIndices[2] < 0)
		{
			Metric.KClosestGuideIndices[2] = Metric.KClosestGuideIndices[1];
			Metric.KMinMetrics[2] = Metric.KMinMetrics[1];
		}

		uint32 RandIndex0 = 0;
		uint32 RandIndex1 = 1;
		uint32 RandIndex2 = 2;
		if (bRandomizeInterpolation)
		{
			// This randomization makes certain strands being affected by 1, 2, or 3 guides
			RandIndex0 = RandomIndices[0];
			RandIndex1 = RandomIndices[1];
			RandIndex2 = RandomIndices[2];
		}

		ClosestGuides.Indices[0] = Metric.KClosestGuideIndices[RandIndex0];
		ClosestGuides.Indices[1] = Metric.KClosestGuideIndices[RandIndex1];
		ClosestGuides.Indices[2] = Metric.KClosestGuideIndices[RandIndex2];

		if (bUseUniqueGuide)
		{
			ClosestGuides.Indices[1] = Metric.KClosestGuideIndices[RandIndex0];
			ClosestGuides.Indices[2] = Metric.KClosestGuideIndices[RandIndex0];
			RandIndex1 = RandIndex0;
			RandIndex2 = RandIndex0;
		}


		float MinMetrics[FMetrics::Count];
		MinMetrics[0] = Metric.KMinMetrics[RandIndex0];
		MinMetrics[1] = Metric.KMinMetrics[RandIndex1];
		MinMetrics[2] = Metric.KMinMetrics[RandIndex2];


		while (!(MinMetrics[0] <= MinMetrics[1] && MinMetrics[1] <= MinMetrics[2]))
		{
			if (MinMetrics[0] > MinMetrics[1])
			{
				SwapValue(MinMetrics[0], MinMetrics[1]);
				SwapValue(ClosestGuides.Indices[0], ClosestGuides.Indices[1]);
			}

			if (MinMetrics[1] > MinMetrics[2])
			{
				SwapValue(MinMetrics[1], MinMetrics[2]);
				SwapValue(ClosestGuides.Indices[1], ClosestGuides.Indices[2]);
			}
		}

		// If there less than 3 valid guides, fill the rest with existing valid guides
		// This can happen due to the normal-orientation based rejection above
		if (ClosestGuides.Indices[1] < 0)
		{
			ClosestGuides.Indices[1] = ClosestGuides.Indices[0];
			MinMetrics[1] = MinMetrics[0];
		}
		if (ClosestGuides.Indices[2] < 0)
		{
			ClosestGuides.Indices[2] = ClosestGuides.Indices[1];
			MinMetrics[2] = MinMetrics[1];
		}

		check(MinMetrics[0] <= MinMetrics[1]);
		check(MinMetrics[1] <= MinMetrics[2]);
	}

	// Simple closest distance metric
	static void ComputeSimpleMetric(
		FMetrics& Metrics1, 
		const FHairRoot& RenRoot, 
		const FHairRoot& GuideRoot, 
		const int32 RenCurveIndex,
		const int32 SimCurveIndex)
	{
		const float Metric = FVector::Dist(GuideRoot.Position, RenRoot.Position);
		if (Metric < Metrics1.KMinMetrics[FMetrics::Count - 1])
		{
			int32 LastGuideIndex = SimCurveIndex;
			float LastMetric = Metric;
			for (uint32 Index = 0; Index < FMetrics::Count; ++Index)
			{
				if (Metric < Metrics1.KMinMetrics[Index])
				{
					SwapValue(Metrics1.KClosestGuideIndices[Index], LastGuideIndex);
					SwapValue(Metrics1.KMinMetrics[Index], LastMetric);
				}
			}
		}
	}

	// Complex pairing metric
	static void ComputeAdvandedMetric(FMetrics& Metrics0,
		const FHairStrandsDatas& RenStrandsData,
		const FHairStrandsDatas& SimStrandsData,
		const int32 RenCurveIndex,
		const int32 SimCurveIndex)
	{
		const float Metric = 1.0 - ComputeCurvesMetric<16>(RenStrandsData, RenCurveIndex, SimStrandsData, SimCurveIndex, 0.0f, 1.0f, 1.0f);
		if (Metric < Metrics0.KMinMetrics[FMetrics::Count - 1])
		{
			int32 LastGuideIndex = SimCurveIndex;
			float LastMetric = Metric;
			for (uint32 Index = 0; Index < FMetrics::Count; ++Index)
			{
				if (Metric < Metrics0.KMinMetrics[Index])
				{
					SwapValue(Metrics0.KClosestGuideIndices[Index], LastGuideIndex);
					SwapValue(Metrics0.KMinMetrics[Index], LastMetric);
				}
			}
		}
	}

	struct FRootsGrid
	{
		FVector MinBound = FVector::ZeroVector;
		FVector MaxBound = FVector::ZeroVector;
		
		const uint32 MaxLookupDistance = 31;
		const FIntVector GridResolution = FIntVector(32, 32, 32);

		TArray<int32> GridIndirection;
		TArray<TArray<int32>> RootIndices;
		
		FORCEINLINE bool IsValid(const FIntVector& P) const
		{
			return	0 <= P.X && P.X < GridResolution.X &&
					0 <= P.Y && P.Y < GridResolution.Y &&
					0 <= P.Z && P.Z < GridResolution.Z;
		}

		FORCEINLINE FIntVector ClampToVolume(const FIntVector& CellCoord, bool& bIsValid) const
		{
			bIsValid = IsValid(CellCoord);
			return FIntVector(
				FMath::Clamp(CellCoord.X, 0, GridResolution.X - 1),
				FMath::Clamp(CellCoord.Y, 0, GridResolution.Y - 1),
				FMath::Clamp(CellCoord.Z, 0, GridResolution.Z - 1));
		}

		FORCEINLINE FIntVector ToCellCoord(const FVector& P) const
		{
			bool bIsValid = false;
			const FVector F = ((P - MinBound) / (MaxBound - MinBound));
			const FIntVector CellCoord = FIntVector(FMath::FloorToInt(F.X * GridResolution.X), FMath::FloorToInt(F.Y * GridResolution.Y), FMath::FloorToInt(F.Z * GridResolution.Z));			
			return ClampToVolume(CellCoord, bIsValid);
		}

		uint32 ToIndex(const FIntVector& CellCoord) const
		{
			uint32 CellIndex = CellCoord.X + CellCoord.Y * GridResolution.X + CellCoord.Z * GridResolution.X * GridResolution.Y;
			check(CellIndex < uint32(GridIndirection.Num()));
			return CellIndex;
		}

		void InsertRoots(TArray<FHairRoot>& Roots, const FVector& InMinBound, const FVector& InMaxBound)
		{
			MinBound = InMinBound;
			MaxBound = InMaxBound;
			GridIndirection.SetNumZeroed(GridResolution.X*GridResolution.Y*GridResolution.Z);
			RootIndices.Empty();
			RootIndices.AddDefaulted(); // Add a default empty list for the null element

			const uint32 RootCount = Roots.Num();
			for (uint32 RootIt = 0; RootIt < RootCount; ++RootIt)
			{
				const FHairRoot& Root = Roots[RootIt];
				const FIntVector CellCoord = ToCellCoord(Root.Position);
				const uint32 Index = ToIndex(CellCoord);
				if (GridIndirection[Index] == 0)
				{
					GridIndirection[Index] = RootIndices.Num();
					RootIndices.AddDefaulted();
				}
				
				TArray<int32>& CellGuideIndices = RootIndices[GridIndirection[Index]];
				CellGuideIndices.Add(RootIt);
			}
		}

		FORCEINLINE void SearchCell(
			const FIntVector& CellCoord,
			const uint32 RenCurveIndex,
			const FHairRoot& RenRoot,
			const TArray<FHairRoot>& SimRoots,
			FMetrics& Metrics) const 
		{
			const uint32 CellIndex = ToIndex(CellCoord);

			if (GridIndirection[CellIndex] == 0)
				return;

			const TArray<int32>& Elements = RootIndices[GridIndirection[CellIndex]];

			for (int32 SimCurveIndex : Elements)
			{
				const FHairRoot& GuideRoot = SimRoots[SimCurveIndex];
				{
					ComputeSimpleMetric(Metrics, RenRoot, GuideRoot, RenCurveIndex, SimCurveIndex);
				}
			}
		}

		FClosestGuides FindClosestRoots(
			const uint32 RenCurveIndex,
			const TArray<FHairRoot>& RenRoots,
			const TArray<FHairRoot>& SimRoots,
			const FHairStrandsDatas& RenStrandsData,
			const FHairStrandsDatas& SimStrandsData,
			const bool bRandomized,
			const bool bUnique,
			const FIntVector& RandomIndices) const
		{
			const FHairRoot& RenRoot = RenRoots[RenCurveIndex];
			const FIntVector PointCoord = ToCellCoord(RenRoot.Position);

			FMetrics Metrics;
			for (uint32 ClearIt = 0; ClearIt < FMetrics::Count; ++ClearIt)
			{
				Metrics.KMinMetrics[ClearIt] = FLT_MAX;
				Metrics.KClosestGuideIndices[ClearIt] = -1;
			}

			for (int32 Offset = 1; Offset <= int32(MaxLookupDistance); ++Offset)
			{
				// Center
				{
					bool bIsValid = false;
					const FIntVector CellCoord = ClampToVolume(PointCoord, bIsValid);
					if (bIsValid) SearchCell(PointCoord, RenCurveIndex, RenRoot, SimRoots, Metrics);
				}

				// Top & Bottom
				for (int32 X = -Offset; X <= Offset; ++X)
				for (int32 Y = -Offset; Y <= Offset; ++Y)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector(X, Y, Offset), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(X, Y,-Offset), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, Metrics);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, Metrics);
				}

				const int32 OffsetMinusOne = Offset - 1;
				// Front & Back
				for (int32 X = -Offset; X <= Offset; ++X)
				for (int32 Z = -OffsetMinusOne; Z <= OffsetMinusOne; ++Z)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector(X,  Offset, Z), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(X, -Offset, Z), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, Metrics);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, Metrics);
				}
				
				// Left & Right
				for (int32 Y = -OffsetMinusOne; Y <= OffsetMinusOne; ++Y)
				for (int32 Z = -OffsetMinusOne; Z <= OffsetMinusOne; ++Z)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector( Offset, Y, Z), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(-Offset, Y, Z), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, Metrics);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, Metrics);
				}

				// Early out if we have found all closest guide during a ring/layer step.
				// This early out is not conservative, as the complex metric might find better guides one or multiple step further.
				if (Metrics.KClosestGuideIndices[FMetrics::Count-1] != -1 && Offset >= 2)
				{
					break;
				}
			}

			// If no valid guide have been found, switch to a simpler metric
			FClosestGuides ClosestGuides;
			SelectFinalGuides(ClosestGuides, RandomIndices, Metrics, bRandomized, bUnique);

			check(ClosestGuides.Indices[0] >= 0);
			check(ClosestGuides.Indices[1] >= 0);
			check(ClosestGuides.Indices[2] >= 0);

			return ClosestGuides;
		}


		FORCEINLINE void SearchCell(
			const FIntVector& CellCoord,
			const uint32 RenCurveIndex,
			const FHairRoot& RenRoot,
			const TArray<FHairRoot>& SimRoots,
			const FHairStrandsDatas& RenStrandsData,
			const FHairStrandsDatas& SimStrandsData,
			FMetrics& Metrics0,
			FMetrics& Metrics1) const
		{
			const uint32 CellIndex = ToIndex(CellCoord);

			if (GridIndirection[CellIndex] == 0)
				return;

			const TArray<int32>& Elements = RootIndices[GridIndirection[CellIndex]];

			for (int32 SimCurveIndex : Elements)
			{
				const FHairRoot& GuideRoot = SimRoots[SimCurveIndex];
				{
					ComputeSimpleMetric(Metrics1, RenRoot, GuideRoot, RenCurveIndex, SimCurveIndex);
					ComputeAdvandedMetric(Metrics0, RenStrandsData, SimStrandsData, RenCurveIndex, SimCurveIndex);
				}
			}
		}

		FClosestGuides FindBestClosestRoots(
			const uint32 RenCurveIndex,
			const TArray<FHairRoot>& RenRoots,
			const TArray<FHairRoot>& SimRoots,
			const FHairStrandsDatas& RenStrandsData,
			const FHairStrandsDatas& SimStrandsData,
			const bool bRandomized,
			const bool bUnique,
			const FIntVector& RandomIndices) const
		{
			const FHairRoot& RenRoot = RenRoots[RenCurveIndex];
			const FIntVector PointCoord = ToCellCoord(RenRoot.Position);

			FMetrics Metrics0;
			FMetrics Metrics1;
			for (uint32 ClearIt = 0; ClearIt < FMetrics::Count; ++ClearIt)
			{
				Metrics0.KMinMetrics[ClearIt] = FLT_MAX;
				Metrics0.KClosestGuideIndices[ClearIt] = -1;
				Metrics1.KMinMetrics[ClearIt] = FLT_MAX;
				Metrics1.KClosestGuideIndices[ClearIt] = -1;
			}

			for (int32 Offset = 1; Offset <= int32(MaxLookupDistance); ++Offset)
			{
				// Center
				{
					bool bIsValid = false;
					const FIntVector CellCoord = ClampToVolume(PointCoord, bIsValid);
					if (bIsValid) SearchCell(CellCoord, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
				}

				// Top & Bottom
				for (int32 X = -Offset; X <= Offset; ++X)
				for (int32 Y = -Offset; Y <= Offset; ++Y)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector(X, Y, Offset), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(X, Y,-Offset), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
				}

				const int32 OffsetMinusOne = Offset - 1;
				// Front & Back
				for (int32 X = -Offset; X <= Offset; ++X)
				for (int32 Z = -OffsetMinusOne; Z <= OffsetMinusOne; ++Z)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector(X, Offset, Z), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(X, -Offset, Z), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
				}
				
				// Left & Right
				for (int32 Y = -OffsetMinusOne; Y <= OffsetMinusOne; ++Y)
				for (int32 Z = -OffsetMinusOne; Z <= OffsetMinusOne; ++Z)
				{
					bool bIsValid0 = false, bIsValid1 = false;
					const FIntVector CellCoord0 = ClampToVolume(PointCoord + FIntVector( Offset, Y, Z), bIsValid0);
					const FIntVector CellCoord1 = ClampToVolume(PointCoord + FIntVector(-Offset, Y, Z), bIsValid1);
					if (bIsValid0) SearchCell(CellCoord0, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
					if (bIsValid1) SearchCell(CellCoord1, RenCurveIndex, RenRoot, SimRoots, RenStrandsData, SimStrandsData, Metrics0, Metrics1);
				}

				// Early out if we have found all closest guide during a ring/layer step.
				// This early out is not conservative, as the complex metric might find better guides one or multiple step further.
				if ((Metrics0.KClosestGuideIndices[FMetrics::Count-1] != -1 || Metrics1.KClosestGuideIndices[FMetrics::Count - 1] != -1) && Offset >= 2)
				{
					break;
				}
			}

			// If no valid guide have been found, switch to a simpler metric
			FClosestGuides ClosestGuides;
			if (Metrics0.KClosestGuideIndices[0] != -1)
			{
				SelectFinalGuides(ClosestGuides, RandomIndices, Metrics0, bRandomized, bUnique);
			}
			else
			{
				SelectFinalGuides(ClosestGuides, RandomIndices, Metrics1, bRandomized, bUnique);
			}

			check(ClosestGuides.Indices[0] >= 0);
			check(ClosestGuides.Indices[1] >= 0);
			check(ClosestGuides.Indices[2] >= 0);

			return ClosestGuides;
		}
	};

	static FClosestGuides FindBestRoots(
		const uint32 RenCurveIndex,
		const TArray<FHairRoot>& RenRoots,
		const TArray<FHairRoot>& SimRoots,
		const FHairStrandsDatas& RenStrandsData,
		const FHairStrandsDatas& SimStrandsData,
		const bool bRandomized,
		const bool bUnique,
		const FIntVector& RandomIndices)
	{
		FMetrics Metrics;
		for (uint32 ClearIt = 0; ClearIt < FMetrics::Count; ++ClearIt)
		{
			Metrics.KMinMetrics[ClearIt] = FLT_MAX;
			Metrics.KClosestGuideIndices[ClearIt] = -1;
		}

		const uint32 SimRootsCount = SimRoots.Num();
		for (uint32 SimCurveIndex =0; SimCurveIndex<SimRootsCount; ++SimCurveIndex)
		{
			ComputeAdvandedMetric(Metrics, RenStrandsData, SimStrandsData, RenCurveIndex, SimCurveIndex);
		}
			
		FClosestGuides ClosestGuides;
		SelectFinalGuides(ClosestGuides, RandomIndices, Metrics, bRandomized, bUnique);

		check(ClosestGuides.Indices[0] >= 0);
		check(ClosestGuides.Indices[1] >= 0);
		check(ClosestGuides.Indices[2] >= 0);

		return ClosestGuides;
	}

	// Extract strand roots
	static void ExtractRoots(const FHairStrandsDatas& InData, TArray<FHairRoot>& OutRoots, FVector& MinBound, FVector& MaxBound)
	{
		MinBound = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
		MaxBound = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		const uint32 CurveCount = InData.StrandsCurves.Num();
		OutRoots.Reserve(CurveCount);
		for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
		{
			const uint32 PointOffset = InData.StrandsCurves.CurvesOffset[CurveIndex];
			const uint32 PointCount = InData.StrandsCurves.CurvesCount[CurveIndex];
			const float  CurveLength = InData.StrandsCurves.CurvesLength[CurveIndex] * InData.StrandsCurves.MaxLength;
			check(PointCount > 1);
			const FVector& P0 = InData.StrandsPoints.PointsPosition[PointOffset];
			const FVector& P1 = InData.StrandsPoints.PointsPosition[PointOffset + 1];
			FVector N = (P1 - P0).GetSafeNormal();

			// Fallback in case the initial points are too close (this happens on certain assets)
			if (FVector::DotProduct(N, N) == 0)
			{
				N = FVector(0, 0, 1);
			}
			OutRoots.Add({ P0, PointCount, N, PointOffset, CurveLength });

			MinBound = MinBound.ComponentMin(P0);
			MaxBound = MaxBound.ComponentMax(P0);
		}
	}

	struct FVertexInterpolationDesc
	{
		uint32 Index0 = 0;
		uint32 Index1 = 0;
		float T = 0;
	};

	// Find the vertex along a sim curve 'SimCurveIndex', which has the same parametric distance than the render distance 'RenPointDistance'
	static FVertexInterpolationDesc FindMatchingVertex(const float RenPointDistance, const FHairStrandsDatas& SimStrandsData, const uint32 SimCurveIndex)
	{
		const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];

		const float CurveLength = SimStrandsData.StrandsCurves.CurvesLength[SimCurveIndex] * SimStrandsData.StrandsCurves.MaxLength;

		// Find with with vertex the vertex should be paired
		const uint32 SimPointCount = SimStrandsData.StrandsCurves.CurvesCount[SimCurveIndex];
		for (uint32 SimPointIndex = 0; SimPointIndex < SimPointCount-1; ++SimPointIndex)
		{
			const float SimPointDistance0 = SimStrandsData.StrandsPoints.PointsCoordU[SimPointIndex + SimOffset] * CurveLength;
			const float SimPointDistance1 = SimStrandsData.StrandsPoints.PointsCoordU[SimPointIndex + SimOffset + 1] * CurveLength;
			if (SimPointDistance0 <= RenPointDistance && RenPointDistance <= SimPointDistance1)
			{
				const float SegmentLength = SimPointDistance1 - SimPointDistance0;
				FVertexInterpolationDesc Out;
				Out.Index0 = SimPointIndex;
				Out.Index1 = SimPointIndex+1;
				Out.T = (RenPointDistance - SimPointDistance0) / (SegmentLength>0? SegmentLength : 1);
				Out.T = FMath::Clamp(Out.T, 0.f, 1.f);
				return Out;
			}
		}
		FVertexInterpolationDesc Desc;
		Desc.Index0 = SimPointCount-2;
		Desc.Index1 = SimPointCount-1;
		Desc.T = 1;
		return Desc;
	}

	static void BuildInterpolationData(
		FHairStrandsInterpolationDatas& InterpolationData,
		const FHairStrandsDatas& SimStrandsData,
		const FHairStrandsDatas& RenStrandsData,
		const FHairInterpolationSettings& Settings,
		const TArray<FIntVector>& RandomGuideIndices)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairInterpolationBuilder::BuildInterpolationData);

		InterpolationData.SetNum(RenStrandsData.GetNumPoints());

		typedef TArray<FHairRoot> FRoots;

		// Build acceleration structure for fast nearest-neighbors lookup.
		// This is used only for low quality interpolation as high quality 
		// interpolation require broader search
		FRoots RenRoots, SimRoots;
		FRootsGrid RootsGrid;
		{
			FVector RenMinBound, RenMaxBound;
			FVector SimMinBound, SimMaxBound;
			ExtractRoots(RenStrandsData, RenRoots, RenMinBound, RenMaxBound);
			ExtractRoots(SimStrandsData, SimRoots, SimMinBound, SimMaxBound);

			if (Settings.InterpolationQuality == EHairInterpolationQuality::Low || Settings.InterpolationQuality == EHairInterpolationQuality::Medium)
			{
				// Build a conservative bound, to insure all queries will fall 
				// into the grid volume.
				const FVector MinBound = RenMinBound.ComponentMin(SimMinBound);
				const FVector MaxBound = RenMaxBound.ComponentMax(SimMaxBound);
				RootsGrid.InsertRoots(SimRoots, MinBound, MaxBound);
			}
		}

		// Find k-closest guide:
		uint32 TotalInvalidInterpolationCount = 0;
		const static float MinWeightDistance = 0.0001f;

		const uint32 RenCurveCount = RenStrandsData.GetNumCurves();
		const uint32 SimCurveCount = SimStrandsData.GetNumCurves();

		TAtomic<uint32> CompletedTasks(0);
		FScopedSlowTask SlowTask(RenCurveCount, LOCTEXT("BuildInterpolationData", "Building groom simulation data"));
		SlowTask.MakeDialog();

		const FDateTime StartTime = FDateTime::UtcNow();
		ParallelFor(RenCurveCount, 
		[
			StartTime,
			Settings,
			RenCurveCount, &RenRoots, &RenStrandsData,
			SimCurveCount, &SimRoots, &SimStrandsData, 
			&RootsGrid,
			&TotalInvalidInterpolationCount,  
			&InterpolationData, 
			&RandomGuideIndices,
			&CompletedTasks,
			&SlowTask
		] (uint32 RenCurveIndex) 
		//for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HairInterpolationBuilder::ComputingClosestGuidesAndWeights);

			++CompletedTasks;

			if (IsInGameThread())
			{
				const uint32 CurrentCompletedTasks = CompletedTasks.Exchange(0);
				const float RemainingTasks = FMath::Clamp(SlowTask.TotalAmountOfWork - SlowTask.CompletedWork, 1.f, SlowTask.TotalAmountOfWork);
				const FTimespan ElaspedTime = FDateTime::UtcNow() - StartTime;
				const double RemainingTimeInSeconds = RemainingTasks * double(ElaspedTime.GetSeconds() / SlowTask.CompletedWork);

				FTextBuilder TextBuilder;
				TextBuilder.AppendLineFormat(LOCTEXT("ComputeGuidesAndWeights", "Computing closest guides and weights ({0})"), FText::AsTimespan(FTimespan::FromSeconds(RemainingTimeInSeconds)));
				SlowTask.EnterProgressFrame(CurrentCompletedTasks, TextBuilder.ToText());
			}

			const FHairRoot& StrandRoot = RenRoots[RenCurveIndex];

			FClosestGuides ClosestGuides;
			if (Settings.InterpolationQuality == EHairInterpolationQuality::Low)
			{
				ClosestGuides = RootsGrid.FindClosestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizeGuide, Settings.bUseUniqueGuide, RandomGuideIndices[RenCurveIndex]);
			}
			else if (Settings.InterpolationQuality == EHairInterpolationQuality::Medium)
			{
				ClosestGuides = RootsGrid.FindBestClosestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizeGuide, Settings.bUseUniqueGuide, RandomGuideIndices[RenCurveIndex]);
			}
			else // (Settings.Quality == EHairInterpolationQuality::High)
			{
				ClosestGuides = FindBestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizeGuide, Settings.bUseUniqueGuide, RandomGuideIndices[RenCurveIndex]);
			}

			const uint32 RendPointCount	= RenStrandsData.StrandsCurves.CurvesCount[RenCurveIndex];
			const uint32 RenOffset		= RenStrandsData.StrandsCurves.CurvesOffset[RenCurveIndex];
			const FVector& RenPointPositionRoot = RenStrandsData.StrandsPoints.PointsPosition[RenOffset];
			for (uint32 RenPointIndex = 0; RenPointIndex < RendPointCount; ++RenPointIndex)
			{
				const uint32 PointGlobalIndex = RenPointIndex + RenOffset;
				const FVector& RenPointPosition = RenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
				const float RenPointDistance = RenStrandsData.StrandsPoints.PointsCoordU[PointGlobalIndex] * RenStrandsData.StrandsCurves.CurvesLength[RenCurveIndex] * RenStrandsData.StrandsCurves.MaxLength;

				float TotalWeight = 0;
				for (uint32 KIndex = 0; KIndex < FClosestGuides::Count; ++KIndex)
				{
					// Find the closest vertex on the guide which matches the strand vertex distance along its curve
					if (Settings.InterpolationDistance == EHairInterpolationWeight::Parametric)
					{
						const uint32 SimCurveIndex = ClosestGuides.Indices[KIndex];
						const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];
						const FVertexInterpolationDesc Desc = FindMatchingVertex(RenPointDistance, SimStrandsData, SimCurveIndex);
						const FVector& SimPointPosition0 = SimStrandsData.StrandsPoints.PointsPosition[Desc.Index0 + SimOffset];
						const FVector& SimPointPosition1 = SimStrandsData.StrandsPoints.PointsPosition[Desc.Index1 + SimOffset];
						const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenPointPosition, FMath::Lerp(SimPointPosition0, SimPointPosition1, Desc.T)));

						InterpolationData.PointsSimCurvesIndex[PointGlobalIndex][KIndex] = SimCurveIndex;
						InterpolationData.PointsSimCurvesVertexIndex[PointGlobalIndex][KIndex] = Desc.Index0 + SimOffset;
						InterpolationData.PointsSimCurvesVertexLerp[PointGlobalIndex][KIndex] = Desc.T;
						InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][KIndex] = Weight;
					}

					// Use only the root as a *constant* weight for deformation along each vertex
					// Still compute the closest vertex (in parametric distance) to know on which vertex the offset/delta should be computed
					if (Settings.InterpolationDistance == EHairInterpolationWeight::Root)
					{
						const uint32 SimCurveIndex = ClosestGuides.Indices[KIndex];
						const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];
						const FVector& SimRootPointPosition = SimStrandsData.StrandsPoints.PointsPosition[SimOffset];
						const FVector& RenRootPointPosition = RenStrandsData.StrandsPoints.PointsPosition[RenOffset];
						const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenRootPointPosition, SimRootPointPosition));
						const FVertexInterpolationDesc Desc = FindMatchingVertex(RenPointDistance, SimStrandsData, SimCurveIndex);

						InterpolationData.PointsSimCurvesIndex[PointGlobalIndex][KIndex] = SimCurveIndex;
						InterpolationData.PointsSimCurvesVertexIndex[PointGlobalIndex][KIndex] = Desc.Index0 + SimOffset;
						InterpolationData.PointsSimCurvesVertexLerp[PointGlobalIndex][KIndex] = Desc.T;
						InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][KIndex] = Weight;
					}

					// Use the *same vertex index* to match guide vertex with strand vertex
					if (Settings.InterpolationDistance == EHairInterpolationWeight::Index)
					{
						const uint32 SimCurveIndex = ClosestGuides.Indices[KIndex];
						const uint32 SimOffset = SimStrandsData.StrandsCurves.CurvesOffset[SimCurveIndex];
						const uint32 SimPointCount = SimStrandsData.StrandsCurves.CurvesCount[SimCurveIndex];
						const uint32 SimPointIndex = FMath::Clamp(RenPointIndex, 0u, SimPointCount - 1);
						const FVector& SimPointPosition = SimStrandsData.StrandsPoints.PointsPosition[SimPointIndex + SimOffset];
						const float Weight = 1.0f / FMath::Max(MinWeightDistance, FVector::Distance(RenPointPosition, SimPointPosition));

						InterpolationData.PointsSimCurvesIndex[PointGlobalIndex][KIndex] = SimCurveIndex;
						InterpolationData.PointsSimCurvesVertexIndex[PointGlobalIndex][KIndex] = SimPointIndex + SimOffset;
						InterpolationData.PointsSimCurvesVertexLerp[PointGlobalIndex][KIndex] = 1;
						InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][KIndex] = Weight;
					}

					TotalWeight += InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][KIndex];
				}

				for (int32 KIndex = 0; KIndex < FClosestGuides::Count; ++KIndex)
				{
					InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][KIndex] /= TotalWeight;
				}
			}
		});
	}

	/** Build data for interpolation between simulation and rendering */
	void BuildRenderData(FHairStrandsInterpolationDatas& HairInterpolation)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairInterpolationBuilder::BuildRenderData);

		const uint32 PointCount = HairInterpolation.Num();
		if (PointCount == 0)
			return;

		auto LowerPart = [](uint32 Index) { return uint16(Index & 0xFFFF); };
		auto UpperPart = [](uint32 Index) { return uint8((Index >> 16) & 0xFF); };

		TArray<FHairStrandsInterpolation0Format::Type>& OutPointsInterpolation0 = HairInterpolation.RenderData.Interpolation0;
		TArray<FHairStrandsInterpolation1Format::Type>& OutPointsInterpolation1 = HairInterpolation.RenderData.Interpolation1;

		OutPointsInterpolation0.SetNum(PointCount * FHairStrandsInterpolation0Format::ComponentCount);
		OutPointsInterpolation1.SetNum(PointCount * FHairStrandsInterpolation1Format::ComponentCount);

		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			const FIntVector& Indices = HairInterpolation.PointsSimCurvesVertexIndex[PointIndex];
			const FVector& Weights = HairInterpolation.PointsSimCurvesVertexWeights[PointIndex];
			const FVector& S = HairInterpolation.PointsSimCurvesVertexLerp[PointIndex];

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
			OutInterp1.VertexLerp0  = S[0] * 255.f;
			OutInterp1.VertexLerp1  = S[1] * 255.f;
			OutInterp1.VertexLerp2  = S[2] * 255.f;
			OutInterp1.Pad0			= 0;
			OutInterp1.Pad1			= 0;
		}	
	}

	/** Fill the GroomAsset with the interpolation data that exists in the HairDescription */
	void FillInterpolationData(UGroomAsset* GroomAsset, const FHairDescription& HairDescription)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairInterpolationBuilder::FillInterpolationData);

		TStrandAttributesConstRef<FVector> ClosestGuides = HairDescription.StrandAttributes().GetAttributesRef<FVector>(HairAttribute::Strand::ClosestGuides);
		TStrandAttributesConstRef<FVector> GuideWeights = HairDescription.StrandAttributes().GetAttributesRef<FVector>(HairAttribute::Strand::GuideWeights);

		if (!GroomAsset || !ClosestGuides.IsValid() || !GuideWeights.IsValid())
		{
			return;
		}

		const int32 NumGroups = GroomAsset->HairGroupsData.Num();
		for (int32 GroupID = 0; GroupID < NumGroups; ++GroupID)
		{
			FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupID];

			const FHairStrandsDatas& RenderData = GroupData.Strands.Data;
			const FHairStrandsDatas& SimulationData = GroupData.Guides.Data;
			FHairStrandsInterpolationDatas& InterpolationData = GroupData.Strands.InterpolationData;

			// Group must include imported guides data for imported interpolation data to be meaningful
			// Otherwise, interpolation data will be built from generated guide data
			if (SimulationData.GetNumCurves() == 0)
			{
				continue;
			}

			InterpolationData.SetNum(RenderData.GetNumPoints());
			for (uint32 CurveIndex = 0; CurveIndex < RenderData.GetNumCurves(); ++CurveIndex)
			{
				const uint32 CurveOffset = RenderData.StrandsCurves.CurvesOffset[CurveIndex];
				const uint16 CurveNumVertices = RenderData.StrandsCurves.CurvesCount[CurveIndex];

				const FStrandID StrandID(RenderData.StrandsCurves.StrandIDs[CurveIndex]);
				const FVector StrandClosestGuides = ClosestGuides[StrandID];
				const FVector StrandGuideWeights = GuideWeights[StrandID];
				for (uint16 VertexIndex = 0; VertexIndex < CurveNumVertices; ++VertexIndex)
				{
					const uint32 PointGlobalIndex = VertexIndex + CurveOffset;
					const FVector& RenPointPosition = RenderData.StrandsPoints.PointsPosition[PointGlobalIndex];
					const float RenPointDistance = RenderData.StrandsPoints.PointsCoordU[PointGlobalIndex] * RenderData.StrandsCurves.CurvesLength[CurveIndex] * RenderData.StrandsCurves.MaxLength;

					float TotalWeight = 0;
					for (uint32 GuideIndex = 0; GuideIndex < FClosestGuides::Count; ++GuideIndex)
					{
						int32 ImportedGroomID = StrandClosestGuides[GuideIndex];
						const int* SimCurveIndex = SimulationData.StrandsCurves.GroomIDToIndex.Find(ImportedGroomID);

						if (SimCurveIndex && *SimCurveIndex >= 0)
						{
							// Fill the interpolation data using the ParametricDistance algorithm with a constant weight for all vertices along the strand
							const uint32 SimOffset = SimulationData.StrandsCurves.CurvesOffset[*SimCurveIndex];
							const FVertexInterpolationDesc Desc = FindMatchingVertex(RenPointDistance, SimulationData, *SimCurveIndex);

							InterpolationData.PointsSimCurvesIndex[PointGlobalIndex][GuideIndex] = *SimCurveIndex;
							InterpolationData.PointsSimCurvesVertexIndex[PointGlobalIndex][GuideIndex] = Desc.Index0 + SimOffset;
							InterpolationData.PointsSimCurvesVertexLerp[PointGlobalIndex][GuideIndex] = Desc.T;
							InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][GuideIndex] = StrandGuideWeights[GuideIndex];
							TotalWeight += InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][GuideIndex];
						}
					}

					// Normalize the weights
					if (TotalWeight > 0.f)
					{
						for (int32 GuideIndex = 0; GuideIndex < FClosestGuides::Count; ++GuideIndex)
						{
							InterpolationData.PointsSimCurvesVertexWeights[PointGlobalIndex][GuideIndex] /= TotalWeight;
						}
					}
				}
			}
		}

		// Deallocate the memory used for indices mapping since it's not needed anymore
		for (int32 GroupID = 0; GroupID < NumGroups; ++GroupID)
		{
			FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupID];

			FHairStrandsDatas& RenderData = GroupData.Strands.Data;
			RenderData.StrandsCurves.GroomIDToIndex.Empty();
			RenderData.StrandsCurves.StrandIDs.Empty();

			FHairStrandsDatas& SimulationData = GroupData.Guides.Data;
			SimulationData.StrandsCurves.GroomIDToIndex.Empty();
			SimulationData.StrandsCurves.StrandIDs.Empty();
		}
	}
}

class FGroomDataRandomizer
{
public:
	FGroomDataRandomizer(int Seed, int NumRenderCurves, int NumSimCurves)
	{
		Random.Initialize(Seed);

		RenderCurveSeeds.SetNumUninitialized(NumRenderCurves);
		for (int Index = 0; Index < NumRenderCurves; ++Index)
		{
			RenderCurveSeeds[Index] = Random.RandHelper(255);
		}

		SimCurveSeeds.SetNumUninitialized(NumSimCurves);
		for (int Index = 0; Index < NumSimCurves; ++Index)
		{
			SimCurveSeeds[Index] = Random.RandHelper(255);
		}

		// This randomization makes certain strands being affected by 1, 2, or 3 guides
		GuideIndices.SetNumUninitialized(NumRenderCurves);
		for (int Index = 0; Index < NumRenderCurves; ++Index)
		{
			FIntVector& RandomIndices = GuideIndices[Index];
			for (int GuideIndex = 0; GuideIndex < HairInterpolationBuilder::FMetrics::Count; ++GuideIndex)
			{
				RandomIndices[GuideIndex] = Random.RandRange(0, HairInterpolationBuilder::FMetrics::Count - 1);
			}
		}
	}

	const TArray<uint8>& GetRenderCurveSeeds() const { return RenderCurveSeeds; }
	const TArray<uint8>& GetSimCurveSeeds() const { return SimCurveSeeds; }
	const TArray<FIntVector>& GetRandomGuideIndices() const { return GuideIndices; }

private:
	FRandomStream Random;
	TArray<uint8> RenderCurveSeeds;
	TArray<uint8> SimCurveSeeds;
	TArray<FIntVector> GuideIndices;
};

bool FGroomBuilder::ProcessHairDescription(const FHairDescription& HairDescription, FProcessedHairDescription& Out)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::ProcessHairDescription);

	// Convert HairDescription to HairStrandsDatas
	// For now, just convert HairDescription to HairStrandsDatas
	int32 NumCurves = HairDescription.GetNumStrands();
	int32 NumVertices = HairDescription.GetNumVertices();

	// Check for required attributes
	TGroomAttributesConstRef<int> MajorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MajorVersion);
	TGroomAttributesConstRef<int> MinorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MinorVersion);

	// Major/Minor version check is currently disabled as this is not used at the moment and create false positive
	#if 0
	if (!MajorVersion.IsValid() || !MinorVersion.IsValid())
	{
		UE_LOG(LogGroomBuilder, Warning, TEXT("No version number attributes found. The groom may be missing attributes even if it imports."));
	}
	#endif

	FGroomID GroomID(0);

	TGroomAttributesConstRef<float> GroomHairWidthAttribute = HairDescription.GroomAttributes().GetAttributesRef<float>(HairAttribute::Groom::Width);
	TOptional<float> GroomHairWidth;
	if (GroomHairWidthAttribute.IsValid())
	{
		GroomHairWidth = GroomHairWidthAttribute[GroomID];
	}

	TGroomAttributesConstRef<FVector> GroomHairColorAttribute = HairDescription.GroomAttributes().GetAttributesRef<FVector>(HairAttribute::Groom::Color);
	TOptional<FVector> GroomHairColor;
	if (GroomHairColorAttribute.IsValid())
	{
		GroomHairColor = GroomHairColorAttribute[GroomID];
	}

	TVertexAttributesConstRef<FVector> VertexPositions	= HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Position);
	TVertexAttributesConstRef<FVector> VertexBaseColor	= HairDescription.VertexAttributes().GetAttributesRef<FVector>(HairAttribute::Vertex::Color);
	TStrandAttributesConstRef<int> StrandNumVertices	= HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::VertexCount);

	if (!VertexPositions.IsValid() || !StrandNumVertices.IsValid())
	{
		UE_LOG(LogGroomBuilder, Warning, TEXT("Failed to import hair: No vertices or curves data found."));
		return false;
	}

	const bool bHasBaseColorAttribute = VertexBaseColor.IsValid();

	TVertexAttributesConstRef<float> VertexWidths = HairDescription.VertexAttributes().GetAttributesRef<float>(HairAttribute::Vertex::Width);
	TStrandAttributesConstRef<float> StrandWidths = HairDescription.StrandAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);

	TStrandAttributesConstRef<FVector2D> StrandRootUV = HairDescription.StrandAttributes().GetAttributesRef<FVector2D>(HairAttribute::Strand::RootUV);
	Out.bHasUVData = StrandRootUV.IsValid();

	TStrandAttributesConstRef<int> StrandGuides = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::Guide);
	TStrandAttributesConstRef<int> GroupIDs = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::GroupID);
	TStrandAttributesConstRef<int> StrandIDs = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::ID);

	bool bImportGuides = true;

	TStrandAttributesConstRef<FVector> ClosestGuides = HairDescription.StrandAttributes().GetAttributesRef<FVector>(HairAttribute::Strand::ClosestGuides);
	TStrandAttributesConstRef<FVector> GuideWeights = HairDescription.StrandAttributes().GetAttributesRef<FVector>(HairAttribute::Strand::GuideWeights);

	// To use ClosestGuides and GuideWeights attributes, guides must be imported from HairDescription and
	// must include StrandID attribute since ClosestGuides references those IDs
	Out.bCanUseClosestGuidesAndWeights = bImportGuides && StrandIDs.IsValid() && ClosestGuides.IsValid() && GuideWeights.IsValid();

	int32 GlobalVertexIndex = 0;
	int32 NumHairPoints = 0;
	int32 NumGuidePoints = 0;
	for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
	{
		FStrandID StrandID(CurveIndex);

		bool bIsGuide = false;
		if (StrandGuides.IsValid())
		{
			// Version 0.1 defines 1 as being guide
			bIsGuide = StrandGuides[StrandID] == 1;
		}

		int32 CurveNumVertices = StrandNumVertices[StrandID];

		if (CurveNumVertices <= 0)
		{
			continue;
		}

		int32 GroupID = 0;
		if (GroupIDs.IsValid())
		{
			GroupID = GroupIDs[StrandID];
		}

		FHairStrandsDatas* CurrentHairStrandsDatas = nullptr;
		FProcessedHairDescription::FHairGroup& Group = Out.HairGroups.FindOrAdd(GroupID);
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;
		GroupInfo.GroupID = GroupID;
		if (!bIsGuide)
		{
			NumHairPoints += CurveNumVertices;
			CurrentHairStrandsDatas = &GroupData.Strands.Data;

			++GroupInfo.NumCurves;
		}
		else if (bImportGuides)
		{
			NumGuidePoints += CurveNumVertices;
			CurrentHairStrandsDatas = &GroupData.Guides.Data;

			++GroupInfo.NumGuides;
		}
		else
		{
			// A guide but don't want to import it, so skip it
			GlobalVertexIndex += CurveNumVertices;
			continue;
		}

		if (!CurrentHairStrandsDatas)
			continue;

		CurrentHairStrandsDatas->StrandsCurves.CurvesCount.Add(CurveNumVertices);

		if (Out.bCanUseClosestGuidesAndWeights)
		{
			// ClosesGuides needs mapping of StrandID (the strand index in the HairDescription)
			CurrentHairStrandsDatas->StrandsCurves.StrandIDs.Add(CurveIndex);

			// and ImportedGroomID (the imported ID associated with a strand) to index in StrandCurves
			const int ImportedGroomID = StrandIDs[StrandID];
			const int StrandCurveIndex = CurrentHairStrandsDatas->StrandsCurves.CurvesCount.Num() - 1;
			CurrentHairStrandsDatas->StrandsCurves.GroomIDToIndex.Add(ImportedGroomID, StrandCurveIndex);
		}

		if (Out.bHasUVData)
		{
			CurrentHairStrandsDatas->StrandsCurves.CurvesRootUV.Add(StrandRootUV[StrandID]);
		}

		float StrandWidth = GroomHairWidth ? GroomHairWidth.GetValue() : 0.01f;
		if (StrandWidths.IsValid())
		{
			StrandWidth = StrandWidths[StrandID];
		}

		for (int32 VertexIndex = 0; VertexIndex < CurveNumVertices; ++VertexIndex, ++GlobalVertexIndex)
		{
			FVertexID VertexID(GlobalVertexIndex);

			CurrentHairStrandsDatas->StrandsPoints.PointsPosition.Add(VertexPositions[VertexID]);
			CurrentHairStrandsDatas->StrandsPoints.PointsBaseColor.Add(bHasBaseColorAttribute ? VertexBaseColor[VertexID] : FLinearColor::Black);
			CurrentHairStrandsDatas->StrandsPoints.PointsRoughness.Add(0); // @hair_todo: add attribute read on the alembic for reading roughness per groom/strands/vertex

			float VertexWidth = 0.f;
			if (VertexWidths.IsValid())
			{
				VertexWidth = VertexWidths[VertexID];
			}
			else if (StrandWidth != 0.f)
			{
				// Fall back to strand width if there was no vertex width
				VertexWidth = StrandWidth;
			}

			CurrentHairStrandsDatas->StrandsPoints.PointsRadius.Add(VertexWidth * 0.5f);
		}
	}
	// Sparse->Dense Groups
	FProcessedHairDescription::FHairGroups CompactGroups;
	int32 GroupIndex = 0;
	for (auto& HairGroupIt : Out.HairGroups)
	{
		FProcessedHairDescription::FHairGroup& Group = CompactGroups.FindOrAdd(GroupIndex);
		Group = HairGroupIt.Value;

		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;

		GroupInfo.GroupID = GroupIndex;

		++GroupIndex;
	}
	Out.HairGroups = CompactGroups;
	return true;
}

bool FGroomBuilder::BuildGroom(const FHairDescription& HairDescription, UGroomAsset* GroomAsset)
{
	if (!GroomAsset)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildGroom);

	FProcessedHairDescription ProcessedHairDescription;
	if (!ProcessHairDescription(HairDescription, ProcessedHairDescription))
	{
		return false;
	}

	const float GroomBoundRadius = FGroomBuilder::ComputeGroomBoundRadius(ProcessedHairDescription);
	const uint32 GroupCount = ProcessedHairDescription.HairGroups.Num();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		bool bSucceed = BuildGroom(ProcessedHairDescription, GroomAsset, GroupIndex);
		if (!bSucceed)
		{
			return false;
		}
		FGroomBuilder::BuildClusterData(GroomAsset, GroomBoundRadius, GroupIndex);
	}

	return true;
}

void FGroomBuilder::BuildHairGroupData(FProcessedHairDescription& ProcessedHairDescription, const FHairGroupsInterpolation& InSettings, uint32 GroupIndex, FHairGroupData& OutHairGroupData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildHairGroupData);

	const uint32 GroupCount = ProcessedHairDescription.HairGroups.Num();
	check(GroupIndex < GroupCount);

	// Sanitize decimation values. Do not update the 'InSettings' values directly as this would change 
	// the groom asset and thus would change the DDC key
	const float CurveDecimation		= FMath::Clamp(InSettings.DecimationSettings.CurveDecimation, 0.f, 1.f);
	const float VertexDecimation	= FMath::Clamp(InSettings.DecimationSettings.VertexDecimation, 0.f, 1.f);
	const float HairToGuideDensity	= FMath::Clamp(InSettings.InterpolationSettings.HairToGuideDensity, 0.f, 1.f);	

	for (TPair<int32, FProcessedHairDescription::FHairGroup>& HairGroupIt : ProcessedHairDescription.HairGroups)
	{
		int32 GroupID = HairGroupIt.Key;
		if (GroupIndex != GroupID)
		{
			continue;
		}

		FProcessedHairDescription::FHairGroup& Group = HairGroupIt.Value;
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;

		// Rendering data
		{
			FHairStrandsDatas& RenData = GroupData.Strands.Data;

			const int32 RenGroupNumCurves = RenData.StrandsCurves.Num();
			RenData.StrandsCurves.SetNum(RenGroupNumCurves);
			GroupInfo.NumCurves = RenGroupNumCurves;

			const int32 RenGroupNumPoints = RenData.StrandsPoints.Num();
			RenData.StrandsPoints.SetNum(RenGroupNumPoints);

			HairStrandsBuilder::BuildInternalData(RenData, !ProcessedHairDescription.bHasUVData);

			// Decimate
			if (CurveDecimation < 1 || VertexDecimation < 1)
			{
				FHairStrandsDatas FullData = RenData;
				RenData.Reset();
				Decimate(FullData, CurveDecimation, VertexDecimation, RenData);
			}
		}

		// Simulation data
		{
			FHairStrandsDatas& SimData = GroupData.Guides.Data;
			const int32 SimGroupNumGuides = SimData.StrandsCurves.Num();

			if (SimGroupNumGuides > 0 && !InSettings.InterpolationSettings.bOverrideGuides)
			{
				GroupInfo.NumGuides = SimGroupNumGuides;
				SimData.StrandsCurves.SetNum(SimGroupNumGuides);

				const int32 GroupNumPoints = SimData.StrandsPoints.Num();
				SimData.StrandsPoints.SetNum(GroupNumPoints);

				HairStrandsBuilder::BuildInternalData(SimData, true); // Imported guides don't currently have root UVs so force computing them
			}
			else
			{
				SimData.Reset();
				Decimate(GroupData.Strands.Data, HairToGuideDensity, 1, SimData);
			}
		}
	}

	for (TPair<int32, FProcessedHairDescription::FHairGroup> HairGroupIt : ProcessedHairDescription.HairGroups)
	{
		int32 GroupID = HairGroupIt.Key;
		if (GroupIndex != GroupID)
		{
			continue;
		}

		FProcessedHairDescription::FHairGroup& Group = HairGroupIt.Value;
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;
		OutHairGroupData = MoveTemp(GroupData);
	}

	// If there's usable closest guides and guide weights attributes, fill them into the asset
	// This step requires the HairSimulationData (guides) to be filled prior to this
	// TODO
	//if (ProcessedHairDescription.bCanUseClosestGuidesAndWeights)
	//{
	//	HairInterpolationBuilder::FillInterpolationData(GroomAsset, HairDescription);
	//}
}

bool FGroomBuilder::BuildGroom(FProcessedHairDescription& ProcessedHairDescription, UGroomAsset* GroomAsset, uint32 GroupIndex)
{
	if (!GroomAsset)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildGroom);

	const uint32 GroupCount = ProcessedHairDescription.HairGroups.Num();
	check(GroupIndex < GroupCount);
	check(uint32(GroomAsset->GetNumHairGroups()) == GroupCount);

	const FHairGroupsInterpolation& InSettings = GroomAsset->HairGroupsInterpolation[GroupIndex];
	FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIndex];

	BuildHairGroupData(ProcessedHairDescription, InSettings, GroupIndex, GroupData);
	BuildData(GroupData, InSettings, GroupIndex);

	return true;
}

void FGroomBuilder::BuildData(UGroomAsset* GroomAsset)
{
	if (!GroomAsset)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildData);

	for (int32 Index = 0, GroupCount = GroomAsset->GetNumHairGroups(); Index < GroupCount; ++Index)
	{
		FHairGroupsInterpolation& IntepolationSettings = GroomAsset->HairGroupsInterpolation[Index];
		FHairGroupData& GroupData = GroomAsset->HairGroupsData[Index];
		BuildData(GroupData, IntepolationSettings, Index);
	}
}

void FGroomBuilder::BuildData(
	FHairStrandsDatas& RenData,
	FHairStrandsDatas& SimData,
	FHairStrandsInterpolationDatas& InterpolationData,
	const FHairInterpolationSettings& InterpolationSettings,
	const bool bBuildRen,
	const bool bBuildSim,
	const bool bBuildInterpolation,
	uint32 Seed)
{
	// Sanity check
	check(RenData.GetNumCurves() > 0);
	check(SimData.GetNumCurves() > 0);

	// Precompute the random values used to build the data
	// In particular, BuildInterpolationData needs this because it parallelizes its computation
	FGroomDataRandomizer Random(Seed, RenData.GetNumCurves(), SimData.GetNumCurves());

	// Build RenderData for HairStrandsDatas
	if (bBuildRen) { HairStrandsBuilder::BuildRenderData(RenData, Random.GetRenderCurveSeeds()); }
	if (bBuildSim) { HairStrandsBuilder::BuildRenderData(SimData, Random.GetSimCurveSeeds()); }

	// Build Rendering data for InterpolationData
	if (bBuildInterpolation)
	{
		// Build InterpolationData from render and simulation HairStrandsDatas
		// Skip building if interpolation data was provided by the source file
		if (InterpolationData.Num() == 0)
		{
			HairInterpolationBuilder::BuildInterpolationData(InterpolationData, SimData, RenData, InterpolationSettings, Random.GetRandomGuideIndices());
		}
		HairInterpolationBuilder::BuildRenderData(InterpolationData);
	}
}

void FGroomBuilder::BuildData(FHairGroupData& GroupData, const FHairGroupsInterpolation& InSettings, uint32 GroupIndex)
{
	BuildData(
		GroupData.Strands.Data,
		GroupData.Guides.Data,
		GroupData.Strands.InterpolationData,
		InSettings.InterpolationSettings, true, true, true, GroupIndex);
}

inline uint32 DecimatePointCount(uint32 InCount, float InDecimationFactor)
{
	return FMath::Clamp(uint32(FMath::CeilToInt(InCount * InDecimationFactor)), 2u, InCount);
}

static void DecimateCurve(
	const TArray<FVector>& InPoints, 
	const uint32 InOffset, 
	const uint32 InCount, 
	const float InDecimationFactor, 
	TArray<uint32>& OutIndices)
{
	check(InCount > 2);
	const int32 OutCount = DecimatePointCount(InCount, InDecimationFactor);

	OutIndices.SetNum(InCount);
	for (uint32 CurveIt = 0; CurveIt < InCount; ++CurveIt)
	{
		OutIndices[CurveIt] = CurveIt;
	}

	while (OutIndices.Num() > OutCount)
	{
		float MinError = FLT_MAX;
		uint32 ElementToRemove = 0;
		const uint32 Count = OutIndices.Num();
		for (uint32 IndexIt = 1; IndexIt < Count - 1; ++IndexIt)
		{
			const FVector& P0 = InPoints[InOffset + OutIndices[IndexIt - 1]];
			const FVector& P1 = InPoints[InOffset + OutIndices[IndexIt]];
			const FVector& P2 = InPoints[InOffset + OutIndices[IndexIt + 1]];

			const float Area = FVector::CrossProduct(P0 - P1, P2 - P1).Size() * 0.5f;

			if (Area < MinError)
			{
				MinError = Area;
				ElementToRemove = IndexIt;
			}
		}
		OutIndices.RemoveAt(ElementToRemove);
	}
}

void FGroomBuilder::Decimate(
	const FHairStrandsDatas& InData, 
	float CurveDecimationPercentage, 
	float VertexDecimationPercentage, 
	FHairStrandsDatas& OutData)
{
	CurveDecimationPercentage = FMath::Clamp(CurveDecimationPercentage, 0.f, 1.f);
	VertexDecimationPercentage = FMath::Clamp(VertexDecimationPercentage, 0.f, 1.f);

	// Pick randomly strand as guide 
	// Divide strands in buckets and pick randomly one stand per bucket
	const uint32 InCurveCount = InData.StrandsCurves.Num();
	const uint32 OutCurveCount = FMath::Clamp(uint32(InCurveCount * CurveDecimationPercentage), 1u, InCurveCount);

	TArray<uint32> CurveIndices;
	CurveIndices.SetNum(OutCurveCount);

	uint32 OutTotalPointCount = 0;
	FRandomStream Random;

	const float CurveBucketSize = float(InCurveCount) / float(OutCurveCount);
	int32 LastCurveIndex = -1;
	for (uint32 BucketIndex = 0; BucketIndex < OutCurveCount; BucketIndex++)
	{
		const float MinBucket = FMath::Max(BucketIndex  * CurveBucketSize, float(LastCurveIndex+1));
		const float MaxBucket = (BucketIndex+1) * CurveBucketSize;
		const float AdjustedBucketSize = MaxBucket - MinBucket;
		if (AdjustedBucketSize > 0)
		{
			const uint32 CurveIndex = FMath::FloorToInt(MinBucket + Random.FRand() * AdjustedBucketSize);
			CurveIndices[BucketIndex] = CurveIndex;
			LastCurveIndex = CurveIndex;

			const uint32 InPointCount = InData.StrandsCurves.CurvesCount[CurveIndex];
			const uint32 OutPointCount = DecimatePointCount(InPointCount, VertexDecimationPercentage);
			OutTotalPointCount += OutPointCount;
		}
	}

	OutData.StrandsCurves.SetNum(OutCurveCount);
	OutData.StrandsPoints.SetNum(OutTotalPointCount);
	OutData.HairDensity = InData.HairDensity;

	uint32 OutPointOffset = 0; 
	for (uint32 OutCurveIndex = 0; OutCurveIndex < OutCurveCount; ++OutCurveIndex)
	{
		const uint32 InCurveIndex	= CurveIndices[OutCurveIndex];
		const uint32 InPointOffset	= InData.StrandsCurves.CurvesOffset[InCurveIndex];
		const uint32 InPointCount	= InData.StrandsCurves.CurvesCount[InCurveIndex];

		// Decimation using area metric
	#if 1
		TArray<uint32> OutPointIndices;
		// Don't need to DecimateCurve if there are only 2 control vertices
		if (InPointCount > 2)
		{
			DecimateCurve(
				InData.StrandsPoints.PointsPosition,
				InPointOffset,
				InPointCount,
				VertexDecimationPercentage,
				OutPointIndices);
		}
		else
		{
			// Just pass the start and end point of the curve, no decimation needed
			OutPointIndices.Add(0);
			OutPointIndices.Add(1);
		}

		const uint32 OutPointCount = OutPointIndices.Num();

		for (uint32 OutPointIndex = 0; OutPointIndex < OutPointCount; ++OutPointIndex)
		{
			const uint32 InPointIndex = OutPointIndices[OutPointIndex];

			OutData.StrandsPoints.PointsPosition [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsPosition	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsCoordU	 [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsCoordU	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsRadius	 [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsRadius	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsBaseColor[OutPointIndex + OutPointOffset] = FLinearColor::Black;
			OutData.StrandsPoints.PointsRoughness[OutPointIndex + OutPointOffset] = 0;
		}
	#else
		// Decimation using uniform/interleaved removal

		// Insure always the original start/end points are part of the trimmed curve
		const uint32 OutPointCount = FMath::Clamp(uint32(InPointCount * VertexDecimationPercentage), 2u, InPointCount);
		const uint32 VertexBucketSize = InPointCount / OutPointCount;

		// Take vertex in the middle of the bucket
		const uint32 OffsetInsideBucket = FMath::FloorToInt(VertexBucketSize * 0.5f);
		for (uint32 OutPointIndex = 0; OutPointIndex < OutPointCount; ++OutPointIndex)
		{
			uint32 InPointIndex = FMath::Clamp(uint32(VertexBucketSize * OutPointIndex + OffsetInsideBucket), 0u, InPointCount - 1);

			// Insure first and last points map onto the root and tip vertices
			InPointIndex = OutPointIndex == 0 ? 0 : InPointIndex;
			InPointIndex = OutPointIndex == OutPointCount-1 ? InPointCount-1 : InPointIndex;

			OutData.StrandsPoints.PointsPosition [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsPosition	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsCoordU	 [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsCoordU	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsRadius	 [OutPointIndex + OutPointOffset] = InData.StrandsPoints.PointsRadius	[InPointIndex + InPointOffset];
			OutData.StrandsPoints.PointsBaseColor[OutPointIndex + OutPointOffset] = FLinearColor::Black;
			OutData.StrandsPoints.PointsRoughness[OutPointIndex + OutPointOffset] = 0;
		}
	#endif

		OutData.StrandsCurves.CurvesCount[OutCurveIndex]  = OutPointCount;
		OutData.StrandsCurves.CurvesRootUV[OutCurveIndex] = InData.StrandsCurves.CurvesRootUV[InCurveIndex];
		OutData.StrandsCurves.CurvesOffset[OutCurveIndex] = OutPointOffset;
		OutData.StrandsCurves.CurvesLength[OutCurveIndex] = InData.StrandsCurves.CurvesLength[InCurveIndex];
		OutData.StrandsCurves.MaxLength = InData.StrandsCurves.MaxLength;
		OutData.StrandsCurves.MaxRadius = InData.StrandsCurves.MaxRadius;
		OutPointOffset += OutPointCount;
	}

	HairStrandsBuilder::BuildInternalData(OutData, false);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster culling data

struct FClusterGrid
{
	struct FCurve
	{
		FCurve()
		{
			for (uint8 LODIt = 0; LODIt < FHairStrandsClusterCullingData::MaxLOD; ++LODIt)
			{
				CountPerLOD[LODIt] = 0;
			}
		}
		uint32 Offset = 0;
		uint32 Count = 0;
		float Area = 0;
		float AvgRadius = 0;
		float MaxRadius = 0;
		uint32 CountPerLOD[FHairStrandsClusterCullingData::MaxLOD];
	};

	struct FCluster
	{
		float CurveAvgRadius = 0;
		float CurveMaxRadius = 0;
		float RootBoundRadius = 0;
		float Area = 0;
		TArray<FCurve> ClusterCurves;
	};

	FClusterGrid(const FIntVector& InResolution, const FVector& InMinBound, const FVector& InMaxBound)
	{
		MinBound = InMinBound;
		MaxBound = InMaxBound;
		GridResolution = InResolution;
		Clusters.SetNum(FMath::Max(GridResolution.X * GridResolution.Y * GridResolution.Z, 1));
	}

	FORCEINLINE bool IsValid(const FIntVector& P) const
	{
		return	0 <= P.X && P.X < GridResolution.X &&
			0 <= P.Y && P.Y < GridResolution.Y &&
			0 <= P.Z && P.Z < GridResolution.Z;
	}

	FORCEINLINE FIntVector ClampToVolume(const FIntVector& CellCoord, bool& bIsValid) const
	{
		bIsValid = IsValid(CellCoord);
		return FIntVector(
			FMath::Clamp(CellCoord.X, 0, GridResolution.X - 1),
			FMath::Clamp(CellCoord.Y, 0, GridResolution.Y - 1),
			FMath::Clamp(CellCoord.Z, 0, GridResolution.Z - 1));
	}

	FORCEINLINE FIntVector ToCellCoord(const FVector& P) const
	{
		bool bIsValid = false;
		const FVector F = ((P - MinBound) / (MaxBound - MinBound));
		const FIntVector CellCoord = FIntVector(FMath::FloorToInt(F.X * GridResolution.X), FMath::FloorToInt(F.Y * GridResolution.Y), FMath::FloorToInt(F.Z * GridResolution.Z));
		return ClampToVolume(CellCoord, bIsValid);
	}

	uint32 ToIndex(const FIntVector& CellCoord) const
	{
		uint32 CellIndex = CellCoord.X + CellCoord.Y * GridResolution.X + CellCoord.Z * GridResolution.X * GridResolution.Y;
		check(CellIndex < uint32(Clusters.Num()));
		return CellIndex;
	}

	void InsertRenderingCurve(FCurve& Curve, const FVector& Root)
	{
		FIntVector CellCoord = ToCellCoord(Root);
		uint32 Index = ToIndex(CellCoord);
		FCluster& Cluster = Clusters[Index];
		Cluster.ClusterCurves.Add(Curve);
	}

	FVector MinBound;
	FVector MaxBound;
	FIntVector GridResolution;
	TArray<FCluster> Clusters;
};

static void DecimateCurve(
	const TArray<FVector>& InPoints,
	const uint32 InOffset,
	const uint32 InCount,
	const TArray<FHairLODSettings>& InSettings,
	uint32* OutCountPerLOD,
	TArray<uint8>& OutVertexLODMask)
{
	// Insure that all settings have more and more agressive, and rectify it is not the case.
	TArray<FHairLODSettings> Settings = InSettings;
	{
		float PrevFactor = 1;
		float PrevAngle = 0;
		for (FHairLODSettings& S : Settings)
		{
			// Sanitize the decimation values
			S.CurveDecimation  = FMath::Clamp(S.CurveDecimation,  0.f,  1.f);
			S.VertexDecimation = FMath::Clamp(S.VertexDecimation, 0.f,  1.f);
			S.AngularThreshold = FMath::Clamp(S.AngularThreshold, 0.f, 90.f);

			if (S.VertexDecimation > PrevFactor)
			{
				S.VertexDecimation = PrevFactor;
			}

			if (S.AngularThreshold < PrevAngle)
			{
				S.AngularThreshold = PrevAngle;
			}

			PrevFactor = S.VertexDecimation;
			PrevAngle = S.AngularThreshold;
		}
	}

	check(InCount > 2);

	// Array containing the remaining vertex indices. This list get trimmed down as we process over all LODs.
	TArray<uint32> OutIndices;
	OutIndices.SetNum(InCount);
	for (uint32 CurveIt = 0; CurveIt < InCount; ++CurveIt)
	{
		OutIndices[CurveIt] = CurveIt;
	}

	const uint32 LODCount = Settings.Num();
	check(LODCount <= FHairStrandsClusterCullingData::MaxLOD);

	for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		const int32 LODTargetVertexCount = FMath::Clamp(uint32(FMath::CeilToInt(InCount * Settings[LODIt].VertexDecimation)), 2u, InCount);
		const float LODAngularThreshold  = FMath::DegreesToRadians(Settings[LODIt].AngularThreshold);

		// 'bCanDecimate' tracks if it is possible to reduce the remaining vertives even more while respecting the user angular constrain
		bool bCanDecimate = true;
		while (OutIndices.Num() > LODTargetVertexCount && bCanDecimate)
		{
			float MinError = FLT_MAX;
			int32 ElementToRemove = -1;
			const uint32 Count = OutIndices.Num();
			for (uint32 IndexIt = 1; IndexIt < Count - 1; ++IndexIt)
			{
				const FVector& P0 = InPoints[InOffset + OutIndices[IndexIt - 1]];
				const FVector& P1 = InPoints[InOffset + OutIndices[IndexIt]];
				const FVector& P2 = InPoints[InOffset + OutIndices[IndexIt + 1]];

				const float Area = FVector::CrossProduct(P0 - P1, P2 - P1).Size() * 0.5f;

				//     P0 .       . P2
				//         \Inner/
				//   ` .    \   /
				// Thres(` . \^/ ) Angle
				//    --------.---------
				//            P1
				const FVector V0 = (P0 - P1).GetSafeNormal();
				const FVector V1 = (P2 - P1).GetSafeNormal();
				const float InnerAngle = FMath::Abs(FMath::Acos(FVector::DotProduct(V0, V1)));
				const float Angle = (PI - InnerAngle) * 0.5f;

				if (Area < MinError && Angle < LODAngularThreshold)
				{
					MinError = Area;
					ElementToRemove = IndexIt;
				}
			}
			bCanDecimate = ElementToRemove >= 0;
			if (bCanDecimate)
			{
				OutIndices.RemoveAt(ElementToRemove);
			}
		}

		OutCountPerLOD[LODIt] = OutIndices.Num();

		// For all remaining vertices, we mark them as 'used'/'valid' for the current LOD levl
		for (uint32 LocalIndex : OutIndices)
		{
			const uint32 VertexIndex = InOffset + LocalIndex;
			OutVertexLODMask[VertexIndex] |= 1 << LODIt;
		}
	}

	// Sanity check to insure that vertex LOD in a continuous fashion.
	for (uint32 VertexIt = 0; VertexIt < InCount; ++VertexIt)
	{
		const uint8 Mask = OutVertexLODMask[InOffset + VertexIt];
		check(Mask == 0 || Mask == 1 || Mask == 3 || Mask == 7 || Mask == 15 || Mask == 31 || Mask == 63 || Mask == 127 || Mask == 255);
	}
}


inline uint32 to10Bits(float V)
{
	return FMath::Clamp(uint32(V * 1024), 0u, 1023u);
}

float FGroomBuilder::ComputeGroomBoundRadius(const FProcessedHairDescription& Description)
{
	FVector GroomBoundMin(FLT_MAX);
	FVector GroomBoundMax(-FLT_MAX);
	for (auto& Group : Description.HairGroups)
	{
		const FHairGroupData::FStrands& Strands = Group.Value.Value.Strands;
		GroomBoundMin.X = FMath::Min(GroomBoundMin.X, Strands.Data.BoundingBox.Min.X);
		GroomBoundMin.Y = FMath::Min(GroomBoundMin.Y, Strands.Data.BoundingBox.Min.Y);
		GroomBoundMin.Z = FMath::Min(GroomBoundMin.Z, Strands.Data.BoundingBox.Min.Z);

		GroomBoundMax.X = FMath::Max(GroomBoundMax.X, Strands.Data.BoundingBox.Max.X);
		GroomBoundMax.Y = FMath::Max(GroomBoundMax.Y, Strands.Data.BoundingBox.Max.Y);
		GroomBoundMax.Z = FMath::Max(GroomBoundMax.Z, Strands.Data.BoundingBox.Max.Z);
	}

	const float GroomBoundRadius = FVector::Distance(GroomBoundMax, GroomBoundMin) * 0.5f;
	return GroomBoundRadius;
}

float FGroomBuilder::ComputeGroomBoundRadius(const TArray<FHairGroupData>& HairGroupsData)
{
	// Compute the bounding box of all the groups. This is used for scaling LOD sceensize 
	// for each group & cluster respectively to their relative size
	FVector GroomBoundMin(FLT_MAX);
	FVector GroomBoundMax(-FLT_MAX);
	for (const FHairGroupData& LocalGroupData : HairGroupsData)
	{
		GroomBoundMin.X = FMath::Min(GroomBoundMin.X, LocalGroupData.Strands.Data.BoundingBox.Min.X);
		GroomBoundMin.Y = FMath::Min(GroomBoundMin.Y, LocalGroupData.Strands.Data.BoundingBox.Min.Y);
		GroomBoundMin.Z = FMath::Min(GroomBoundMin.Z, LocalGroupData.Strands.Data.BoundingBox.Min.Z);

		GroomBoundMax.X = FMath::Max(GroomBoundMax.X, LocalGroupData.Strands.Data.BoundingBox.Max.X);
		GroomBoundMax.Y = FMath::Max(GroomBoundMax.Y, LocalGroupData.Strands.Data.BoundingBox.Max.Y);
		GroomBoundMax.Z = FMath::Max(GroomBoundMax.Z, LocalGroupData.Strands.Data.BoundingBox.Max.Z);
	}

	const float GroomBoundRadius = FVector::Distance(GroomBoundMax, GroomBoundMin) * 0.5f;
	return GroomBoundRadius;
}

static void InternalBuildClusterData(
	const FHairStrandsDatas& InRenStrandsData,
	const float InGroomAssetRadius, 
	const FHairGroupsLOD& InSettings, 
	FHairStrandsClusterCullingData& Out)
{
	// 0. Rest existing culling data
	Out.Reset();

	const uint32 LODCount = FMath::Min(uint32(InSettings.LODs.Num()), FHairStrandsClusterCullingData::MaxLOD);
	check(LODCount > 0);

	const uint32 RenCurveCount = InRenStrandsData.GetNumCurves();
	Out.VertexCount = InRenStrandsData.GetNumPoints();
	check(Out.VertexCount);

	// 1. Allocate cluster per voxel containing contains >=1 render curve root
	const FVector GroupMinBound = InRenStrandsData.BoundingBox.Min;
	FVector GroupMaxBound = InRenStrandsData.BoundingBox.Max;
	const float GroupRadius = FVector::Distance(GroupMaxBound, GroupMinBound) * 0.5f;

	// Compute the voxel volume resolution, and snap the max bound to the voxel grid
	// Iterate until voxel size are below max resolution, so that computation is not too long
	FIntVector VoxelResolution = FIntVector::ZeroValue;
	{
		const int32 MaxResolution = FMath::Max(GHairClusterBuilder_MaxVoxelResolution, 2);

		float ClusterWorldSize = FMath::Max(InSettings.ClusterWorldSize, 0.001f);
		bool bIsValid = false;
		while (!bIsValid)
		{
			FVector VoxelResolutionF = (GroupMaxBound - GroupMinBound) / ClusterWorldSize;
			VoxelResolution = FIntVector(FMath::CeilToInt(VoxelResolutionF.X), FMath::CeilToInt(VoxelResolutionF.Y), FMath::CeilToInt(VoxelResolutionF.Z));
			bIsValid = VoxelResolution.X <= MaxResolution && VoxelResolution.Y <= MaxResolution && VoxelResolution.Z <= MaxResolution;
			if (!bIsValid)
			{
				ClusterWorldSize *= 2;
			}
		}
		GroupMaxBound = GroupMinBound + FVector(VoxelResolution) * ClusterWorldSize;
	}

	// 2. Insert all rendering curves into the voxel structure
	FClusterGrid ClusterGrid(VoxelResolution, GroupMinBound, GroupMaxBound);
	for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
	{
		FClusterGrid::FCurve RCurve;
		RCurve.Count = InRenStrandsData.StrandsCurves.CurvesCount[RenCurveIndex];
		RCurve.Offset = InRenStrandsData.StrandsCurves.CurvesOffset[RenCurveIndex];
		RCurve.Area = 0.0f;
		RCurve.AvgRadius = 0;
		RCurve.MaxRadius = 0;

		// Compute area of each curve to later compute area correction
		for (uint32 RenPointIndex = 0; RenPointIndex < RCurve.Count; ++RenPointIndex)
		{
			uint32 PointGlobalIndex = RenPointIndex + RCurve.Offset;
			const FVector& V0 = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
			if (RenPointIndex > 0)
			{
				const FVector& V1 = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex - 1];
				FVector OutDir;
				float OutLength;
				(V1 - V0).ToDirectionAndLength(OutDir, OutLength);
				RCurve.Area += InRenStrandsData.StrandsPoints.PointsRadius[PointGlobalIndex] * OutLength;
			}

			const float PointRadius = InRenStrandsData.StrandsPoints.PointsRadius[PointGlobalIndex] * InRenStrandsData.StrandsCurves.MaxRadius;
			RCurve.AvgRadius += PointRadius;
			RCurve.MaxRadius = FMath::Max(RCurve.MaxRadius, PointRadius);
		}
		RCurve.AvgRadius /= FMath::Max(1u, RCurve.Count);

		const FVector Root = InRenStrandsData.StrandsPoints.PointsPosition[RCurve.Offset];
		ClusterGrid.InsertRenderingCurve(RCurve, Root);
	}

	// 3. Count non-empty clusters
	TArray<uint32> ValidClusterIndices;
	{
		uint32 GridLinearIndex = 0;
		ValidClusterIndices.Reserve(ClusterGrid.Clusters.Num() * 0.2);
		for (FClusterGrid::FCluster& Cluster : ClusterGrid.Clusters)
		{
			if (Cluster.ClusterCurves.Num() > 0)
			{
				ValidClusterIndices.Add(GridLinearIndex);
			}
			++GridLinearIndex;
		}
	}
	Out.ClusterCount = ValidClusterIndices.Num();
	Out.ClusterInfos.Init(FHairStrandsClusterCullingData::FHairClusterInfo(), Out.ClusterCount);
	Out.VertexToClusterIds.SetNum(Out.VertexCount);

	// Conservative allocation for inserting vertex indices for the various curves LOD
	uint32* RawClusterVertexIds = new uint32[LODCount * InRenStrandsData.GetNumPoints()];
	TAtomic<uint32> RawClusterVertexCount(0);

	// 4. Write out cluster information
	Out.ClusterLODInfos.SetNum(LODCount * Out.ClusterCount);
	TArray<uint8> VertexLODMasks;
	VertexLODMasks.SetNum(InRenStrandsData.GetNumPoints());

	// Local variable for being capture by the lambda
	TArray<FHairStrandsClusterCullingData::FHairClusterInfo>& LocalClusterInfos = Out.ClusterInfos;
	TArray<FHairStrandsClusterCullingData::FHairClusterLODInfo>& LocalClusterLODInfos = Out.ClusterLODInfos;
	TArray<uint32>& LocalVertexToClusterIds = Out.VertexToClusterIds;
#define USE_PARALLE_FOR 1
#if USE_PARALLE_FOR
	ParallelFor(Out.ClusterCount,
		[
			LODCount,
			InGroomAssetRadius,
			InSettings,
			&ValidClusterIndices,
			&InRenStrandsData,
			&ClusterGrid,
			&LocalClusterInfos,
			&LocalClusterLODInfos,
			&LocalVertexToClusterIds,
			&VertexLODMasks,
			&RawClusterVertexIds,
			&RawClusterVertexCount
		]
	(uint32 ClusterIt)
#else
	for (uint32 ClusterIt = 0; ClusterIt < Out.ClusterCount; ++ClusterIt)
#endif
	{
		const uint32 GridLinearIndex = ValidClusterIndices[ClusterIt];
		FClusterGrid::FCluster& Cluster = ClusterGrid.Clusters[GridLinearIndex];
		check(Cluster.ClusterCurves.Num() != 0);

		// 4.1 Sort curves
		// Sort curve to have largest area first, so that lower area curves with less influence are removed first.
		// This also helps the radius scaling to not explode.
		Cluster.ClusterCurves.Sort([](const FClusterGrid::FCurve& A, const FClusterGrid::FCurve& B) -> bool
			{
				return A.Area > B.Area;
			});

		// 4.2 Compute cluster's area & fill in the vertex to cluster ID mapping
		float ClusterArea = 0;
		FVector ClusterMinBound(FLT_MAX);
		FVector ClusterMaxBound(-FLT_MAX);

		FVector RootMinBound(FLT_MAX);
		FVector RootMaxBound(-FLT_MAX);

		Cluster.CurveMaxRadius = 0;
		Cluster.CurveAvgRadius = 0;
		Cluster.Area = 0;
		for (FClusterGrid::FCurve& ClusterCurve : Cluster.ClusterCurves)
		{
			for (uint32 RenPointIndex = 0; RenPointIndex < ClusterCurve.Count; ++RenPointIndex)
			{
				const uint32 PointGlobalIndex = RenPointIndex + ClusterCurve.Offset;
				LocalVertexToClusterIds[PointGlobalIndex] = ClusterIt;

				const FVector& P = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
				{
					ClusterMinBound.X = FMath::Min(ClusterMinBound.X, P.X);
					ClusterMinBound.Y = FMath::Min(ClusterMinBound.Y, P.Y);
					ClusterMinBound.Z = FMath::Min(ClusterMinBound.Z, P.Z);

					ClusterMaxBound.X = FMath::Max(ClusterMaxBound.X, P.X);
					ClusterMaxBound.Y = FMath::Max(ClusterMaxBound.Y, P.Y);
					ClusterMaxBound.Z = FMath::Max(ClusterMaxBound.Z, P.Z);
				}

				if (RenPointIndex == 0)
				{
					RootMinBound.X = FMath::Min(RootMinBound.X, P.X);
					RootMinBound.Y = FMath::Min(RootMinBound.Y, P.Y);
					RootMinBound.Z = FMath::Min(RootMinBound.Z, P.Z);

					RootMaxBound.X = FMath::Max(RootMaxBound.X, P.X);
					RootMaxBound.Y = FMath::Max(RootMaxBound.Y, P.Y);
					RootMaxBound.Z = FMath::Max(RootMaxBound.Z, P.Z);
				}
			}
			Cluster.CurveMaxRadius = FMath::Max(Cluster.CurveMaxRadius, ClusterCurve.MaxRadius);
			Cluster.CurveAvgRadius += ClusterCurve.AvgRadius;
			Cluster.Area += ClusterCurve.Area;
		}
		Cluster.CurveAvgRadius /= FMath::Max(1, Cluster.ClusterCurves.Num());
		Cluster.RootBoundRadius = (RootMaxBound - RootMinBound).GetMax() * 0.5f + Cluster.CurveAvgRadius;

		// Compute the max radius that a cluster can have. This is done by computing an estimate of the cluster coverage (using pre-computed LUT) 
		// and computing how much is visible
		// This supposes the radius is proportional to the radius of the roots bounding volume
		const float NormalizedAvgRadius = Cluster.CurveAvgRadius / Cluster.RootBoundRadius;
		const float ClusterCoverage = GetHairCoverage(Cluster.ClusterCurves.Num(), NormalizedAvgRadius);
		const float ClusterVisibleRadius = Cluster.RootBoundRadius * ClusterCoverage;

		const float ClusterRadius = FVector::Distance(ClusterMaxBound, ClusterMinBound) * 0.5f;


		// 4.3 Compute the number of curve per LOD
		// Compute LOD infos (vertx count, vertex offset, radius scale ...)
		// Compute the ratio of the cluster related the actual groom and scale the screen size accordingly
		TArray<uint32> LODCurveCount;
		LODCurveCount.SetNum(LODCount);
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			const float CurveDecimation = FMath::Clamp(InSettings.LODs[LODIt].CurveDecimation, 0.0f, 1.0f);
			LODCurveCount[LODIt] = FMath::Clamp(FMath::CeilToInt(Cluster.ClusterCurves.Num() * CurveDecimation), 1, Cluster.ClusterCurves.Num());
		}

		// 4.4 Decimate each curve for all LODs
		// This fill in a bitfield per vertex which indiates on which LODs a vertex can be used
		for (uint32 CurveIt = 0, CurveCount = uint32(Cluster.ClusterCurves.Num()); CurveIt < CurveCount; ++CurveIt)
		{
			FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];

			// Don't need to DecimateCurve if there are only 2 control vertices
			if (ClusterCurve.Count > 2)
			{
				DecimateCurve(
					InRenStrandsData.StrandsPoints.PointsPosition,
					ClusterCurve.Offset,
					ClusterCurve.Count,
					InSettings.LODs,
					ClusterCurve.CountPerLOD,
					VertexLODMasks);
			}
		}

		// 4.5 Record/Insert vertex indices for each LOD of the current cluster
		// Vertex offset is stored into the cluster LOD info
		// Stores the accumulated vertex count per LOD
		//
		// ClusterVertexIds contains the vertex index of curve belonging to a cluster.
		// Since for a given LOD, both the number of curve and vertices varies, we stores 
		// this information per LOD.
		//
		//  Global Vertex index
		//            v
		// ||0 1 2 3 4 5 6 7 8 9 ||0 1 3 5 7 9 ||0 5 9 | |0 1 2 3 4 5 6 7 || 0 1 5 7 ||0 9 ||||11 12 ...
		// ||____________________||____________||______| |________________||_________||____||||_____ _ _ 
		// ||        LOD 0			 LOD 1		 LOD2  | |    LOD 0			 LOD 1	  LOD2 ||||  LOD 0
		// ||__________________________________________| | ________________________________||||_____ _ _ 
		// |                   Curve 0								Curve 1				    ||   Curve 0
		// |________________________________________________________________________________||_____ _ _ 
		//										Cluster 0										Cluster 1

		TArray<uint32> LocalClusterVertexIds;
		LocalClusterVertexIds.Reserve(LODCount * Cluster.ClusterCurves.Num() * 32); // Guestimate pre-allocation (32 points per curve in average)

		FHairStrandsClusterCullingData::FHairClusterInfo& ClusterInfo = LocalClusterInfos[ClusterIt];
		ClusterInfo.LODCount = LODCount;
		ClusterInfo.LODInfoOffset = LODCount * ClusterIt;
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			FHairStrandsClusterCullingData::FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.VertexOffset = LocalClusterVertexIds.Num(); // At the end, it will be the offset at which the data are inserted into ClusterVertexIds
			ClusterLODInfo.VertexCount0 = 0;
			ClusterLODInfo.VertexCount1 = 0;
			ClusterLODInfo.RadiusScale0 = 0;
			ClusterLODInfo.RadiusScale1 = 0;

			const uint32 CurveCount = LODCurveCount[LODIt];
			const uint32 NextCurveCount = LODIt < LODCount - 1 ? LODCurveCount[LODIt + 1] : CurveCount;
			for (uint32 CurveIt = 0; CurveIt < CurveCount; ++CurveIt)
			{
				FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];

				for (uint32 PointIt = 0; PointIt < ClusterCurve.Count; ++PointIt)
				{
					const uint32 GlobalPointIndex = PointIt + ClusterCurve.Offset;
					const uint8 LODMask = VertexLODMasks[GlobalPointIndex];
					if (LODMask & (1 << LODIt))
					{
						// Count the number of vertices for all curves in the cluster as well as the vertex 
						// of the remaining curves once the cluster has been decimated with the current LOD 
						// settings
						++ClusterLODInfo.VertexCount0;
						if (CurveIt < NextCurveCount)
						{
							++ClusterLODInfo.VertexCount1;
						}

						LocalClusterVertexIds.Add(GlobalPointIndex);
					}
				}
			}
		}

		// 4.5.1 Insert vertex indices for each LOD into the final array
		// Since this runs in parallel, we prefill LocalClusterVertexIds with 
		// all indices, then we insert the indices into the final array with a single allocation + memcopy
		// We also patch the vertex offset so that it is correct
		const uint32 AllocOffset = RawClusterVertexCount.AddExchange(LocalClusterVertexIds.Num());
		FMemory::Memcpy(RawClusterVertexIds + AllocOffset, LocalClusterVertexIds.GetData(), LocalClusterVertexIds.Num() * sizeof(uint32));
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			FHairStrandsClusterCullingData::FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.VertexOffset += AllocOffset;
		}

		// 4.6 Compute the radius scaling to preserve the cluster apperance as we decimate 
		// the number of strands
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			// Compute the visible area for various orientation? 
			// Reference: Stochastic Simplification of Aggregate Detail
			float  LODArea = 0;
			float  LODAvgRadiusRef = 0;
			float  LODMaxRadiusRef = 0;
			uint32 LODVertexCount = 0;

			const uint32 ClusterCurveCount = LODCurveCount[LODIt];
			for (uint32 CurveIt = 0; CurveIt < ClusterCurveCount; ++CurveIt)
			{
				const FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];
				LODVertexCount += ClusterCurve.Count;
				LODArea += ClusterCurve.Area;
				LODAvgRadiusRef += ClusterCurve.AvgRadius;
				LODMaxRadiusRef = FMath::Max(LODMaxRadiusRef, ClusterCurve.MaxRadius);
			}
			LODAvgRadiusRef /= ClusterCurveCount;

			// Compute what should be the average (normalized) radius of the strands, and scale it 
			// with the radius of the clusters/roots to get an actual world radius.
			const float LODAvgRadiusTarget = Cluster.RootBoundRadius * GetHairAvgRadius(ClusterCurveCount, ClusterCoverage);

			// Compute the ratio between the size of the cluster and the size of the groom (at rest position)
			// On the GPU, we compute the screen size of the cluster, and use the LOD screensize to know which 
			// LOD needs to be pick up. Since the screen area are setup by artists based the entire groom (not 
			// based on the cluster size), we precompute the correcting ratio here, and pre-scale the LOD screensize
			const float ScreenSizeScale = InSettings.ClusterScreenSizeScale * ClusterRadius / InGroomAssetRadius;

			float LODScale = LODAvgRadiusTarget / LODAvgRadiusRef;
			if (LODMaxRadiusRef * LODScale > ClusterVisibleRadius)
			{
				LODScale = FMath::Max(LODMaxRadiusRef, ClusterVisibleRadius) / LODMaxRadiusRef;
			}
			LODScale *= FMath::Max(InSettings.LODs[LODIt].ThicknessScale, 0.f);
			//if (LODMaxRadiusRef * LODScale > Cluster.RootBoundRadius)
			//{
			//	LODScale = Cluster.RootBoundRadius / LODMaxRadiusRef;
			//}

			ClusterInfo.ScreenSize[LODIt] = InSettings.LODs[LODIt].ScreenSize * ScreenSizeScale;
			ClusterInfo.bIsVisible[LODIt] = InSettings.LODs[LODIt].bVisible;
			FHairStrandsClusterCullingData::FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.RadiusScale0 = LODScale;
			ClusterLODInfo.RadiusScale1 = LODScale;
		}

		// Fill in transition radius between LOD to insure that the interpolation is continuous
		for (uint8 LODIt = 0; LODIt < LODCount - 1; ++LODIt)
		{
			FHairStrandsClusterCullingData::FHairClusterLODInfo& Curr = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			FHairStrandsClusterCullingData::FHairClusterLODInfo& Next = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt + 1];
			Curr.RadiusScale1 = Next.RadiusScale0;
		}
	}
#if USE_PARALLE_FOR
	);
#endif

	// Compute the screen size of the entire group at which the groom need to change LOD
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		Out.CPULODScreenSize.Add(InSettings.LODs[LODIt].ScreenSize);
		Out.LODVisibility.Add(InSettings.LODs[LODIt].bVisible);
	}

	// Copy the final value to the array which will be used to copy data to the GPU.
	// This operations is not needer per se. We could just keep & use RawClusterVertexIds
	Out.ClusterVertexIds.SetNum(RawClusterVertexCount);
	FMemory::Memcpy(Out.ClusterVertexIds.GetData(), RawClusterVertexIds, RawClusterVertexCount * sizeof(uint32));

	delete[] RawClusterVertexIds;

	// Pack LODInfo into GPU format
	{
		check(uint32(Out.ClusterInfos.Num()) == Out.ClusterCount);
		check(uint32(Out.VertexToClusterIds.Num()) == Out.VertexCount);
		
		Out.PackedClusterInfos.Reserve(Out.ClusterInfos.Num());
		for (const FHairStrandsClusterCullingData::FHairClusterInfo& Info : Out.ClusterInfos)
		{
			FHairStrandsClusterCullingData::FHairClusterInfo::Packed& PackedInfo = Out.PackedClusterInfos.AddDefaulted_GetRef();
			PackedInfo.LODCount = FMath::Clamp(Info.LODCount, 0u, 0xFFu);
			PackedInfo.LODInfoOffset = FMath::Clamp(Info.LODInfoOffset, 0u, (1u << 24u) - 1u);
			PackedInfo.LOD_ScreenSize_0 = to10Bits(Info.ScreenSize[0]);
			PackedInfo.LOD_ScreenSize_1 = to10Bits(Info.ScreenSize[1]);
			PackedInfo.LOD_ScreenSize_2 = to10Bits(Info.ScreenSize[2]);
			PackedInfo.LOD_ScreenSize_3 = to10Bits(Info.ScreenSize[3]);
			PackedInfo.LOD_ScreenSize_4 = to10Bits(Info.ScreenSize[4]);
			PackedInfo.LOD_ScreenSize_5 = to10Bits(Info.ScreenSize[5]);
			PackedInfo.LOD_ScreenSize_6 = to10Bits(Info.ScreenSize[6]);
			PackedInfo.LOD_ScreenSize_7 = to10Bits(Info.ScreenSize[7]);
			PackedInfo.LOD_bIsVisible = 0;
			for (uint32 LODIt = 0; LODIt < FHairStrandsClusterCullingData::MaxLOD; ++LODIt)
			{
				if (Info.bIsVisible[LODIt])
				{
					PackedInfo.LOD_bIsVisible = PackedInfo.LOD_bIsVisible | (1 << LODIt);
				}
			}

			PackedInfo.Pad0 = 0;
			PackedInfo.Pad1 = 0;
			PackedInfo.Pad2 = 0;
		}
	}
}

void FGroomBuilder::BuildClusterData(UGroomAsset* GroomAsset, const float GroomBoundRadius)
{
	if (GroomAsset)
	{
		const uint32 GroupCount = GroomAsset->HairGroupsData.Num();
		for (uint32 GroupIt = 0; GroupIt < GroupCount; GroupIt++)
		{
			FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];
			InternalBuildClusterData(
				GroupData.Strands.Data,
				GroomBoundRadius,
				GroomAsset->HairGroupsLOD[GroupIt],
				GroupData.Strands.ClusterCullingData);
		}
	}
}
void FGroomBuilder::BuildClusterData(UGroomAsset* GroomAsset, const float GroomBoundRadius, uint32 GroupIndex)
{
	if (GroomAsset)
	{
		FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIndex];
		InternalBuildClusterData(
			GroupData.Strands.Data,
			GroomBoundRadius,
			GroomAsset->HairGroupsLOD[GroupIndex],
			GroupData.Strands.ClusterCullingData);
	}
}

void FGroomBuilder::BuildClusterData(UGroomAsset* GroomAsset, const FProcessedHairDescription& ProcessedHairDescription)
{
	if (GroomAsset)
	{
		const float GroomBoundRadius = FGroomBuilder::ComputeGroomBoundRadius(ProcessedHairDescription);
		FGroomBuilder::BuildClusterData(GroomAsset, GroomBoundRadius);
	}
}
void FGroomBuilder::BuildClusterData(UGroomAsset* GroomAsset, const FProcessedHairDescription& ProcessedHairDescription, uint32 GroupIndex)
{
	if (GroomAsset)
	{
		const float GroomBoundRadius = FGroomBuilder::ComputeGroomBoundRadius(ProcessedHairDescription);
		FGroomBuilder::BuildClusterData(GroomAsset, GroomBoundRadius, GroupIndex);
	}
}


#undef LOCTEXT_NAMESPACE
