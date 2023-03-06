// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "MoviePipelineQueue.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEditor"

/** Customize how properties for a job appear in the details panel. */
class FJobDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FJobDetailsCustomization>();
	}

protected:
	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override
	{
		WeakDetailBuilder = DetailBuilder;
		CustomizeDetails(*DetailBuilder);
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		// Add a new "Graph Variables" category. Set as "Uncommon" priority to push variables down below the other properties.
		IDetailCategoryBuilder& GraphVariablesCategory = DetailBuilder.EditCategory(
"GraphVariables", LOCTEXT("GraphVariablesCategory", "Graph Variables"), ECategoryPriority::Uncommon);

		TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
		DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

		// Only display variables if there is one job selected
		if (ObjectsBeingCustomized.Num() != 1)
		{
			return;
		}

		// Get the displayed job
		UMoviePipelineExecutorJob* CustomizedJob = Cast<UMoviePipelineExecutorJob>(ObjectsBeingCustomized[0].Get());
		if (!CustomizedJob)
		{
			return;
		}

		UMovieGraphConfig* GraphConfig = GetGraphConfig(CustomizedJob);
		if (!GraphConfig)
		{
			return;
		}

		for (const UMovieGraphVariable* Variable : GraphConfig->GetVariables())
		{
			AddVariableAssignmentRow(GraphVariablesCategory, DetailBuilder, Variable, CustomizedJob);
		}

		// Refresh the variable listing if anything changes with the variables
		GraphConfig->OnGraphVariablesChangedDelegate.AddSP(this, &FJobDetailsCustomization::Refresh);
	}
	//~ End IDetailCustomization interface

	void Refresh() const
	{
		// Don't keep the details builder alive just for the sake of this Refresh() call
		if (IDetailLayoutBuilder* DetailLayoutBuilder = WeakDetailBuilder.Pin().Get())
		{
			DetailLayoutBuilder->ForceRefreshDetails();
		}
	}

	void AddVariableAssignmentRow(IDetailCategoryBuilder& DetailCategoryBuilder, const IDetailLayoutBuilder& DetailBuilder,
		const UMovieGraphVariable* Variable, UMoviePipelineExecutorJob* CustomizedJob)
	{
		FDetailWidgetRow& NewRow = DetailCategoryBuilder.AddCustomRow(FText::FromString(Variable->Name));
		NewRow.WholeRowWidget
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SCheckBox)
				.IsChecked(this, &FJobDetailsCustomization::IsVariableOverrideChecked, Variable, CustomizedJob)
				.OnCheckStateChanged(this, &FJobDetailsCustomization::OnVariableOverrideEnableStateChanged, Variable, CustomizedJob)
			]
				
			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Variable->Name))
				.IsEnabled(this, &FJobDetailsCustomization::IsVariableOverrideEnabled, Variable, CustomizedJob)
				.Font(DetailBuilder.GetDetailFont())
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.5f)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 1, 0)
			[
				SNew(SNumericEntryBox<float>)
				.Value(this, &FJobDetailsCustomization::GetVariableValue, Variable, CustomizedJob)
				.IsEnabled(this, &FJobDetailsCustomization::IsVariableOverrideEnabled, Variable, CustomizedJob)
				.Font(DetailBuilder.GetDetailFont())
				.OnValueCommitted(this, &FJobDetailsCustomization::OnVariableValueCommitted, Variable, CustomizedJob)
			]
		];
	}

	UMovieGraphConfig* GetGraphConfig(const UMoviePipelineExecutorJob* InJob) const
	{
		// TODO: This should get the config OR preset
		return InJob->GetGraphPreset();
	}
	
	ECheckBoxState IsVariableOverrideChecked(const UMovieGraphVariable* InVariable, UMoviePipelineExecutorJob* InJob) const
	{
		if (InJob && InVariable)
		{
			bool bIsEnabled;
			if (InJob->GetVariableAssignmentEnableState(InVariable, bIsEnabled))
			{
				return bIsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
		}

		return ECheckBoxState::Unchecked;
	}

	bool IsVariableOverrideEnabled(const UMovieGraphVariable* InVariable, UMoviePipelineExecutorJob* InJob) const
	{
		return IsVariableOverrideChecked(InVariable, InJob) == ECheckBoxState::Checked;
	}

	void OnVariableOverrideEnableStateChanged(ECheckBoxState CheckBoxState, const UMovieGraphVariable* InVariable, UMoviePipelineExecutorJob* InJob) const
	{
		if (InJob && InVariable)
		{
			InJob->SetVariableAssignmentEnableState(InVariable, CheckBoxState == ECheckBoxState::Checked);
		}
	}

	TOptional<float> GetVariableValue(const UMovieGraphVariable* InVariable, UMoviePipelineExecutorJob* InJob) const
	{
		if (InJob && InVariable)
		{
			// Get the value of the variable from the job, if any
			float VariableValue;
			if (InJob->GetVariableAssignmentValue(InVariable, VariableValue))
			{
				return VariableValue;
			}

			// Fall back to the default value of the variable
			return InVariable->Default;
		}

		return TOptional<float>();
	}

	void OnVariableValueCommitted(float NewValue, ETextCommit::Type Arg, const UMovieGraphVariable* Variable, UMoviePipelineExecutorJob* InJob) const
	{
		if (InJob)
		{
			InJob->SetVariableAssignmentValue(Variable, NewValue);
		}
	}

private:
	TWeakPtr<IDetailLayoutBuilder> WeakDetailBuilder;
};

#undef LOCTEXT_NAMESPACE