// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMSourceSelector.h"

#include "Algo/Transform.h"
#include "SSimpleButton.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SMVVMSourceEntry.h"

#define LOCTEXT_NAMESPACE "MVVMSourceSelector"

namespace UE::MVVM
{

void SSourceSelector::Construct(const FArguments& Args)
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
				return SNew(SSourceEntry)
					.TextStyle(TextStyle)
					.Source(Source);
			})
			.OnSelectionChanged(this, &SSourceSelector::OnComboBoxSelectionChanged)
			.InitiallySelectedItem(SelectedSource)
			[
				SAssignNew(SelectedSourceWidget, SSourceEntry)
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
			.Visibility(this, &SSourceSelector::GetClearVisibility)
			.OnClicked(this, &SSourceSelector::OnClearSource)
		]
	];

	OnSelectionChanged = Args._OnSelectionChanged;
}

void SSourceSelector::OnComboBoxSelectionChanged(FBindingSource Selected, ESelectInfo::Type SelectionType)
{
	SelectedSource = Selected;

	SelectedSourceWidget->RefreshSource(Selected);

	OnSelectionChanged.ExecuteIfBound(Selected);
}

void SSourceSelector::Refresh()
{
	SelectedSource = SelectedSourceAttribute.Get();
	AvailableSources = AvailableSourcesAttribute.Get();

	if (SourceComboBox.IsValid())
	{
		SourceComboBox->RefreshOptions();
		SourceComboBox->SetSelectedItem(SelectedSource);
	}
}

EVisibility SSourceSelector::GetClearVisibility() const
{
	return SelectedSource.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SSourceSelector::OnClearSource()
{
	if (SourceComboBox.IsValid())
	{
		SourceComboBox->ClearSelection();
	}

	return FReply::Handled();
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
