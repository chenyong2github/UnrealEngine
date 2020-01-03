// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/VariantManagerPropertyNode.h"
#include "Widgets/Input/SComboBox.h"

class SToolTip;
class SVariantManagerTableRow;


class FVariantManagerEnumPropertyNode
	: public FVariantManagerPropertyNode
{
public:

	FVariantManagerEnumPropertyNode(TArray<UPropertyValue*> InPropertyValues, TWeakPtr<FVariantManager> InVariantManager);

protected:

	virtual TSharedPtr<SWidget> GetPropertyValueWidget() override;

private:

	void OnComboboxSelectionChanged(TSharedPtr<FString> NewItem, ESelectInfo::Type SelectType);
	FText ComboboxGetText(bool bSameValue) const;

	void UpdateComboboxStrings();

	TSharedPtr<SComboBox<TSharedPtr<FString>>> Combobox;

	// Also store indices because we won't store texts/tooltips for hidden enums
	TArray<TSharedPtr<FString>> EnumDisplayTexts;
	TArray<TSharedPtr<SToolTip>> EnumRichToolTips;
	TArray<int32> EnumIndices;
};
