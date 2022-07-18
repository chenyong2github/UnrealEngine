// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMSourceSelector.h"

#include "Algo/Transform.h"
#include "Editor.h"
#include "MVVMEditorSubsystem.h"
#include "SSimpleButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SMVVMSourceEntry.h"

#define LOCTEXT_NAMESPACE "MVVMSourceSelector"

namespace UE::MVVM
{

void SSourceSelector::Construct(const FArguments& Args, const UWidgetBlueprint* InWidgetBlueprint)
{
	TextStyle = Args._TextStyle;
	SelectedSourceAttribute = Args._SelectedSource;
	check(SelectedSourceAttribute.IsSet());

	WidgetBlueprint = InWidgetBlueprint;
	check(InWidgetBlueprint);

	bAutoRefresh = Args._AutoRefresh;
	bViewModels = Args._ViewModels;
	bShowClear = Args._ShowClear;
	
	Refresh();

	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SAssignNew(MenuAnchor, SMenuAnchor)
			.OnGetMenuContent(this, &SSourceSelector::OnGetMenuContent)
			[
				SNew(SButton)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton").ButtonStyle)
				.OnClicked_Lambda([this]() 
				{
					MenuAnchor->SetIsOpen(true); 
					return FReply::Handled(); 
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SAssignNew(SelectedSourceWidget, SSourceEntry)
						.Source(SelectedSource)
						.TextStyle(TextStyle)
					]
					+ SHorizontalBox::Slot()
					.Padding(2, 0)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.ChevronDown"))
					]
				]
			]
		];

	if (bShowClear)
	{
		HorizontalBox->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SSimpleButton)
				.Icon(FAppStyle::Get().GetBrush("Icons.X"))
				.ToolTipText(LOCTEXT("ClearField", "Clear source selection."))
				.Visibility(this, &SSourceSelector::GetClearVisibility)
				.OnClicked(this, &SSourceSelector::OnClearSource)
			];
	}

	ChildSlot
	[
		HorizontalBox
	];

	OnSelectionChangedDelegate = Args._OnSelectionChanged;
}

TSharedRef<SWidget> SSourceSelector::OnGetMenuContent()
{
	TSharedRef<SBox> TopLevelBox = SNew(SBox)
		.MinDesiredWidth(200)
		.MinDesiredHeight(400);

	if (bViewModels)
	{
		ViewModelList = SNew(SListView<FBindingSource>)
			.OnGenerateRow_Lambda([this](FBindingSource Source, TSharedRef<STableViewBase> OwnerTable)
				{
					return SNew(STableRow<FBindingSource>, OwnerTable)
					[
						SNew(SSourceEntry)
						.Source(Source)
						.TextStyle(TextStyle)
					];
				})
			.OnSelectionChanged(this, &SSourceSelector::OnViewModelSelectionChanged)
			.SelectionMode(ESelectionMode::Single)
			.ListItemsSource(&ViewModelSources);

		TopLevelBox->SetContent(ViewModelList.ToSharedRef());
	}
	else
	{
		WidgetHierarchy = SNew(SReadOnlyHierarchyView, WidgetBlueprint.Get())
			.OnSelectionChanged(this, &SSourceSelector::OnWidgetSelectionChanged)
			.ShowSearch(false);

		TopLevelBox->SetContent(WidgetHierarchy.ToSharedRef());
	}

	return TopLevelBox;
}

void SSourceSelector::OnViewModelSelectionChanged(FBindingSource Selected, ESelectInfo::Type SelectionType)
{
	SelectedSource = Selected;

	if (SelectedSourceWidget.IsValid())
	{
		SelectedSourceWidget->RefreshSource(Selected);
	}

	OnSelectionChangedDelegate.ExecuteIfBound(Selected);
}

void SSourceSelector::OnWidgetSelectionChanged(FName SelectedName, ESelectInfo::Type SelectionType)
{
	FBindingSource Source = FBindingSource::CreateForWidget(WidgetBlueprint.Get(), SelectedName);

	if (SelectedSourceWidget.IsValid())
	{
		SelectedSourceWidget->RefreshSource(Source);
	}

	OnSelectionChangedDelegate.ExecuteIfBound(Source);

}

void SSourceSelector::Refresh()
{
	SelectedSource = SelectedSourceAttribute.Get();

	if (SelectedSourceWidget.IsValid())
	{
		SelectedSourceWidget->RefreshSource(SelectedSource);
	}

	TSharedPtr<SWidget> SourcePicker;
	if (bViewModels)
	{
		UMVVMEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
		if (const UMVVMBlueprintView* View = EditorSubsystem->GetView(WidgetBlueprint.Get()))
		{
			const TArrayView<const FMVVMBlueprintViewModelContext> ViewModels = View->GetViewModels();
			ViewModelSources.Reserve(ViewModels.Num());

			for (const FMVVMBlueprintViewModelContext& ViewModel : ViewModels)
			{
				FBindingSource& Source = ViewModelSources.AddDefaulted_GetRef();
				Source.Class = ViewModel.GetViewModelClass();
				Source.Name = ViewModel.GetViewModelName();
				Source.DisplayName = ViewModel.GetDisplayName();
				Source.ViewModelId = ViewModel.GetViewModelId();
			}
		}
	}

	if (WidgetHierarchy.IsValid())
	{
		WidgetHierarchy->Refresh();
	}

	if (ViewModelList.IsValid())
	{
		if (SelectedSource.IsValid())
		{
			ViewModelList->SetItemSelection(SelectedSource, true);
		}
		else
		{
			ViewModelList->ClearSelection();
		}
	}

}

EVisibility SSourceSelector::GetClearVisibility() const
{
	return SelectedSource.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SSourceSelector::OnClearSource()
{
	if (ViewModelList.IsValid())
	{
		ViewModelList->ClearSelection();
	}

	return FReply::Handled();
}

} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE
