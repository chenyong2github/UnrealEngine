// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "Misc/Optional.h"
#include "NiagaraTypes.h"

// This data structure is used internally by the dropdown to keep track of the user's choice
struct SwitchDropdownOption
{
	FString Name;
	UEnum* Enum;

	SwitchDropdownOption(FString Name) : Name(Name), Enum(nullptr)
	{}

	SwitchDropdownOption(FString Name, UEnum* Enum) : Name(Name), Enum(Enum)
	{}
};

// This data structure is used internally by the default enum dropdown to keep track of the user's choice
struct DefaultEnumOption
{
	FText DisplayName;
	int32 EnumIndex;	

	DefaultEnumOption(FText DisplayName) : DisplayName(DisplayName), EnumIndex(0)
	{
	}

	DefaultEnumOption(FText DisplayName, int32 EnumIndex) : DisplayName(DisplayName), EnumIndex(EnumIndex)
	{
	}
};

/** This customization sets up a custom details panel for the static switch node in the niagara module graph. */
class FNiagaraStaticSwitchNodeDetails : public IDetailCustomization
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	FNiagaraStaticSwitchNodeDetails();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	
private:
	// enum dropdown functions
	TSharedRef<SWidget> CreateWidgetForDropdownOption(TSharedPtr<SwitchDropdownOption> InOption);
	void OnSelectionChanged(TSharedPtr<SwitchDropdownOption> NewValue, ESelectInfo::Type);
	FText GetDropdownItemLabel() const;
	void UpdateSelectionFromNode();

	// float type option functions
	bool GetIntOptionEnabled() const;
	TOptional<int32> GetIntOptionValue() const;
	void IntOptionValueCommitted(int32 Value, ETextCommit::Type CommitInfo);

	// parameter name option function
	FText GetParameterNameText() const;
	void OnParameterNameCommited(const FText& InText, ETextCommit::Type InCommitType);

	// default value option functions
	int32 GetDefaultWidgetIndex() const;
	TOptional<int32> GetSwitchDefaultValue() const;
	void DefaultIntValueCommitted(int32 Value, ETextCommit::Type CommitInfo);
	void DefaultBoolValueCommitted(ECheckBoxState NewState);
	TSharedRef<SWidget> CreateWidgetForDropdownOption(TSharedPtr<DefaultEnumOption> InOption);
	void OnSelectionChanged(TSharedPtr<DefaultEnumOption> NewValue, ESelectInfo::Type);
	FText GetDefaultSelectionItemLabel() const;
	void RefreshDefaultDropdownValues();

	TOptional<FNiagaraVariableMetaData> GetSwitchParameterMetadata() const;
	void SetSwitchParameterMetadata(const FNiagaraVariableMetaData& MetaData);

	TWeakObjectPtr<class UNiagaraNodeStaticSwitch> Node;
	TArray<TSharedPtr<SwitchDropdownOption>> DropdownOptions;
	TSharedPtr<SwitchDropdownOption> SelectedDropdownItem;
	TArray<TSharedPtr<DefaultEnumOption>> DefaultEnumDropdownOptions;
	TSharedPtr<DefaultEnumOption> SelectedDefaultValue;
};
