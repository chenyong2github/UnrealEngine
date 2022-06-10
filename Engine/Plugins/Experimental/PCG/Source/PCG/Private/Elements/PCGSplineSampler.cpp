// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSplineSampler.h"

#include "PCGCommon.h"
#include "Data/PCGIntersectionData.h"
#include "Data/PCGLandscapeSplineData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

namespace PCGSplineSampler
{
	struct FStepSampler
	{
		FStepSampler(const UPCGPolyLineData* InLineData)
			: LineData(InLineData)
		{
			check(LineData);
			CurrentSegmentIndex = 0;
		}

		virtual void Step(FTransform& OutTransform, FBox& OutBox) = 0;

		bool IsDone() const
		{
			return CurrentSegmentIndex >= LineData->GetNumSegments();
		}

		const UPCGPolyLineData* LineData;
		int CurrentSegmentIndex;
	};

	struct FSubdivisionStepSampler : public FStepSampler
	{
		FSubdivisionStepSampler(const UPCGPolyLineData* InLineData, const FPCGSplineSamplerParams& Params)
			: FStepSampler(InLineData)
		{
			NumSegments = LineData->GetNumSegments();
			SubdivisionsPerSegment = Params.SubdivisionsPerSegment;

			CurrentSegmentIndex = 0;
			SubpointIndex = 0;
		}

		virtual void Step(FTransform& OutTransform, FBox& OutBox) override
		{
			const FVector::FReal SegmentLength = LineData->GetSegmentLength(CurrentSegmentIndex);
			const FVector::FReal SegmentStep = SegmentLength / (SubdivisionsPerSegment + 1);

			OutTransform = LineData->GetTransformAtDistance(CurrentSegmentIndex, SubpointIndex * SegmentStep, &OutBox);

			if (SubpointIndex == 0)
			{
				const int PreviousSegmentIndex = (CurrentSegmentIndex > 0 ? CurrentSegmentIndex : NumSegments) - 1;
				const FVector::FReal PreviousSegmentLength = LineData->GetSegmentLength(PreviousSegmentIndex);
				FTransform PreviousSegmentEndTransform = LineData->GetTransformAtDistance(PreviousSegmentIndex, PreviousSegmentLength);

				if ((PreviousSegmentEndTransform.GetLocation() - OutTransform.GetLocation()).Length() <= KINDA_SMALL_NUMBER)
				{
					OutBox.Min.X *= 0.5 * PreviousSegmentLength / (PreviousSegmentEndTransform.GetScale3D().X * (SubdivisionsPerSegment + 1));
				}
				else
				{
					OutBox.Min.X *= 0.5 * SegmentStep / OutTransform.GetScale3D().X;
				}
			}
			else
			{
				OutBox.Min.X *= 0.5 * SegmentStep / OutTransform.GetScale3D().X;
			}

			OutBox.Max.X *= 0.5 * SegmentStep / OutTransform.GetScale3D().X;

			++SubpointIndex;
			if (SubpointIndex > SubdivisionsPerSegment)
			{
				SubpointIndex = 0;
				++CurrentSegmentIndex;
			}
		}

		int NumSegments;
		int SubdivisionsPerSegment;
		int SubpointIndex;
	};

	struct FDistanceStepSampler : public FStepSampler
	{
		FDistanceStepSampler(const UPCGPolyLineData* InLineData, const FPCGSplineSamplerParams& Params)
			: FStepSampler(InLineData)
		{
			DistanceIncrement = Params.DistanceIncrement;
			CurrentDistance = 0;
		}

		virtual void Step(FTransform& OutTransform, FBox& OutBox) override
		{
			FVector::FReal CurrentSegmentLength = LineData->GetSegmentLength(CurrentSegmentIndex);
			OutTransform = LineData->GetTransformAtDistance(CurrentSegmentIndex, CurrentDistance, &OutBox);

			OutBox.Min.X *= DistanceIncrement / OutTransform.GetScale3D().X;
			OutBox.Max.X *= DistanceIncrement / OutTransform.GetScale3D().X;

			CurrentDistance += DistanceIncrement;
			while(CurrentDistance > CurrentSegmentLength)
			{
				CurrentDistance -= CurrentSegmentLength;
				++CurrentSegmentIndex;
				if (!IsDone())
				{
					CurrentSegmentLength = LineData->GetSegmentLength(CurrentSegmentIndex);
				}
				else
				{
					break;
				}
			}
		}

		FVector::FReal DistanceIncrement;
		FVector::FReal CurrentDistance;
	};

	struct FDimensionSampler
	{
		FDimensionSampler(const UPCGPolyLineData* InLineData, const UPCGSpatialData* InSpatialData)
		{
			check(InLineData);
			LineData = InLineData;
			SpatialData = InSpatialData;
		}

		virtual void Sample(const FTransform& InTransform, const FBox& InBox, UPCGPointData* OutPointData)
		{
			FPCGPoint TrivialPoint;
			if(SpatialData->SamplePoint(InTransform, InBox, TrivialPoint, OutPointData->Metadata))
			{
				OutPointData->GetMutablePoints().Add(TrivialPoint);
			}
		}

