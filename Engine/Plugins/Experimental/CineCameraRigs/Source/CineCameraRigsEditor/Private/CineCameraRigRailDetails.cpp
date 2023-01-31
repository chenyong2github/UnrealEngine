// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineCameraRigRailDetails.h"
#include "CineCameraRigRail.h"
#include "CameraRig_Rail.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "PropertyHandle.h"
#include "UObject/PropertyIterator.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Internationalization/Internationalization.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FCineCameraRigRailDetails"

void FCineCameraRigRailDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	if (Objects.Num() != 1)
	{
		return;
	}

	ACineCameraRigRail* RigRailActor = Cast<ACineCameraRigRail>(Objects[0].Get());
	if(RigRailActor == nullptr)
	{
		return;
	}
	RigRailActorPtr = RigRailActor;

	TSharedRef<IPropertyHandle> CustomPositionPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ACineCameraRigRail, CustomPosition));
	TSharedRef<IPropertyHandle> UseCustomPositionPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ACineCameraRigRail, bUseCustomPosition));
	TSharedRef<IPropertyHandle> OrigPositionPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ACameraRig_Rail, CurrentPositionOnRail), ACameraRig_Rail::StaticClass());

	const TAttribute<bool> EditCondition = TAttribute<bool>::Create([this, UseCustomPositionPropertyHandle]()
	{
		bool bCond = false;
		UseCustomPositionPropertyHandle->GetValue(bCond);
		return !bCond;
	});

	DetailBuilder.EditDefaultProperty(OrigPositionPropertyHandle)->EditCondition(EditCondition, nullptr);


	if (IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(CustomPositionPropertyHandle))
	{
		Row->CustomWidget()
		.NameContent()
		[
			CustomPositionPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(125.0f)
		[
			SNew(SNumericEntryBox<float>)
			.ToolTipText(LOCTEXT("CurrentPostionToolTip","Postion property using custom parameterization"))
			.AllowSpin(true)
			.MinSliderValue(this, &FCineCameraRigRailDetails::GetCustomPositionSliderMinValue)
			.MaxSliderValue(this, &FCineCameraRigRailDetails::GetCustomPositionSliderMaxValue)
			.Value(this, &FCineCameraRigRailDetails::GetCustomPosition)
			.OnValueChanged(this, &FCineCameraRigRailDetails::OnCustomPositionChanged)
			.OnValueCommitted(this, &FCineCameraRigRailDetails::OnCustomPositionCommitted)
			.OnBeginSliderMovement(this, &FCineCameraRigRailDetails::OnBeginCustomPositionSliderMovement)
			.OnEndSliderMovement(this, &FCineCameraRigRailDetails::OnEndCustomPositionSliderMovement)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	}

}

void FCineCameraRigRailDetails::OnCustomPositionChanged(float NewValue)
{
	if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
	{
		RigRailActor->CustomPosition = NewValue;
		static FProperty* CustomPositionProperty = FindFProperty<FProperty>(ACineCameraRigRail::StaticClass(), GET_MEMBER_NAME_CHECKED(ACineCameraRigRail, CustomPosition));
		FPropertyChangedEvent SetValueEvent(CustomPositionProperty);
		RigRailActor->PostEditChangeProperty(SetValueEvent);
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
	}
}

void FCineCameraRigRailDetails::OnCustomPositionCommitted(float NewValue, ETextCommit::Type CommitType)
{
	if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCustomPosition", "Set rig rail custom position"));
		RigRailActor->SetFlags(RF_Transactional);
		RigRailActor->Modify();
		RigRailActor->CustomPosition = NewValue;
		static FProperty* CustomPositionProperty = FindFProperty<FProperty>(ACineCameraRigRail::StaticClass(), GET_MEMBER_NAME_CHECKED(ACineCameraRigRail, CustomPosition));
		FPropertyChangedEvent SetValueEvent(CustomPositionProperty);
		RigRailActor->PostEditChangeProperty(SetValueEvent);
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(true);
		}
	}
}

void FCineCameraRigRailDetails::OnBeginCustomPositionSliderMovement()
{
	if (!bCustomPositionSliderStartedTransaction)
	{
		if (GEditor)
		{
			bCustomPositionSliderStartedTransaction = true;
			GEditor->BeginTransaction(LOCTEXT("CustomPositionSliderTransaction", "Set rig rail custom position via slider"));
			if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
			{
				RigRailActor->SetFlags(RF_Transactional);
				RigRailActor->Modify();
			}
		}
	}
}

void FCineCameraRigRailDetails::OnEndCustomPositionSliderMovement(float NewValue)
{
	if (bCustomPositionSliderStartedTransaction)
	{
		if (GEditor)
		{
			GEditor->EndTransaction();
			bCustomPositionSliderStartedTransaction = false;
		}
	}
}

TOptional<float> FCineCameraRigRailDetails::GetCustomPosition() const
{
	if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
	{
		return RigRailActor->CustomPosition;
	}
	return 0.0f;
}

TOptional<float> FCineCameraRigRailDetails::GetCustomPositionSliderMinValue() const
{

	float MinValue = 1.0f;
	if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
	{
		UCineSplineComponent* SplineComp = RigRailActor->GetCineSplineComponent();
		const UCineSplineMetadata* MetaData = Cast<UCineSplineMetadata>(SplineComp->GetSplinePointsMetadata());
		MinValue = MetaData->CustomPosition.Points[0].OutVal;
	}
	return MinValue;
}

TOptional<float> FCineCameraRigRailDetails::GetCustomPositionSliderMaxValue() const
{

	float MaxValue = 5.0f;
	if (ACineCameraRigRail* RigRailActor = RigRailActorPtr.Get())
	{
		UCineSplineComponent* SplineComp = RigRailActor->GetCineSplineComponent();
		const UCineSplineMetadata* MetaData = Cast<UCineSplineMetadata>(SplineComp->GetSplinePointsMetadata());
		int32 NumPoints = MetaData->CustomPosition.Points.Num();
		MaxValue = MetaData->CustomPosition.Points[NumPoints - 1].OutVal;
	}
	return MaxValue;
}
#undef LOCTEXT_NAMESPACE