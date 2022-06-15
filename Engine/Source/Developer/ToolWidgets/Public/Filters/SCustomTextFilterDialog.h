// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Filters/FilterBase.h"
#include "Core/Public/Misc/TextFilter.h"

#include "SCustomTextFilterDialog.generated.h"

class SColorBlock;
class SEditableTextBox;

/* Struct containing the data that SCustomTextFilterDialog is currently editing */
USTRUCT()
struct FCustomTextFilterData
{
	GENERATED_BODY()

	UPROPERTY()
	FText FilterLabel;
	
	UPROPERTY()
	FText FilterString;

	UPROPERTY()
	FLinearColor FilterColor;

	FCustomTextFilterData()
		: FilterColor(FLinearColor::White)
	{
		
	}
};

/* A filter that allows testing items of type FilterType against text expressions */
template<typename FilterType>
class FCustomTextFilter : public FFilterBase<FilterType>
{
public:

	FCustomTextFilter(TSharedPtr<TTextFilter<FilterType>> InTextFilter)
	: FFilterBase<FilterType>(nullptr)
	, TextFilter(InTextFilter)
	{
		
	}

	/** Returns the system name for this filter */
	virtual FString GetName() const override
	{
		return GetCommonName().ToString();
	}

	/** Returns the human readable name for this filter */
	virtual FText GetDisplayName() const override
	{
		return DisplayName; 
	}

	/** Set the human readable name for this filter */
	void SetDisplayName(const FText& InDisplayName)
	{
		DisplayName = InDisplayName;
	}

	/** Returns the tooltip for this filter, shown in the filters menu */
	virtual FText GetToolTipText() const override
	{
		return TextFilter->GetRawFilterText(); // The tooltip will display the filter string
	}

	/** Returns the color this filter button will be when displayed as a button */
	virtual FLinearColor GetColor() const override
	{
		return Color;
	}

	/** Set the color this filter button will be when displayed as a button */
	void SetColor(const FLinearColor& InColor)
	{
		Color = InColor;
	}

	/** Returns the name of the icon to use in menu entries */
	virtual FName GetIconName() const override
	{
		return NAME_None;
	}

	/** Returns true if the filter should be in the list when disabled and not in the list when enabled */
	virtual bool IsInverseFilter() const override
	{
		return false;
	}

	virtual bool PassesFilter( FilterType InItem ) const override
	{
		return TextFilter->PassesFilter(InItem);
	}

	/** Get the actual text this filter is using to test against */
	FText GetFilterString() const
	{
		return TextFilter->GetRawFilterText();
	}

	/** Set the actual text this filter is using to test against */
	void SetFilterString(const FText& InFilterString)
	{
		TextFilter->SetRawFilterText(InFilterString);
	}

	/** All FCustomTextFilters have the same internal name, this is a helper function to get that name to test against */
	static FName GetCommonName()
	{
		return FName("CustomTextFilter");
	}

	/** Set the internals of this filter from an FCustomTextFilterData */
	void SetFromCustomTextFilterData(const FCustomTextFilterData& InFilterData)
	{
		SetDisplayName(InFilterData.FilterLabel);
		SetColor(InFilterData.FilterColor);
		SetFilterString(InFilterData.FilterString);
	}

	/** Create an FCustomTextFilterData from the internals of this filter */
	FCustomTextFilterData CreateCustomTextFilterData() const
	{
		FCustomTextFilterData CustomTextFilterData;
		CustomTextFilterData.FilterLabel = GetDisplayName();
		CustomTextFilterData.FilterColor = GetColor();
		CustomTextFilterData.FilterString = GetFilterString();

		return CustomTextFilterData;
	}
	
	// Functionality that is not needed by this filter
	
	/** Notification that the filter became active or inactive */
	virtual void ActiveStateChanged(bool bActive) override
	{
		
	}

	/** Called when the right-click context menu is being built for this filter */
	virtual void ModifyContextMenu(FMenuBuilder& MenuBuilder) override
	{
		
	}
	
	/** Can be overriden for custom FilterBar subclasses to save settings, currently not implemented in any gneeric Filter Bar */
	virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override
	{
		
	}

	/** Can be overriden for custom FilterBar subclasses to load settings, currently not implemented in any gneeric Filter Bar */
	virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override
	{
		
	}
	
protected:

	/* The actual Text Filter containing information about the text being tested against */
	TSharedPtr<TTextFilter<FilterType>> TextFilter;

	/* The Display Name of this custom filter that the user sees */
	FText DisplayName;

	/* The Color of this filter pill */
	FLinearColor Color;
};




class TOOLWIDGETS_API SCustomTextFilterDialog : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_TwoParams(FOnCreateFilter, const FCustomTextFilterData& /* InFilterData */, bool /* bApplyFilter */);
	DECLARE_DELEGATE(FOnDeleteFilter);
	DECLARE_DELEGATE_OneParam(FOnModifyFilter, const FCustomTextFilterData& /* InFilterData */);
	DECLARE_DELEGATE(FOnCancelClicked);

	SLATE_BEGIN_ARGS(SCustomTextFilterDialog) {}
	
    	/** The filter that this dialog is creating/editing */
    	SLATE_ARGUMENT(FCustomTextFilterData, FilterData)

		/** True if we are editing an existing filter, false if we are creating a new one */
		SLATE_ARGUMENT(bool, InEditMode)
		
		/** Delegate for when the Create button is clicked */
		SLATE_EVENT(FOnCreateFilter, OnCreateFilter)
		
		/** Delegate for when the Delete button is clicked */
		SLATE_EVENT(FOnDeleteFilter, OnDeleteFilter)
		
		/** Delegate for when the Cancel button is clicked */
		SLATE_EVENT(FOnCancelClicked, OnCancelClicked)

		/** Delegate for when the Modify Filter button is clicked */
        SLATE_EVENT(FOnModifyFilter, OnModifyFilter)
    
    SLATE_END_ARGS()
    	
    /** Constructs this widget with InArgs */
    void Construct( const FArguments& InArgs );

protected:

	/* Handler for when the color block is clicked to open the color picker */
	FReply ColorBlock_OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	
	FReply OnDeleteButtonClicked() const;
	
	FReply OnCreateFilterButtonClicked(bool bApplyFilter) const;
	
	FReply OnCancelButtonClicked() const;

	FReply OnModifyButtonClicked() const;

	bool CheckFilterValidity() const;
	
protected:

	/* True if we are editing a filter, false if we are creating a new filter */
	bool bInEditMode;

	/* The current filter data we are editing */
	FCustomTextFilterData FilterData;
	
	FOnCreateFilter OnCreateFilter;
	
	FOnDeleteFilter OnDeleteFilter;
	
	FOnCancelClicked OnCancelClicked;

	FOnModifyFilter OnModifyFilter;

	/* The color block widget that edits the filter color */
	TSharedPtr<SColorBlock> ColorBlock;

	/* The widget that edits the filter label */
	TSharedPtr<SEditableTextBox> FilterLabelTextBox;
};