		const UPCGPolyLineData* LineData;
		const UPCGSpatialData* SpatialData;
	};

	struct FVolumeSampler : public FDimensionSampler
	{
		FVolumeSampler(const UPCGPolyLineData* InLineData, const UPCGSpatialData* InSpatialData, const FPCGSplineSamplerParams& Params)
			: FDimensionSampler(InLineData, InSpatialData)
		{
			Fill = Params.Fill;
			NumPlanarSteps = 1 + ((Params.Dimension == EPCGSplineSamplingDimension::OnVertical) ? 0 : Params.NumPlanarSubdivisions);
			NumHeightSteps = 1 + ((Params.Dimension == EPCGSplineSamplingDimension::OnHorizontal) ? 0 : Params.NumHeightSubdivisions);
		}

		virtual void Sample(const FTransform& InTransform, const FBox& InBox, UPCGPointData* OutPointData) override
		{
			// We're assuming that we can scale against the origin in this method so this should always be true.
			// We will also assume that we can separate the curve into 4 ellipse sections for radius checks
			check(InBox.Max.Y > 0 && InBox.Min.Y < 0 && InBox.Max.Z > 0 && InBox.Min.Z < 0);

			const FVector::FReal YHalfStep = 0.5 * (InBox.Max.Y - InBox.Min.Y) / (FVector::FReal)NumPlanarSteps;
			const FVector::FReal ZHalfStep = 0.5 * (InBox.Max.Z - InBox.Min.Z) / (FVector::FReal)NumHeightSteps;

			FBox SubBox = InBox;
			SubBox.Min /= FVector(1.0, NumPlanarSteps, NumHeightSteps);
			SubBox.Max /= FVector(1.0, NumPlanarSteps, NumHeightSteps);

			FPCGPoint SeedPoint;
			if (SpatialData->SamplePoint(InTransform, InBox, SeedPoint, nullptr))
			{
				// Assuming the the normal to the curve is on the Y axis
				const FVector YAxis = SeedPoint.Transform.GetScaledAxis(EAxis::Y);
				const FVector ZAxis = SeedPoint.Transform.GetScaledAxis(EAxis::Z);

				// TODO: we can optimize this if we are in the "edges only case" to pick only values that
				FVector::FReal CurrentZ = (InBox.Min.Z + ZHalfStep);
				while(CurrentZ <= InBox.Max.Z - ZHalfStep + KINDA_SMALL_NUMBER)
				{
					// Compute inner/outer distance Z contribution (squared value since we'll compare against 1)
					const FVector::FReal InnerZ = ((NumHeightSteps > 1) ? (CurrentZ - FMath::Sign(CurrentZ) * ZHalfStep) : 0);
					const FVector::FReal OuterZ = ((NumHeightSteps > 1) ? (CurrentZ + FMath::Sign(CurrentZ) * ZHalfStep) : 0);

					// TODO: based on the current Z, we can compute the "unit" circle (as seen below) so we don't run outside of it by design, which would be a bit more efficient
					//  care needs to be taken to make sure we are on the same "steps" though
					FVector::FReal CurrentY = (InBox.Min.Y + YHalfStep);
					while(CurrentY <= InBox.Max.Y - YHalfStep + KINDA_SMALL_NUMBER)
					{
						// Compute inner/outer distance Y contribution
						const FVector::FReal InnerY = ((NumPlanarSteps > 1) ? (CurrentY - FMath::Sign(CurrentY) * YHalfStep) : 0);
						const FVector::FReal OuterY = ((NumPlanarSteps > 1) ? (CurrentY + FMath::Sign(CurrentY) * YHalfStep) : 0);

						bool bTestPoint = true;
						const FVector::FReal InnerDistance = FMath::Square((CurrentZ >= 0) ? (InnerZ / InBox.Max.Z) : (InnerZ / InBox.Min.Z)) + FMath::Square((CurrentY >= 0) ? (InnerY / InBox.Max.Y) : (InnerY / InBox.Min.Y));
						const FVector::FReal OuterDistance = FMath::Square((CurrentZ >= 0) ? (OuterZ / InBox.Max.Z) : (OuterZ / InBox.Min.Z)) + FMath::Square((CurrentY >= 0) ? (OuterY / InBox.Max.Y) : (OuterY / InBox.Min.Y));

						// Check if we should consider this point based on the fill mode / the position in the iteration
						// If the normalized z^2 + y^2 > 1, then there's no point in testing this point
						if (InnerDistance >= 1.0 + KINDA_SMALL_NUMBER)
						{
							bTestPoint = false; // fully outside the unit circle
						}
						else if (Fill == EPCGSplineSamplingFill::EdgesOnly && OuterDistance < 1.0 - KINDA_SMALL_NUMBER)
						{
							bTestPoint = false; // Not the edge point
						}

						if (bTestPoint)
						{
							FVector TentativeLocation = InTransform.GetLocation() + YAxis * CurrentY + ZAxis * CurrentZ;

							FTransform TentativeTransform = InTransform;
							TentativeTransform.SetLocation(TentativeLocation);

							FPCGPoint OutPoint;
							if (SpatialData->SamplePoint(TentativeTransform, SubBox, OutPoint, OutPointData->Metadata))
							{
								OutPointData->GetMutablePoints().Add(OutPoint);
							}
						}

						CurrentY += 2.0 * YHalfStep;
					}

					CurrentZ += 2.0 * ZHalfStep;
				}
			}
		}

