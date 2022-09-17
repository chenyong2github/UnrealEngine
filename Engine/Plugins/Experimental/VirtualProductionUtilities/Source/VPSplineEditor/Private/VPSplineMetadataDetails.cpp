// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPSplineMetadataDetails.h"
#include "VPSplineMetadata.h"

#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Internationalization/Internationalization.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "ScopedTransaction.h"
#include "ComponentVisualizer.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FVPSplineMetadataDetails"

UClass* UVPSplineMetadataDetailsFactory::GetMetadataClass() const
{
	return UVPSplineMetadata::StaticClass();
}

TSharedPtr<ISplineMetadataDetails> UVPSplineMetadataDetailsFactory::Create()
{
	return MakeShared<FVPSplineMetadataDetails>();
}

FText FVPSplineMetadataDetails::GetDisplayName() const
{
	return LOCTEXT("VPSplineMetadataDetails", "VPSpline");
}

template<class T>
bool UpdateMultipleValue(TOptional<T>& CurrentValue, T InValue)
{
	if (!CurrentValue.IsSet())
	{
		CurrentValue = InValue;
	}
	else if (CurrentValue.IsSet() && CurrentValue.GetValue() != InValue)
	{
		CurrentValue.Reset();
		return false;
	}

	return true;
}

void FVPSplineMetadataDetails::Update(USplineComponent* InSplineComponent, const TSet<int32>& InSelectedKeys)
{
	SplineComp = InSplineComponent;
	SelectedKeys = InSelectedKeys;
	NormalizedPositionValue.Reset();
	FocalLengthValue.Reset();
	ApertureValue.Reset();
	FocusDistanceValue.Reset();
	

	if (IsValid(InSplineComponent))
	{
		bool bUpdateNormalizedPosition = true;
		bool bUpdateFocalLength = true;
		bool bUpdateAperture = true;
		bool bUpdateFocusDistance = true;
		

		UVPSplineMetadata* Metadata = Cast<UVPSplineMetadata>(InSplineComponent->GetSplinePointsMetadata());
		if (Metadata)
		{
			for (int32 Index : InSelectedKeys)
			{
				if (bUpdateNormalizedPosition)
				{
					bUpdateNormalizedPosition = UpdateMultipleValue(NormalizedPositionValue, Metadata->NormalizedPosition.Points[Index].OutVal);
				}

				if (bUpdateFocalLength)
				{
					bUpdateFocalLength = UpdateMultipleValue(FocalLengthValue, Metadata->FocalLength.Points[Index].OutVal);
				}

				if (bUpdateAperture)
				{
					bUpdateAperture = UpdateMultipleValue(ApertureValue, Metadata->Aperture.Points[Index].OutVal);
				}

				if (bUpdateFocusDistance)
				{
					bUpdateFocusDistance = UpdateMultipleValue(FocusDistanceValue, Metadata->FocusDistance.Points[Index].OutVal);
				}
			}
		}
	}
}

template<class T>
void SetValues(FVPSplineMetadataDetails& Details, TArray<FInterpCurvePoint<T>>& Points, const T& NewValue)
{
	Details.SplineComp->GetSplinePointsMetadata()->Modify();
	for (int32 Index : Details.SelectedKeys)
	{
		Points[Index].OutVal = NewValue;
	}

	Details.SplineComp->UpdateSpline();
	Details.SplineComp->bSplineHasBeenEdited = true;
	static FProperty* SplineCurvesProperty = FindFProperty<FProperty>(USplineComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USplineComponent, SplineCurves));
	FComponentVisualizer::NotifyPropertyModified(Details.SplineComp, SplineCurvesProperty);
	Details.Update(Details.SplineComp, Details.SelectedKeys);

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(true);
	}
}

void FVPSplineMetadataDetails::OnBeginSliderMovement()
{
	EditSliderValueTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditVPSplineProperty", "Edit VPSpline Property"));
}

void FVPSplineMetadataDetails::OnEndSliderMovement(float NewValue)
{
	EditSliderValueTransaction.Reset();
}

UVPSplineMetadata* FVPSplineMetadataDetails::GetMetadata() const
{
	return SplineComp ? Cast<UVPSplineMetadata>(SplineComp->GetSplinePointsMetadata()) : nullptr;
}

void FVPSplineMetadataDetails::OnSetNormalizedPosition(float NewValue, ETextCommit::Type CommitInfo)
{
	if (UVPSplineMetadata* Metadata = GetMetadata())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetNormalizedPosition", "Set spline point normalized position data"));
		SetValues<float>(*this, Metadata->NormalizedPosition.Points, NewValue);
	}
}

void FVPSplineMetadataDetails::OnSetFocalLength(float NewValue, ETextCommit::Type CommitInfo)
{
	if (UVPSplineMetadata* Metadata = GetMetadata())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetFocalLength", "Set spline point focal length data"));
		SetValues<float>(*this, Metadata->FocalLength.Points, NewValue);
	}
}

void FVPSplineMetadataDetails::OnSetAperture(float NewValue, ETextCommit::Type CommitInfo)
{
	if (UVPSplineMetadata* Metadata = GetMetadata())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetAperture", "Set spline point aperture data"));
		SetValues<float>(*this, Metadata->Aperture.Points, NewValue);
	}
}

