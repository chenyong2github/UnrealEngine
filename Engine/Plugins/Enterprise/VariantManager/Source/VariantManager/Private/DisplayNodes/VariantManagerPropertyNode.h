// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/VariantManagerDisplayNode.h"
#include "UObject/StrongObjectPtr.h"

class FMenuBuilder;
struct FSlateBrush;
class ISinglePropertyView;
class UPropertyTemplateObject;
class UPropertyValue;
class SVariantManagerTableRow;
class UPropertyValue;
class FReply;
struct EVisibility;
class SButton;

class FVariantManagerPropertyNode
	: public FVariantManagerDisplayNode
{
public:

	FVariantManagerPropertyNode(TArray<UPropertyValue*> InPropertyValues, TWeakPtr<FVariantManager> InVariantManager);

	const TArray<TWeakObjectPtr<UPropertyValue>>& GetPropertyValues() const
	{
		return PropertyValues;
	}

	// FVariantManagerDisplayNode interface
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual FText GetDisplayNameToolTipText() const override;
	virtual const FSlateBrush* GetIconOverlayBrush() const override;
	virtual FSlateColor GetNodeBackgroundTint() const override;
	virtual FText GetIconToolTipText() const override;
	virtual EVariantManagerNodeType GetType() const override;
	virtual bool IsReadOnly() const override;
	virtual FText GetDisplayName() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;
	virtual bool IsSelectable() const override;
	virtual TWeakPtr<FVariantManager> GetVariantManager() const override
	{
		return VariantManager;
	}
	//~ End FVariantManagerDisplayNode interface

	virtual TSharedRef<SWidget> GetCustomOutlinerContent(TSharedPtr<SVariantManagerTableRow> InTableRow) override;

protected:

	// Callback for when user updates the property widget
	virtual void UpdateRecordedDataFromSinglePropView(TSharedPtr<ISinglePropertyView> SinglePropView);

	// Reset recorded data of all property values to the CDO value
	virtual FReply ResetMultipleValuesToDefault();

	// Reset recorded data from all property values
	virtual FReply RecordMultipleValues();

	// Returns true if all properties have the exact same value bytes
	virtual bool PropertiesHaveSameValue() const;

	// Returns true if all properties have the exact same value bytes as the CDO
	virtual bool PropertiesHaveDefaultValue() const;

	// Returns true if all UPropertyValues have recorded data that matches the current value of the
	// properties they are tracking
	virtual bool PropertiesHaveCurrentValue() const;

	bool RecursiveDisableOldResetButton(TSharedPtr<SWidget> Widget);

	EVisibility GetResetButtonVisibility() const;
	EVisibility GetRecordButtonVisibility() const;

	virtual TSharedPtr<SWidget> GetPropertyValueWidget();

	TSharedPtr<SButton> ResetButton;
	TSharedPtr<SButton> RecordButton;

	TStrongObjectPtr<UPropertyTemplateObject> SinglePropertyViewTemplate;

	TMap<TWeakObjectPtr<UPropertyValue>, FDelegateHandle> PropertyValueOnRecordedSubscriptions;

	TArray<TWeakObjectPtr<UPropertyValue>> PropertyValues;

	FText DefaultDisplayName;

	TWeakPtr<FVariantManager> VariantManager;
};
