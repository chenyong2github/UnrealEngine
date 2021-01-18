// Copyright Epic Games, Inc. All Rights Reserved.
#include "SNewLevelInstanceDialog.h"
#include "SEnumCombo.h"
#include "Widgets/Layout/SSpacer.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#define LOCTEXT_NAMESPACE "LevelInstanceEditor"

const FVector2D SNewLevelInstanceDialog::DEFAULT_WINDOW_SIZE = FVector2D(400, 150);

void SNewLevelInstanceDialog::Construct(const FArguments& InArgs)
{
	SelectedCreationType = ELevelInstanceCreationType::LevelInstance;
	SelectedPivotType = ELevelInstancePivotType::CenterMinZ;
	ParentWindowPtr = InArgs._ParentWindow.Get();
	PivotActors = InArgs._PivotActors.Get();
	bClickedOk = false;

	this->ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LevelInstanceCreationTypeTextBlock", "Type"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(2.f)
				[
					SNew(SEnumComboBox, StaticEnum<ELevelInstanceCreationType>())
					.ContentPadding(FCoreStyle::Get().GetMargin("StandardDialog.ContentPadding"))
					.CurrentValue(this, &SNewLevelInstanceDialog::GetSelectedCreationType)
					.OnEnumSelectionChanged(this, &SNewLevelInstanceDialog::OnSelectedCreationTypeChanged)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LevelInstancePivotTypeTextBlock", "Pivot"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(2.f)
				[
					SNew(SEnumComboBox, StaticEnum<ELevelInstancePivotType>())
					.ContentPadding(FCoreStyle::Get().GetMargin("StandardDialog.ContentPadding"))
					.CurrentValue(this, &SNewLevelInstanceDialog::GetSelectedPivotType)
					.OnEnumSelectionChanged(this, &SNewLevelInstanceDialog::OnSelectedPivotTypeChanged)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				.IsEnabled(this, &SNewLevelInstanceDialog::IsPivotActorSelectionEnabled)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LevelInstancePivotActorTextBlock", "Actor"))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(2.f)
				[
					SNew(SComboBox<AActor*>)
					.ContentPadding(FCoreStyle::Get().GetMargin("StandardDialog.ContentPadding"))
					.OptionsSource(&PivotActors)
					.OnGenerateWidget(this, &SNewLevelInstanceDialog::OnGeneratePivotActorWidget)
					.OnSelectionChanged(this, &SNewLevelInstanceDialog::OnSelectedPivotActorChanged)
					[
						SNew(STextBlock)
						.Text(this, &SNewLevelInstanceDialog::GetSelectedPivotActorText)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.IsEnabled(this, &SNewLevelInstanceDialog::IsOkEnabled)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SNewLevelInstanceDialog::OnOkClicked)
					.Text(LOCTEXT("OkButton", "Ok"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.ContentPadding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
					.OnClicked(this, &SNewLevelInstanceDialog::OnCancelClicked)
					.Text(LOCTEXT("CancelButton", "Cancel"))
				]
			]
		]
	];
}

bool SNewLevelInstanceDialog::IsOkEnabled() const
{
	if (SelectedPivotType == ELevelInstancePivotType::Actor && !SelectedPivotActor)
	{
		return false;
	}

	return true;
}

FReply SNewLevelInstanceDialog::OnOkClicked()
{
	bClickedOk = true;
	ParentWindowPtr.Pin()->RequestDestroyWindow();
	return FReply::Handled();
}

FReply SNewLevelInstanceDialog::OnCancelClicked()
{
	bClickedOk = false;
	ParentWindowPtr.Pin()->RequestDestroyWindow();
	return FReply::Handled();
}

int32 SNewLevelInstanceDialog::GetSelectedCreationType() const
{
	return (int32)SelectedCreationType;
}

void SNewLevelInstanceDialog::OnSelectedCreationTypeChanged(int32 NewValue, ESelectInfo::Type SelectionType)
{
	SelectedCreationType = (ELevelInstanceCreationType)NewValue;
}

int32 SNewLevelInstanceDialog::GetSelectedPivotType() const
{
	return (int32)SelectedPivotType;
}

void SNewLevelInstanceDialog::OnSelectedPivotTypeChanged(int32 NewValue, ESelectInfo::Type SelectionType)
{
	SelectedPivotType = (ELevelInstancePivotType)NewValue;
}

TSharedRef<SWidget> SNewLevelInstanceDialog::OnGeneratePivotActorWidget(AActor* Actor) const
{
	// If a row wasn't generated just create the default one, a simple text block of the item's name.
	return SNew(STextBlock).Text(Actor ? FText::FromString(Actor->GetActorLabel()) : LOCTEXT("null", "null"));
}

FText SNewLevelInstanceDialog::GetSelectedPivotActorText() const
{
	return SelectedPivotActor ? FText::FromString(SelectedPivotActor->GetActorLabel()) : LOCTEXT("none", "None");
}

void SNewLevelInstanceDialog::OnSelectedPivotActorChanged(AActor* NewValue, ESelectInfo::Type SelectionType)
{
	SelectedPivotActor = NewValue;
}

bool SNewLevelInstanceDialog::IsPivotActorSelectionEnabled() const
{
	return SelectedPivotType == ELevelInstancePivotType::Actor;
}

#undef LOCTEXT_NAMESPACE
