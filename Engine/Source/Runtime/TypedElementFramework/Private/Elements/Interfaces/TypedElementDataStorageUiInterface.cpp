// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"
#include "Elements/Framework/TypedElementColumnUtils.h"
#include "Elements/Columns/TypedElementSlateWidgetColumns.h"

FTypedElementWidgetConstructor::FTypedElementWidgetConstructor(UScriptStruct* InTypeInfo)
	: TypeInfo(InTypeInfo)
{
}

const UScriptStruct* FTypedElementWidgetConstructor::GetTypeInfo() const
{
	return TypeInfo;
}

TSharedPtr<SWidget> FTypedElementWidgetConstructor::Construct(
	TypedElementRowHandle Row,
	ITypedElementDataStorageInterface* DataStorage,
	ITypedElementDataStorageUiInterface* DataStorageUi,
	TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments)
{
	ApplyArguments(Arguments);
	TSharedPtr<SWidget> Widget = CreateWidget();
	if (Widget)
	{
		AddColumns(DataStorage, Row, Widget);
	}
	return Widget;
}

void FTypedElementWidgetConstructor::ApplyArguments(TConstArrayView<TypedElement::ColumnUtils::Argument> Arguments)
{
	SetColumnValues(*this, Arguments);
}

void FTypedElementWidgetConstructor::AddColumns(
	ITypedElementDataStorageInterface* DataStorage, TypedElementRowHandle Row, const TSharedPtr<SWidget>& Widget)
{
	DataStorage->AddColumns<FTypedElementSlateWidgetReferenceColumn, FTypedElementSlateWidgetReferenceDeletesRowTag>(Row);
	DataStorage->GetColumn<FTypedElementSlateWidgetReferenceColumn>(Row)->Widget = Widget;
}