// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GroomBuilder.h"
#include "GroomAsset.h"
#include "GroomComponent.h"
#include "GroomSettings.h"
#include "HairDescription.h"

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
	void BuildRenderData(FHairStrandsDatas& HairStrands)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HairStrandsBuilder::BuildRenderData);

		uint32 NumCurves = HairStrands.GetNumCurves();
		uint32 NumPoints = HairStrands.GetNumPoints();
		if (!(NumCurves > 0 && NumPoints > 0))
			return;

		TArray<FHairStrandsPositionFormat::Type>& OutPackedPositions = HairStrands.RenderData.RenderingPositions;
		TArray<FHairStrandsAttributeFormat::Type>& OutPackedAttributes = HairStrands.RenderData.RenderingAttributes;
		TArray<FHairStrandsMaterialFormat::Type>& OutPackedMaterials = HairStrands.RenderData.RenderingMaterials;

		OutPackedPositions.SetNum(NumPoints * FHairStrandsPositionFormat::ComponentCount);
		OutPackedAttributes.SetNum(NumPoints * FHairStrandsAttributeFormat::ComponentCount);
		OutPackedMaterials.SetNum(NumPoints * FHairStrandsMaterialFormat::ComponentCount);

		const FVector HairBoxCenter = HairStrands.BoundingBox.GetCenter();

		FHairStrandsCurves& Curves = HairStrands.StrandsCurves;
		FHairStrandsPoints& Points = HairStrands.StrandsPoints;

		FRandomStream Random;
		for (uint32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
		{
			const float CurveSeed = Random.RandHelper(255);
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
}

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
		FRandomStream& Random, 
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
			RandIndex0 = Random.RandRange(0, FMetrics::Count - 1);
			RandIndex1 = Random.RandRange(0, FMetrics::Count - 1);
			RandIndex2 = Random.RandRange(0, FMetrics::Count - 1);
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
			FRandomStream& Random) const
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
			SelectFinalGuides(ClosestGuides, Random, Metrics, bRandomized, bUnique);

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
			FRandomStream& Random) const 
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
				SelectFinalGuides(ClosestGuides, Random, Metrics0, bRandomized, bUnique);
			}
			else
			{
				SelectFinalGuides(ClosestGuides, Random, Metrics1, bRandomized, bUnique);
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
		FRandomStream& Random)
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
		SelectFinalGuides(ClosestGuides, Random, Metrics, bRandomized, bUnique);

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

	enum class EHairInterpolationWeightMethod : uint8
	{
		ParametricDistance,
		RootDistance,
		VertexIndex
	};

	enum class EHairInterpolationDataQuality : uint8
	{
		Low,
		Medium,
		High
	};

	struct FHairInterpolationSettings
	{
		EHairInterpolationDataQuality Quality		= EHairInterpolationDataQuality::High;
		EHairInterpolationWeightMethod WeightMethod = EHairInterpolationWeightMethod::ParametricDistance;
		bool bRandomizedGuides						= false;
		bool bUseUniqueGuide						= false;
	};

	static void BuildInterpolationData(
		FHairStrandsInterpolationDatas& InterpolationData,
		const FHairStrandsDatas& SimStrandsData,
		const FHairStrandsDatas& RenStrandsData,
		const FHairInterpolationSettings& Settings)
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

			if (Settings.Quality == EHairInterpolationDataQuality::Low || Settings.Quality == EHairInterpolationDataQuality::Medium)
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

		FRandomStream Random;
		const uint32 RenCurveCount = RenStrandsData.GetNumCurves();
		const uint32 SimCurveCount = SimStrandsData.GetNumCurves();

		TAtomic<uint32> CompletedTasks(0);
		FScopedSlowTask SlowTask(RenCurveCount, LOCTEXT("BuildInterpolationData", "Building groom simulation data"));
		SlowTask.MakeDialog();

		ParallelFor(RenCurveCount, 
		[
			Settings,
			RenCurveCount, &RenRoots, &RenStrandsData,
			SimCurveCount, &SimRoots, &SimStrandsData, 
			&RootsGrid,
			&TotalInvalidInterpolationCount,  
			&InterpolationData, 
			&Random,
			&CompletedTasks,
			&SlowTask
		] (uint32 RenCurveIndex) 
		//for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HairInterpolationBuilder::ComputingClosestGuidesAndWeights);

			++CompletedTasks;

			if (IsInGameThread())
			{
				uint32 CurrentCompletedTasks = CompletedTasks.Exchange(0);
				SlowTask.EnterProgressFrame(CurrentCompletedTasks, LOCTEXT("ComputeGuidesAndWeights", "Computing closest guides and weights"));
			}

			const FHairRoot& StrandRoot = RenRoots[RenCurveIndex];

			FClosestGuides ClosestGuides;
			if (Settings.Quality == EHairInterpolationDataQuality::Low)
			{
				ClosestGuides = RootsGrid.FindClosestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizedGuides, Settings.bUseUniqueGuide, Random);
			}
			else if (Settings.Quality == EHairInterpolationDataQuality::Medium)
			{
				ClosestGuides = RootsGrid.FindBestClosestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizedGuides, Settings.bUseUniqueGuide, Random);
			}
			else // (Settings.Quality == EHairInterpolationDataQuality::High)
			{
				ClosestGuides = FindBestRoots(RenCurveIndex, RenRoots, SimRoots, RenStrandsData, SimStrandsData, Settings.bRandomizedGuides, Settings.bUseUniqueGuide, Random);
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
					if (Settings.WeightMethod == EHairInterpolationWeightMethod::ParametricDistance)
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
					if (Settings.WeightMethod == EHairInterpolationWeightMethod::RootDistance)
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
					if (Settings.WeightMethod == EHairInterpolationWeightMethod::VertexIndex)
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
}

