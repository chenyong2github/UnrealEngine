// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGDifferenceData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGUnionData.h"
#include "Helpers/PCGAsync.h"
#include "PCGHelpers.h"
#include "Metadata/PCGMetadataAccessor.h"

namespace PCGDifferenceDataUtils
{
	EPCGUnionDensityFunction ToUnionDensityFunction(EPCGDifferenceDensityFunction InDensityFunction)
	{
		if (InDensityFunction == EPCGDifferenceDensityFunction::ClampedSubstraction)
		{
			return EPCGUnionDensityFunction::ClampedAddition;
		}
		else if (InDensityFunction == EPCGDifferenceDensityFunction::Binary)
		{
			return EPCGUnionDensityFunction::Binary;
		}
		else
		{
			return EPCGUnionDensityFunction::Maximum;
		}
	}
}

float UPCGDifferenceData::GetDensityAtPositionFromDifference(const FVector& InPosition) const
{
	const float DensityInDifference = (Difference ? Difference->GetDensityAtPosition(InPosition) : 0.0f);
	const bool bBinaryDensity = (DensityFunction == EPCGDifferenceDensityFunction::Binary);
	return ((bBinaryDensity && DensityInDifference > 0) ? 1.0f : DensityInDifference);
}

void UPCGDifferenceData::Initialize(const UPCGSpatialData* InData)
{
	check(InData);
	Source = InData;
	TargetActor = InData->TargetActor;
}

void UPCGDifferenceData::AddDifference(const UPCGSpatialData* InDifference)
{
	check(InDifference);

	// In the eventuality that the difference has no overlap with the source, then we can drop it directly
	if (!GetBounds().Intersect(InDifference->GetBounds()))
	{
		return;
	}

	// First difference element we'll keep as is, but subsequent ones will be pushed into a union
	if (!Difference)
	{
		Difference = InDifference;
	}
	else
	{
		if (!DifferencesUnion)
		{
			DifferencesUnion = NewObject<UPCGUnionData>(this);
			DifferencesUnion->AddData(Difference);
			DifferencesUnion->SetDensityFunction(PCGDifferenceDataUtils::ToUnionDensityFunction(DensityFunction));
			Difference = DifferencesUnion;
		}

		check(Difference == DifferencesUnion);
		DifferencesUnion->AddData(InDifference);
	}
}

void UPCGDifferenceData::SetDensityFunction(EPCGDifferenceDensityFunction InDensityFunction)
{
	DensityFunction = InDensityFunction;

	if (DifferencesUnion)
	{
		DifferencesUnion->SetDensityFunction(PCGDifferenceDataUtils::ToUnionDensityFunction(DensityFunction));
	}
}

