// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMSourceSelector.h"

#include "Algo/Transform.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/SMVVMSourceEntry.h"

#define LOCTEXT_NAMESPACE "MVVMSourceSelector"

using namespace UE::MVVM;

static FBindingSource GetSelectedSource(const TArray<IFieldPathHelper*>& Helpers)
{
	bool bFirst = true;
	TOptional<FBindingSource> Result;

	for (const IFieldPathHelper* Helper : Helpers)
	{
		TOptional<FBindingSource> ThisSelection = Helper->GetSelectedSource();
		if (bFirst)
		{
			Result = ThisSelection;
		}
		else if (Result != ThisSelection)
		{
			// all don't share the same value
			Result.Reset();
			break;
		}
	}

	return Result.Get(FBindingSource());
}

void SMVVMSourceSelector::Construct(const FArguments& Args)
{
	PathHelpers = Args._PathHelpers;
	OnSelectionChanged = Args._OnSelectionChanged;
	TextStyle = Args._TextStyle;

	Refresh();

	ChildSlot
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
	];
}

void SMVVMSourceSelector::OnComboBoxSelectionChanged(FBindingSource Selected, ESelectInfo::Type SelectionType)
{
	SelectedSource = Selected;

	TOptional<FBindingSource> OptionalSource = SelectedSource;

	SelectedSourceWidget->RefreshSource(OptionalSource);

	OnSelectionChanged.ExecuteIfBound(OptionalSource);
}

void SMVVMSourceSelector::Refresh()
{
	AvailableSources.Reset();
	SelectedSource = FBindingSource();

	TArray<IFieldPathHelper*> Helpers = PathHelpers.Get(TArray<IFieldPathHelper*>());

	bool bFirst = true;

	TSet<FBindingSource> SourceIntersection;
	TOptional<FBindingSource> NewSelection;

	for (const IFieldPathHelper* Helper : Helpers)
	{
		// get available sources
		TSet<FBindingSource> Sources;
		Helper->GetAvailableSources(Sources);

		if (bFirst)
		{
			bFirst = false; 

			SourceIntersection.Append(Sources);
		}
		else
		{
			SourceIntersection.Intersect(Sources);
		}
	}

	AvailableSources = SourceIntersection.Array();
	SelectedSource = GetSelectedSource(Helpers);

	if (SourceComboBox.IsValid())
	{
		SourceComboBox->RefreshOptions();
		SourceComboBox->SetSelectedItem(SelectedSource);
	}
}

#undef LOCTEXT_NAMESPACE