bool FGroomBuilder::BuildGroom(const FHairDescription& HairDescription, const FGroomBuildSettings& BuildSettings, UGroomAsset* GroomAsset)
{
	if (!GroomAsset)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildGroom);

	// Convert HairDescription to HairStrandsDatas
	// For now, just convert HairDescription to HairStrandsDatas
	int32 NumCurves = HairDescription.GetNumStrands();
	int32 NumVertices = HairDescription.GetNumVertices();

	// Check for required attributes
	TGroomAttributesConstRef<int> MajorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MajorVersion);
	TGroomAttributesConstRef<int> MinorVersion = HairDescription.GroomAttributes().GetAttributesRef<int>(HairAttribute::Groom::MinorVersion);

	if (!MajorVersion.IsValid() || !MinorVersion.IsValid())
	{
		UE_LOG(LogGroomBuilder, Warning, TEXT("No version number attributes found. The groom may be missing attributes even if it imports."));
	}

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
	bool bHasUVData = StrandRootUV.IsValid();

	TStrandAttributesConstRef<int> StrandGuides = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::Guide);
	TStrandAttributesConstRef<int> GroupIDs = HairDescription.StrandAttributes().GetAttributesRef<int>(HairAttribute::Strand::GroupID);

	bool bImportGuides = !BuildSettings.bOverrideGuides;

	FHairStrandsDatas* CurrentHairStrandsDatas = nullptr;

	typedef TPair<FHairGroupInfo, FHairGroupData> FHairGroup;
	TMap<int32, FHairGroup> HairGroups;

	int32 GlobalVertexIndex = 0;
	int32 NumHairCurves = 0;
	int32 NumGuideCurves = 0;
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

		int32 GroupID = 0;
		if (GroupIDs.IsValid())
		{
			GroupID = GroupIDs[StrandID];
		}

		FHairGroup& Group = HairGroups.FindOrAdd(GroupID);
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;
		GroupInfo.GroupID = GroupID;
		if (!bIsGuide)
		{
			++NumHairCurves;
			NumHairPoints += CurveNumVertices;
			CurrentHairStrandsDatas = &GroupData.HairRenderData;

			++GroupInfo.NumCurves;
		}
		else if (bImportGuides)
		{
			++NumGuideCurves;
			NumGuidePoints += CurveNumVertices;
			CurrentHairStrandsDatas = &GroupData.HairSimulationData;

			++GroupInfo.NumGuides;
		}
		else
		{
			// A guide but don't want to import it, so skip it
			GlobalVertexIndex += CurveNumVertices;
			continue;
		}

		CurrentHairStrandsDatas->StrandsCurves.CurvesCount.Add(CurveNumVertices);

		if (bHasUVData)
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

			// Fall back to strand width if there was no vertex width
			if (VertexWidth == 0.f && StrandWidth != 0.f)
			{
				VertexWidth = StrandWidth;
			}

			CurrentHairStrandsDatas->StrandsPoints.PointsRadius.Add(VertexWidth * 0.5f);
		}
	}

	FGroomComponentRecreateRenderStateContext RecreateRenderContext(GroomAsset);

	for (TPair<int32, FHairGroup>& HairGroupIt : HairGroups)
	{
		int32 GroupID = HairGroupIt.Key;
		FHairGroup& Group = HairGroupIt.Value;
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;

		FHairStrandsDatas& HairRenderData = GroupData.HairRenderData;
		int32 GroupNumCurves = HairRenderData.StrandsCurves.Num();
		HairRenderData.StrandsCurves.SetNum(GroupNumCurves);
		GroupInfo.NumCurves = GroupNumCurves;

		int32 GroupNumPoints = HairRenderData.StrandsPoints.Num();
		HairRenderData.StrandsPoints.SetNum(GroupNumPoints);

		HairStrandsBuilder::BuildInternalData(HairRenderData, !bHasUVData);

		FHairStrandsDatas& HairSimulationData = GroupData.HairSimulationData;
		GroupNumCurves = GroupData.HairSimulationData.StrandsCurves.Num();

		if (GroupNumCurves > 0)
		{
			GroupInfo.NumGuides = GroupNumCurves;
			HairSimulationData.StrandsCurves.SetNum(GroupNumCurves);

			GroupNumPoints = HairSimulationData.StrandsPoints.Num();
			HairSimulationData.StrandsPoints.SetNum(GroupNumPoints);

			HairStrandsBuilder::BuildInternalData(HairSimulationData, true); // Imported guides don't currently have root UVs so force computing them
		}
		else
		{
			GroomAsset->HairToGuideDensity = FMath::Clamp(BuildSettings.HairToGuideDensity, 0.01f, 1.f);
		}
	}

	for (TPair<int32, FHairGroup> HairGroupIt : HairGroups)
	{
		FHairGroup& Group = HairGroupIt.Value;
		FHairGroupInfo& GroupInfo = Group.Key;
		FHairGroupData& GroupData = Group.Value;

		GroomAsset->HairGroupsInfo.Add(MoveTemp(GroupInfo));
		GroomAsset->HairGroupsData.Add(MoveTemp(GroupData));
	}

	BuildData(GroomAsset, uint8(BuildSettings.InterpolationQuality), uint8(BuildSettings.InterpolationDistance), BuildSettings.bRandomizeGuide, BuildSettings.bUseUniqueGuide);

	GroomAsset->InitResource();

	return true;
}