		EPCGSplineSamplingFill Fill;
		int NumPlanarSteps;
		int NumHeightSteps;
	};

	void SampleLineData(const UPCGPolyLineData* LineData, const UPCGSpatialData* SpatialData, const FPCGSplineSamplerParams& Params, UPCGPointData* OutPointData)
	{
		check(LineData && OutPointData);

		FSubdivisionStepSampler SubdivisionSampler(LineData, Params);
		FDistanceStepSampler DistanceSampler(LineData, Params);

		FStepSampler* Sampler = ((Params.Mode == EPCGSplineSamplingMode::Subdivision) ? static_cast<FStepSampler*>(&SubdivisionSampler) : static_cast<FStepSampler*>(&DistanceSampler));

		FDimensionSampler TrivialDimensionSampler(LineData, SpatialData);
		FVolumeSampler VolumeSampler(LineData, SpatialData, Params);

		FDimensionSampler* ExtentsSampler = ((Params.Dimension == EPCGSplineSamplingDimension::OnSpline) ? &TrivialDimensionSampler : static_cast<FDimensionSampler*>(&VolumeSampler));

		FTransform SeedTransform;

		while (!Sampler->IsDone())
		{
			FBox SeedBox = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector);
			// Get seed transform/box
			Sampler->Step(SeedTransform, SeedBox);
			// From seed point, sample in other dimensions as needed
			ExtentsSampler->Sample(SeedTransform, SeedBox, OutPointData);
		}

		// Finally, set seed on points based on position
		for (FPCGPoint& Point : OutPointData->GetMutablePoints())
		{
			Point.Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition(Point.Transform.GetLocation());
		}
	}

	const UPCGPolyLineData* GetPolyLineData(const UPCGSpatialData* InSpatialData)
	{
		if (!InSpatialData)
		{
			return nullptr;
		}

		if (const UPCGPolyLineData* LineData = Cast<const UPCGPolyLineData>(InSpatialData))
		{
			return LineData;
		}
		else if (const UPCGSplineProjectionData* SplineProjectionData = Cast<const UPCGSplineProjectionData>(InSpatialData))
		{
			return SplineProjectionData->GetSpline();
		}
		else if (const UPCGIntersectionData* Intersection = Cast<const UPCGIntersectionData>(InSpatialData))
		{
			if (const UPCGPolyLineData* IntersectionA = GetPolyLineData(Intersection->A))
			{
				return IntersectionA;
			}
			else if (const UPCGPolyLineData* IntersectionB = GetPolyLineData(Intersection->B))
			{
				return IntersectionB;
			}
		}

		return nullptr;
	}
}

FPCGElementPtr UPCGSplineSamplerSettings::CreateElement() const
{
	return MakeShared<FPCGSplineSamplerElement>();
}

TArray<FPCGPinProperties> UPCGSplineSamplerSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultInputLabel, /*EPCGDataType::Point |*/ EPCGDataType::Spline | EPCGDataType::LandscapeSpline);

	return PinProperties;
}

bool FPCGSplineSamplerElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGSplineSamplerElement::Execute);

	const UPCGSplineSamplerSettings* Settings = Context->GetInputSettings<UPCGSplineSamplerSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputs();
	UPCGParamData* Params = Context->InputData.GetParams();

	FPCGSplineSamplerParams SamplerParams = Settings->Params;
	SamplerParams.Mode = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, Mode, Params);
	SamplerParams.Dimension = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, Dimension, Params);
	SamplerParams.Fill = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, Fill, Params);
	SamplerParams.SubdivisionsPerSegment = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, SubdivisionsPerSegment, Params);
	SamplerParams.DistanceIncrement = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, DistanceIncrement, Params);
	SamplerParams.NumPlanarSubdivisions = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, NumPlanarSubdivisions, Params);
	SamplerParams.NumHeightSubdivisions = PCG_GET_OVERRIDEN_VALUE(&SamplerParams, NumHeightSubdivisions, Params);

	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	for (FPCGTaggedData& Input : Inputs)
	{
		const UPCGSpatialData* SpatialData = Cast<const UPCGSpatialData>(Input.Data);

		if (!SpatialData)
		{
			continue;
		}

		// TODO: do something for point data approximations
		const UPCGPolyLineData* LineData = PCGSplineSampler::GetPolyLineData(SpatialData);

		if (!LineData)
		{
			continue;
		}

		FPCGTaggedData& Output = Outputs.Emplace_GetRef();
		Output = Input;

		UPCGPointData* SampledPointData = NewObject<UPCGPointData>(const_cast<UPCGPolyLineData*>(LineData));
		SampledPointData->InitializeFromData(SpatialData);

		Output.Data = SampledPointData;

		PCGSplineSampler::SampleLineData(LineData, SpatialData, SamplerParams, SampledPointData);
	}

	return true;
}