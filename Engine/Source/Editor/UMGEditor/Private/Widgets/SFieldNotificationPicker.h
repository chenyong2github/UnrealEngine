// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "FieldNotification/FieldId.h"
#include "FieldNotification/IFieldValueChanged.h"

template<typename OptionType>
class SComboBox;

namespace UE::FieldNotification
{

/**
 * A widget which allows the user to enter a FieldNotificationId or discover it from a drop menu.
 */
class SFieldNotificationPicker : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnValueChanged, FFieldNotificationId);

	SLATE_BEGIN_ARGS(SFieldNotificationPicker)
	{}
		SLATE_ATTRIBUTE(FFieldNotificationId, Value)
		SLATE_EVENT(FOnValueChanged, OnValueChanged)
		SLATE_ATTRIBUTE(UClass*, FromClass)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FFieldNotificationId GetCurrentValue() const;

private:
	void HandleComboBoxChanged(TSharedPtr<FFieldNotificationId> InItem, ESelectInfo::Type InSeletionInfo);
	TSharedRef<SWidget> HandleGenerateWidget(TSharedPtr<FFieldNotificationId> InItem);
	void HandleComboOpening();
	FText HandleComboBoxValueAsText() const;

private:
	TSharedPtr<SComboBox<TSharedPtr<FFieldNotificationId>>> PickerBox;
	TArray<TSharedPtr<FFieldNotificationId>> FieldNotificationIdsSource;
	FOnValueChanged OnValueChangedDelegate;
	TAttribute<FFieldNotificationId> ValueAttribute;
	TAttribute<UClass*> FromClassAttribute;
};

} // namespace