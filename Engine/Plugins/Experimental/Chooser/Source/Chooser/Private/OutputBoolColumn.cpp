// Copyright Epic Games, Inc. All Rights Reserved.
#include "OutputBoolColumn.h"
#include "ChooserPropertyAccess.h"
#include "BoolColumn.h"

FOutputBoolColumn::FOutputBoolColumn()
{
	InputValue.InitializeAs(FBoolContextProperty::StaticStruct());
}

void FOutputBoolColumn::SetOutputs(UObject* ContextObject, int RowIndex) const
{
	InputValue.Get<FChooserParameterBoolBase>().SetValue(ContextObject, RowValues[RowIndex]);
}