void FGroomBuilder::BuildData(UGroomAsset* GroomAsset, uint8 QualityLevel, uint8 WeightMethod, bool bRandomize, bool bUnique)
{
	if (!GroomAsset)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FGroomBuilder::BuildData);

	for (int32 Index = 0; Index < GroomAsset->GetNumHairGroups(); ++Index)
	{
		FHairGroupData& GroupData = GroomAsset->HairGroupsData[Index];

		FHairStrandsDatas& HairRenderData = GroupData.HairRenderData;
		FHairStrandsDatas& HairSimulationData = GroupData.HairSimulationData;

		if (HairSimulationData.GetNumCurves() == 0)
		{
			FHairGroupInfo& GroupInfo = GroomAsset->HairGroupsInfo[Index];

			float GuideDensity = FMath::Clamp(GroomAsset->HairToGuideDensity, 0.01f, 1.f);
			GenerateGuides(HairRenderData, GuideDensity, HairSimulationData);
			GroupInfo.bIsAutoGenerated = true;
			GroupInfo.NumGuides = HairSimulationData.GetNumCurves();
		}

		// Build RenderData for HairStrandsDatas
		HairStrandsBuilder::BuildRenderData(HairRenderData);

		HairStrandsBuilder::BuildRenderData(HairSimulationData);

		FHairStrandsInterpolationDatas& HairInterpolationData = GroupData.HairInterpolationData;

		// Build InterpolationData from render and simulation HairStrandsDatas
		HairInterpolationBuilder::FHairInterpolationSettings Settings;
		Settings.bRandomizedGuides = bRandomize;
		Settings.bUseUniqueGuide = bUnique;
		switch (WeightMethod)
		{
			case 0: Settings.WeightMethod = HairInterpolationBuilder::EHairInterpolationWeightMethod::ParametricDistance; break;
			case 1: Settings.WeightMethod = HairInterpolationBuilder::EHairInterpolationWeightMethod::RootDistance; break;
			case 2: Settings.WeightMethod = HairInterpolationBuilder::EHairInterpolationWeightMethod::VertexIndex; break;
		}
		switch (QualityLevel)
		{
			case 0: Settings.Quality = HairInterpolationBuilder::EHairInterpolationDataQuality::Low; break;
			case 1: Settings.Quality = HairInterpolationBuilder::EHairInterpolationDataQuality::Medium; break;
			case 2: Settings.Quality = HairInterpolationBuilder::EHairInterpolationDataQuality::High; break;
		}
		HairInterpolationBuilder::BuildInterpolationData(HairInterpolationData, HairSimulationData, HairRenderData, Settings);

		// Build Rendering data for InterpolationData
		HairInterpolationBuilder::BuildRenderData(HairInterpolationData);
	}
}

