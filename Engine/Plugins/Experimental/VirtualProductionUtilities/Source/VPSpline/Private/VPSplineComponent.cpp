// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPSplineComponent.h"

#include "VPSpline.h"
#include "VPSplineLog.h"

UVPSplineComponent::UVPSplineComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SplineCurves.Position.Points.Reset(10);
	SplineCurves.Rotation.Points.Reset(10);
	SplineCurves.Scale.Points.Reset(10);
	VPSplineMetadata = ObjectInitializer.CreateDefaultSubobject<UVPSplineMetadata>(this, TEXT("VPSplineMetadata"));
	VPSplineMetadata->Reset(10);
}


USplineMetadata* UVPSplineComponent::GetSplinePointsMetadata()
{
	return VPSplineMetadata;
}

const USplineMetadata* UVPSplineComponent::GetSplinePointsMetadata() const
{
	return VPSplineMetadata;
}

void UVPSplineComponent::PostLoad()
{
	Super::PostLoad();
	if (VPSplineMetadata)
	{
		SynchronizeProperties();
	}
}


TStructOnScope<FActorComponentInstanceData> UVPSplineComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> InstanceData = MakeStructOnScope<FActorComponentInstanceData, FVPSplineInstanceData>(this);
	FVPSplineInstanceData* SplineInstanceData = InstanceData.Cast<FVPSplineInstanceData>();

	if (bSplineHasBeenEdited)
	{
		SplineInstanceData->VPSplineMetadata = VPSplineMetadata;
		SplineInstanceData->SplineCurves = SplineCurves;
	}

	SplineInstanceData->bSplineHasBeenEdited = bSplineHasBeenEdited;

	return InstanceData;
}

void UVPSplineComponent::ApplyComponentInstanceData(FVPSplineInstanceData* SplineInstanceData, const bool bPostUCS)
{
	check(SplineInstanceData);

	if (bPostUCS)
	{
		if (bInputSplinePointsToConstructionScript)
		{
			// Don't reapply the saved state after the UCS has run if we are inputting the points to it.
			// This allows the UCS to work on the edited points and make its own changes.
			return;
		}
		else
		{
			//bModifiedByConstructionScript = (SplineInstanceData->SplineCurvesPreUCS != SplineCurves);

			// If we are restoring the saved state, unmark the SplineCurves property as 'modified'.
			// We don't want to consider that these changes have been made through the UCS.
			TArray<FProperty*> Properties;
			//Properties.Emplace(FindFProperty<FProperty>(UVPSplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UVPSplineComponent, SplineCurves)));
			Properties.Emplace(FindFProperty<FProperty>(UVPSplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UVPSplineComponent, SplineCurves)));
			RemoveUCSModifiedProperties(Properties);
		}
	}
	else
	{
		//SplineInstanceData->SplineCurvesPreUCS = SplineCurves;
	}

	if (SplineInstanceData->bSplineHasBeenEdited)
	{
		// Copy metadata to current component
		if (VPSplineMetadata && SplineInstanceData->VPSplineMetadata)
		{
			VPSplineMetadata->Modify();
			//VPSplineMetadata = SplineInstanceData->VPSplineMetadata;
			UEngine::CopyPropertiesForUnrelatedObjects(SplineInstanceData->VPSplineMetadata, VPSplineMetadata);
		}
	
		bModifiedByConstructionScript = false;
	}

	UpdateSpline();
	SynchronizeProperties();
}

void UVPSplineComponent::SynchronizeProperties()
{
	int32 NumOfPoints = GetNumberOfSplinePoints();
	if (VPSplineMetadata && NumOfPoints > 0)
	{
		VPSplineMetadata->Fixup(NumOfPoints, this);

		// Fixing invalid NormalizedPosition
		// For now, it just finds metadata item with NormalizedPosition less than 0.

		int32 NumOfValidPoints = 0;
		for (int32 Index = 0; Index < NumOfPoints; ++Index)
		{
			float CurrPosition = VPSplineMetadata->NormalizedPosition.Points[Index].OutVal;
			if (CurrPosition >= -0.0001)
			{
				NumOfValidPoints += 1;
			}
		}
		
		if (NumOfPoints - NumOfValidPoints > 0)
		{
			UE_LOG(LogVPSpline, Warning, TEXT("%s: Num Of Invalid Keys: %d"), *GetReadableName(), (NumOfPoints - NumOfValidPoints));
			float PositionIncr = 1.0 / (float)(NumOfPoints-1);

			UE_LOG(LogVPSpline, Warning, TEXT("Updating NormalizedPosition metadata: (Total: %d, Valid: %d, Incr: %f"), NumOfPoints, NumOfValidPoints, PositionIncr);
			for (int32 Index = 0; Index < NumOfPoints; ++Index)
			{
				VPSplineMetadata->NormalizedPosition.Points[Index].OutVal = (float)Index * PositionIncr;
			}
		}
	}
}