void FVPSplineMetadataDetails::OnSetFocusDistance(float NewValue, ETextCommit::Type CommitInfo)
{
	if (UVPSplineMetadata* Metadata = GetMetadata())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetFocusDistance", "Set spline point focus distance data"));
		SetValues<float>(*this, Metadata->FocusDistance.Points, NewValue);
	}
}

void FVPSplineMetadataDetails::GenerateChildContent(IDetailGroup& DetailGroup)
{
	DetailGroup.AddWidgetRow()
		.RowTag("NormalizedPosition")
		.Visibility(TAttribute<EVisibility>(this, &FVPSplineMetadataDetails::IsEnabled))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NormalizedPosition", "Normalized Position"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	.ValueContent()
		.MinDesiredWidth(125.0f)
		.MaxDesiredWidth(125.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(this, &FVPSplineMetadataDetails::GetNormalizedPosition)
			.AllowSpin(true)
			.MinValue(0.0f)
			.MaxValue(1.0f)
			.MinSliderValue(0.0f)
			.MaxSliderValue(1.0f)
			.OnBeginSliderMovement(this, &FVPSplineMetadataDetails::OnBeginSliderMovement)
			.OnEndSliderMovement(this, &FVPSplineMetadataDetails::OnEndSliderMovement)
			.UndeterminedString(LOCTEXT("Multiple", "Multiple"))
			.OnValueCommitted(this, &FVPSplineMetadataDetails::OnSetNormalizedPosition)
			.OnValueChanged(this, &FVPSplineMetadataDetails::OnSetNormalizedPosition, ETextCommit::Default)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	DetailGroup.AddWidgetRow()
		.RowTag("FocalLength")
		.Visibility(TAttribute<EVisibility>(this, &FVPSplineMetadataDetails::IsEnabled))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FocalLength", "Focal Length"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	.ValueContent()
		.MinDesiredWidth(125.0f)
		.MaxDesiredWidth(125.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(this, &FVPSplineMetadataDetails::GetFocalLength)
			.AllowSpin(true)
			.OnBeginSliderMovement(this, &FVPSplineMetadataDetails::OnBeginSliderMovement)
			.OnEndSliderMovement(this, &FVPSplineMetadataDetails::OnEndSliderMovement)
			.MinValue(0.0f)
			// Because we have no upper limit in MaxSliderValue, we need to "unspecify" the max value here, otherwise the spinner has a limited range,
			//  with TNumericLimits<NumericType>::Max() as the MaxValue and the spinning increment is huge
			.MaxValue(TOptional<float>())
			.MinSliderValue(0.0f)
			.MaxSliderValue(TOptional<float>()) // No upper limit
			.UndeterminedString(LOCTEXT("Multiple", "Multiple"))
			.OnValueCommitted(this, &FVPSplineMetadataDetails::OnSetFocalLength)
			.OnValueChanged(this, &FVPSplineMetadataDetails::OnSetFocalLength, ETextCommit::Default)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	DetailGroup.AddWidgetRow()
		.RowTag("Aperture")
		.Visibility(TAttribute<EVisibility>(this, &FVPSplineMetadataDetails::IsEnabled))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Aperture", "Aperture"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(125.0f)
		.MaxDesiredWidth(125.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(this, &FVPSplineMetadataDetails::GetAperture)
			.AllowSpin(true)
			.OnBeginSliderMovement(this, &FVPSplineMetadataDetails::OnBeginSliderMovement)
			.OnEndSliderMovement(this, &FVPSplineMetadataDetails::OnEndSliderMovement)
			.MinValue(TOptional<float>())
			.MaxValue(TOptional<float>())
			.MinSliderValue(TOptional<float>()) // No lower limit
			.MaxSliderValue(TOptional<float>()) // No upper limit
			.UndeterminedString(LOCTEXT("Multiple", "Multiple"))
			.OnValueCommitted(this, &FVPSplineMetadataDetails::OnSetAperture)
			.OnValueChanged(this, &FVPSplineMetadataDetails::OnSetAperture, ETextCommit::Default)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	DetailGroup.AddWidgetRow()
		.RowTag("FocusDistance")
		.Visibility(TAttribute<EVisibility>(this, &FVPSplineMetadataDetails::IsEnabled))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FocusDistance", "Focus Distance"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(125.0f)
		.MaxDesiredWidth(125.0f)
		[
			SNew(SNumericEntryBox<float>)
			.Value(this, &FVPSplineMetadataDetails::GetFocusDistance)
			.AllowSpin(true)
			.OnBeginSliderMovement(this, &FVPSplineMetadataDetails::OnBeginSliderMovement)
			.OnEndSliderMovement(this, &FVPSplineMetadataDetails::OnEndSliderMovement)
			.MinValue(TOptional<float>())
			.MaxValue(TOptional<float>())
			.MinSliderValue(TOptional<float>()) // No lower limit
			.MaxSliderValue(TOptional<float>()) // No upper limit
			.UndeterminedString(LOCTEXT("Multiple", "Multiple"))
			.OnValueCommitted(this, &FVPSplineMetadataDetails::OnSetFocusDistance)
			.OnValueChanged(this, &FVPSplineMetadataDetails::OnSetFocusDistance, ETextCommit::Default)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

#undef LOCTEXT_NAMESPACE