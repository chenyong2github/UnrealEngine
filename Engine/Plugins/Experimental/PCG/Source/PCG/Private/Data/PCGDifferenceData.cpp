// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGDifferenceData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGUnionData.h"
#include "Helpers/PCGAsync.h"

namespace PCGDifferenceDataUtils
{
	EPCGUnionDensityFunction ToUnionDensityFunction(EPCGDifferenceDensityFunction InDensityFunction)
	{
		return (InDensityFunction == EPCGDifferenceDensityFunction::ClampedSubstraction ? EPCGUnionDensityFunction::ClampedAddition : EPCGUnionDensityFunction::Maximum);
	}
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
	return FBox(EForceInit::ForceInit);
}

float UPCGDifferenceData::GetDensityAtPosition(const FVector& InPosition) const
{
	if (!Source->GetBounds().IsInside(InPosition) ||
		(Difference && Difference->GetStrictBounds().IsInside(InPosition)))
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

		const float DensityInDifference = Difference->GetDensityAtPosition(InPosition);
		return (DensityInDifference > 0 ? 0 : DensityInSource);
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
		TransformedPoint.Density = FMath::Max(0, TransformedPoint.Density - Difference->GetDensityAtPosition(TransformedPoint.Transform.GetLocation()));
	}

	return TransformedPoint;
}

bool UPCGDifferenceData::HasNonTrivialTransform() const
{
	check(Source);
	return Source->HasNonTrivialTransform();
}

const UPCGPointData* UPCGDifferenceData::CreatePointData(FPCGContextPtr Context) const
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
	Data->TargetActor = TargetActor;

	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();
	TArray<FPCGPoint>& TargetPoints = Data->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(Context, SourcePoints.Num(), TargetPoints, [this, &SourcePoints](int32 Index, FPCGPoint& OutPoint)
	{
		const FPCGPoint& Point = SourcePoints[Index];
		const float DensityInDifference = Difference->GetDensityAtPosition(Point.Transform.GetLocation());

		if (DensityInDifference < Point.Density)
		{
			OutPoint = Point;
			OutPoint.Density -= DensityInDifference;
			return true;
		}
#if WITH_EDITORONLY_DATA
		else if (bKeepZeroDensityPoints)
		{
			OutPoint = Point;
			OutPoint.Density = 0;
			return true;
		}
#endif
		else
		{
			return false;
		}
	});

	UE_LOG(LogPCG, Verbose, TEXT("Difference generated %d points from %d source points"), TargetPoints.Num(), SourcePointData->GetPoints().Num());

	return Data;
}