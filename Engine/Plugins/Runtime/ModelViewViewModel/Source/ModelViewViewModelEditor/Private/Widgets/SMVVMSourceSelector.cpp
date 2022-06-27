// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMSourceSelector.h"

#include "Algo/Transform.h"
#include "SSimpleButton.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SMVVMSourceEntry.h"

#define LOCTEXT_NAMESPACE "MVVMSourceSelector"

using namespace UE::MVVM;

void SMVVMSourceSelector::Construct(const FArguments& Args)
{
	TextStyle = Args._TextStyle;
	AvailableSourcesAttribute = Args._AvailableSources;
	check(AvailableSourcesAttribute.IsSet());
	SelectedSourceAttribute = Args._SelectedSource;
	check(SelectedSourceAttribute.IsSet());

	Refresh();

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SAssignNew(SourceComboBox, SComboBox<FBindingSource>)
			.OptionsSource(&AvailableSources)
			.OnGenerateWidget_Lambda([this](FBindingSource Source)
			{
				return SNew(SMVVMSourceEntry)
					.TextStyle(TextStyle)
					.Source(Source);
			})
			.OnSelectionChanged(this, &SMVVMSourceSelector::OnComboBoxSelectionChanged)
			.InitiallySelectedItem(SelectedSource)
			[
				SAssignNew(SelectedSourceWidget, SMVVMSourceEntry)
				.TextStyle(TextStyle)
				.Source(SelectedSource)
			]
		]
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SSimpleButton)
			.Icon(FAppStyle::Get().GetBrush("Icons.X"))
			.ToolTipText(LOCTEXT("ClearField", "Clear source selection."))
			.Visibility(this, &SMVVMSourceSelector::GetClearVisibility)
			.OnClicked(this, &SMVVMSourceSelector::OnClearSource)
		]
	];

	OnSelectionChanged = Args._OnSelectionChanged;
}

void SMVVMSourceSelector::OnComboBoxSelectionChanged(FBindingSource Selected, ESelectInfo::Type SelectionType)
{
	SelectedSource = Selected;

	SelectedSourceWidget->RefreshSource(Selected);

	OnSelectionChanged.ExecuteIfBound(Selected);
}

void SMVVMSourceSelector::Refresh()
{
	SelectedSource = SelectedSourceAttribute.Get();
	AvailableSources = AvailableSourcesAttribute.Get();

	if (SourceComboBox.IsValid())
	{
		SourceComboBox->RefreshOptions();
		SourceComboBox->SetSelectedItem(SelectedSource);
	}
}

EVisibility SMVVMSourceSelector::GetClearVisibility() const
{
	return SelectedSource.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SMVVMSourceSelector::OnClearSource()
{
	if (SourceComboBox.IsValid())
	{
		SourceComboBox->ClearSelection();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