#if WITH_EDITOR
void UPCGDifferenceData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGDifferenceData, DensityFunction))
	{
		SetDensityFunction(DensityFunction);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

int UPCGDifferenceData::GetDimension() const
{
	return Source->GetDimension();
}

FBox UPCGDifferenceData::GetBounds() const
{
	return Source->GetBounds();
}

FBox UPCGDifferenceData::GetStrictBounds() const
{
	return Difference ? FBox(EForceInit::ForceInit) : Source->GetStrictBounds();
}

float UPCGDifferenceData::GetDensityAtPosition(const FVector& InPosition) const
{
	if(!PCGHelpers::IsInsideBounds(Source->GetBounds(), InPosition) ||
		(Difference && PCGHelpers::IsInsideBounds(Difference->GetStrictBounds(), InPosition)))
	{
		return 0;
	}
	else if(Difference)
	{
		const float DensityInSource = Source->GetDensityAtPosition(InPosition);
		if (DensityInSource == 0)
		{
			return 0;
		}

		const float DensityInDifference = GetDensityAtPositionFromDifference(InPosition);
		return FMath::Max(0, DensityInSource - DensityInDifference);
	}
	else
	{
		return Source->GetDensityAtPosition(InPosition);
	}
}

FVector UPCGDifferenceData::TransformPosition(const FVector& InPosition) const
{
	check(Source);
	return Source->TransformPosition(InPosition);
}

FPCGPoint UPCGDifferenceData::TransformPoint(const FPCGPoint& InPoint) const
{
	check(Source);
	FPCGPoint TransformedPoint = Source->TransformPoint(InPoint);

	if (Difference && TransformedPoint.Density > 0)
	{
		TransformedPoint.Density = FMath::Max(0, TransformedPoint.Density - GetDensityAtPositionFromDifference(TransformedPoint.Transform.GetLocation()));
	}

	return TransformedPoint;
}

bool UPCGDifferenceData::GetPointAtPosition(const FVector& InPosition, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	check(Source);

	FPCGPoint PointFromSource;
	if (!Source->GetPointAtPosition(InPosition, PointFromSource, OutMetadata))
	{
		return false;
	}

	OutPoint = PointFromSource;

	FPCGPoint PointFromDiff;
	if (Difference && Difference->GetPointAtPosition(PointFromSource.Transform.GetLocation(), PointFromDiff, OutMetadata))
	{
		const bool bBinaryDensity = (DensityFunction == EPCGDifferenceDensityFunction::Binary);
		
		// Apply difference
		OutPoint.Density = bBinaryDensity ? 0 : FMath::Max(0, PointFromSource.Density - PointFromDiff.Density);
		// Color?
		if (OutMetadata && OutPoint.Density > 0)
		{
			OutMetadata->MergePointAttributes(PointFromSource, PointFromDiff, OutPoint, EPCGMetadataOp::Sub);
		}

		return OutPoint.Density > 0;
	}
	else
	{
		return true;
	}
}

bool UPCGDifferenceData::HasNonTrivialTransform() const
{
	check(Source);
	return Source->HasNonTrivialTransform();
}

const UPCGPointData* UPCGDifferenceData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGDifferenceData::CreatePointData);
	
	// This is similar to what we are doing in UPCGUnionData::CreatePointData
	const UPCGPointData* SourcePointData = Source->ToPointData(Context);

	if (!SourcePointData)
	{
		UE_LOG(LogPCG, Error, TEXT("Difference unable to get source points"));
		return SourcePointData;
	}

	if (!Difference)
	{
		UE_LOG(LogPCG, Verbose, TEXT("Difference is trivial"));
		return SourcePointData;
	}

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGDifferenceData*>(this));
	Data->InitializeFromData(this, SourcePointData->Metadata);

	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();
	TArray<FPCGPoint>& TargetPoints = Data->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(Context, SourcePoints.Num(), TargetPoints, [this, Data, SourcePointData, &SourcePoints](int32 Index, FPCGPoint& OutPoint)
	{
		const FPCGPoint& Point = SourcePoints[Index];

		FPCGPoint PointFromDiff;
		if (Difference && Difference->GetPointAtPosition(Point.Transform.GetLocation(), PointFromDiff, Data->Metadata))
		{
			const bool bBinaryDensity = (DensityFunction == EPCGDifferenceDensityFunction::Binary);

			OutPoint = Point;
			UPCGMetadataAccessorHelpers::InitializeMetadata(OutPoint, Data->Metadata, Point);
			OutPoint.Density = bBinaryDensity ? 0 : FMath::Max(0, Point.Density - PointFromDiff.Density);

			if (Data->Metadata && OutPoint.Density > 0)
			{
				Data->Metadata->MergePointAttributes(Point, SourcePointData->Metadata, PointFromDiff, Data->Metadata, OutPoint, EPCGMetadataOp::Sub);
			}

#if WITH_EDITORONLY_DATA
			return OutPoint.Density > 0 || bKeepZeroDensityPoints;
#else
			return OutPoint.Density > 0;
#endif
		}
		else
		{
			OutPoint = Point;
			UPCGMetadataAccessorHelpers::InitializeMetadata(OutPoint, Data->Metadata, Point);
			return true;
		}
	});

	UE_LOG(LogPCG, Verbose, TEXT("Difference generated %d points from %d source points"), TargetPoints.Num(), SourcePointData->GetPoints().Num());

	return Data;
}