void UVPSplineComponent::SetFocalLengthAtSplinePoint(const int32 PointIndex, const float Value)
{
	int32 NumPoints = VPSplineMetadata-> FocalLength.Points.Num();
	check(PointIndex >= 0 && PointIndex < NumPoints);
	VPSplineMetadata->Modify();
	VPSplineMetadata->FocalLength.Points[PointIndex].OutVal = Value;
}

void UVPSplineComponent::SetApertureAtSplinePoint(const int32 PointIndex, const float Value)
{
	int32 NumPoints = VPSplineMetadata->Aperture.Points.Num();
	check(PointIndex >= 0 && PointIndex < NumPoints);
	VPSplineMetadata->Modify();
	VPSplineMetadata->Aperture.Points[PointIndex].OutVal = Value;
}

void UVPSplineComponent::SetFocusDistanceAtSplinePoint(const int32 PointIndex, const float Value)
{
	int32 NumPoints = VPSplineMetadata->FocusDistance.Points.Num();
	check(PointIndex >= 0 && PointIndex < NumPoints);
	VPSplineMetadata->Modify();
	VPSplineMetadata->FocusDistance.Points[PointIndex].OutVal = Value;
}


void UVPSplineComponent::SetNormalizedPositionAtSplinePoint(const int32 PointIndex, const float Value)
{
	int32 NumPoints = VPSplineMetadata->NormalizedPosition.Points.Num();
	check(PointIndex >= 0 && PointIndex < NumPoints);
	VPSplineMetadata->Modify();
	VPSplineMetadata->NormalizedPosition.Points[PointIndex].OutVal = Value;
}

bool UVPSplineComponent::FindSplineDataAtPosition(const float InPosition, int32& OutIndex) const
{
	int32 NumPoints = VPSplineMetadata->NormalizedPosition.Points.Num();
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (FMath::IsNearlyEqual(InPosition, VPSplineMetadata->NormalizedPosition.Points[i].OutVal))
		{
			OutIndex = i;
			return true;
		}
	}
	OutIndex = -1;
	return false;
}

float UVPSplineComponent::GetInputKeyAtPosition(const float InPosition)
{
	float OutValue = 0.0f;
	int32 NumPoints = VPSplineMetadata->NormalizedPosition.Points.Num();
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (InPosition < VPSplineMetadata->NormalizedPosition.Points[i].OutVal)
		{
			if (i > 0)
			{
				float Value0 = VPSplineMetadata->NormalizedPosition.Points[i - 1].OutVal;
				float Value1 = VPSplineMetadata->NormalizedPosition.Points[i].OutVal;
				OutValue = (InPosition - Value0) / (Value1 - Value0) + (float)(i-1);
			}
			break;
		}
		OutValue = (float)i;
	}
	return OutValue;
}

void UVPSplineComponent::UpdateSplineDataAtIndex(const int InIndex, const FVPSplinePointData& InPointData)
{
	ESplinePointType::Type PointType = GetSplinePointType(InIndex);
	SetLocationAtSplinePoint(InIndex, InPointData.Location, ESplineCoordinateSpace::World);
	SetRotationAtSplinePoint(InIndex, InPointData.Rotation, ESplineCoordinateSpace::World);
	SetSplinePointType(InIndex, PointType);
	SetFocalLengthAtSplinePoint(InIndex, InPointData.FocalLength);
	SetApertureAtSplinePoint(InIndex, InPointData.Aperture);
	SetFocusDistanceAtSplinePoint(InIndex, InPointData.FocusDistance);
}

void UVPSplineComponent::AddSplineDataAtPosition(const float InPosition, const FVPSplinePointData& InPointData)
{
	int32 NewIndex = 0;
	int32 NumPoints = VPSplineMetadata->NormalizedPosition.Points.Num();
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (InPosition <= VPSplineMetadata->NormalizedPosition.Points[i].OutVal)
		{
			break;
		}
		NewIndex++;
	}
	AddSplinePointAtIndex(InPointData.Location, NewIndex, ESplineCoordinateSpace::World);
	SetRotationAtSplinePoint(NewIndex, InPointData.Rotation, ESplineCoordinateSpace::World);
	SetFocalLengthAtSplinePoint(NewIndex, InPointData.FocalLength);
	SetApertureAtSplinePoint(NewIndex, InPointData.Aperture);
	SetFocusDistanceAtSplinePoint(NewIndex, InPointData.FocusDistance);
	SetNormalizedPositionAtSplinePoint(NewIndex, InPosition);
	SetSplinePointType(NewIndex, ESplinePointType::Curve);
}


#if WITH_EDITOR
void UVPSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	OnSplineEdited.ExecuteIfBound();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SynchronizeProperties();
}
#endif

void FVPSplineInstanceData::ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase)
{
	if (UVPSplineComponent* SplineComp = CastChecked<UVPSplineComponent>(Component))
	{
		// This ensures there is no stale data causing issues where the spline is marked as read-only even though it shouldn't.
		// There might be a better solution, but this works.
		SplineComp->UpdateSpline();

		Super::ApplyToComponent(Component, CacheApplyPhase);
		SplineComp->ApplyComponentInstanceData(this, (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript));
	}
}

