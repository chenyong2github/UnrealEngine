// Copyright Epic Games, Inc. All Rights Reserved.
#include "TimeOfDayActorDetails.h"
#include "TimeOfDayActor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Editor.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Algo/Transform.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "TimeOfDayActorDetails"

TSharedRef<IDetailCustomization> FTimeOfDayActorDetails::MakeInstance()
{
	return MakeShared<FTimeOfDayActorDetails>();
}

void AddAllSubObjectProperties(TArray<UObject*>& SubObjects, IDetailCategoryBuilder& Category, TAttribute<EVisibility> Visibility = TAttribute<EVisibility>(EVisibility::Visible))
{
	SubObjects.Remove(nullptr);
	if (!SubObjects.Num())
	{
		return;
	}

	for (const FProperty* TestProperty : TFieldRange<FProperty>(SubObjects[0]->GetClass()))
	{
		if (TestProperty->HasAnyPropertyFlags(CPF_Edit))
		{
			const bool bAdvancedDisplay = TestProperty->HasAnyPropertyFlags(CPF_AdvancedDisplay);
			const EPropertyLocation::Type PropertyLocation = bAdvancedDisplay ? EPropertyLocation::Advanced : EPropertyLocation::Common;

			IDetailPropertyRow* NewRow = Category.AddExternalObjectProperty(SubObjects, TestProperty->GetFName(), PropertyLocation);
			if (NewRow)
			{
				NewRow->Visibility(Visibility);
			}
		}
	}
}

void FTimeOfDayActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = DetailLayout.GetSelectedObjects();
	for (int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex)
	{
		const TWeakObjectPtr<UObject>& CurrentObject = SelectedObjects[ObjectIndex];
		if (CurrentObject.IsValid())
		{
			if (ATimeOfDayActor* CurrentTimeOfDayActor = Cast<ATimeOfDayActor>(CurrentObject.Get()))
			{
				TimeOfDayActor = CurrentTimeOfDayActor;
				break;
			}
		}
	}

	TArray<ATimeOfDayActor*> TimeOfDayActors;
	{
		TArray<TWeakObjectPtr<>> ObjectPtrs;
		DetailLayout.GetObjectsBeingCustomized(ObjectPtrs);

		for (TWeakObjectPtr<> WeakObj : ObjectPtrs)
		{
			if (ATimeOfDayActor* Actor = Cast<ATimeOfDayActor>(WeakObj.Get()))
			{
				TimeOfDayActors.Add(Actor);
			}
		}
	}
	
	DetailLayout.HideProperty("DefaultComponents");

	IDetailCategoryBuilder& GeneralCategory = DetailLayout.EditCategory( "General", NSLOCTEXT("GeneralDetails", "General", "General"), ECategoryPriority::Important );

	GeneralCategory.AddCustomRow( NSLOCTEXT("TimeOfDayActorDetails", "OpenLevelSequence", "Open Level Sequence") )
	.RowTag("OpenLevelSequence")
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(0, 5, 10, 5)
		[
			SNew(SButton)
			.ContentPadding(3)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.IsEnabled( this, &FTimeOfDayActorDetails::CanOpenLevelSequenceForActor )
			.OnClicked( this, &FTimeOfDayActorDetails::OnOpenLevelSequenceForActor )
			.Text( NSLOCTEXT("TimeOfDayActorDetails", "OpenLevelSequence", "Open Level Sequence") )
		]
	];

	TArray<UObject*> SubObjects;

	IDetailCategoryBuilder& BindingOverridesCategory = DetailLayout.EditCategory( "BindingOverrides", LOCTEXT("BindingOverrides", "Binding Overrides") ).InitiallyCollapsed(false);
	{
		SubObjects.Reset();
		Algo::Transform(TimeOfDayActors, SubObjects, &ATimeOfDayActor::BindingOverrides);
		AddAllSubObjectProperties(SubObjects, BindingOverridesCategory);
	}
}

bool FTimeOfDayActorDetails::CanOpenLevelSequenceForActor() const
{
	if (TimeOfDayActor.IsValid())
	{
		return TimeOfDayActor.Get()->GetDaySequence() != nullptr;
	}
	return false;
}

FReply FTimeOfDayActorDetails::OnOpenLevelSequenceForActor() const
{
	if (TimeOfDayActor.IsValid())
	{
		if (UObject* LoadedObject = TimeOfDayActor.Get()->GetDaySequence())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(LoadedObject);
		}
	}
	return FReply::Handled();
}



#undef LOCTEXT_NAMESPACE
