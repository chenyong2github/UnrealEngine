// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailWidgetRow.h"
#include "PropertyEditorHelpers.h"

FDetailWidgetRow& FDetailWidgetRow::EditCondition(TAttribute<bool> InEditConditionValue, FOnBooleanValueChanged InOnEditConditionValueChanged)
{
	EditConditionValue = InEditConditionValue;
	OnEditConditionValueChanged = InOnEditConditionValueChanged;
	return *this;
}