void FGroomBuilder::GenerateGuides(const FHairStrandsDatas& InData, float DecimationPercentage, FHairStrandsDatas& OutData)
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
	for (uint32 OutCurveIndex = 0; OutCurveIndex < OutCurveCount; ++OutCurveIndex)
	{
		const uint32 InCurveIndex	= CurveIndices[OutCurveIndex];
		const uint32 InPointOffset	= InData.StrandsCurves.CurvesOffset[InCurveIndex];
		const uint32 PointCount		= InData.StrandsCurves.CurvesCount[InCurveIndex];
		OutData.StrandsCurves.CurvesCount[OutCurveIndex]  = PointCount;
		OutData.StrandsCurves.CurvesRootUV[OutCurveIndex] = InData.StrandsCurves.CurvesRootUV[InCurveIndex];
		OutData.StrandsCurves.CurvesOffset[OutCurveIndex] = OutPointOffset;
		OutData.StrandsCurves.CurvesLength[OutCurveIndex] = InData.StrandsCurves.CurvesLength[InCurveIndex];
		OutData.StrandsCurves.MaxLength = InData.StrandsCurves.MaxLength;
		OutData.StrandsCurves.MaxRadius = InData.StrandsCurves.MaxRadius;

		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			OutData.StrandsPoints.PointsPosition [PointIndex + OutPointOffset] = InData.StrandsPoints.PointsPosition	[PointIndex + InPointOffset];
			OutData.StrandsPoints.PointsCoordU	 [PointIndex + OutPointOffset] = InData.StrandsPoints.PointsCoordU		[PointIndex + InPointOffset];
			OutData.StrandsPoints.PointsRadius	 [PointIndex + OutPointOffset] = InData.StrandsPoints.PointsRadius		[PointIndex + InPointOffset];
			OutData.StrandsPoints.PointsBaseColor[PointIndex + OutPointOffset] = FLinearColor::Black;
			OutData.StrandsPoints.PointsRoughness[PointIndex + OutPointOffset] = 0;
		}
		OutPointOffset += PointCount;
	}

	HairStrandsBuilder::BuildInternalData(OutData, false);
}

#undef LOCTEXT_NAMESPACE
