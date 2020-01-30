// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLiveLinkSubjectRepresentationGraphPin.h"
#include "SLiveLinkSubjectRepresentationPicker.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"
#include "UObject/PropertyPortFlags.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SubjectRepresentation"

void SLiveLinkSubjectRepresentationGraphPin::Construct( const FArguments& InArgs, UEdGraphPin* InGraphPinObj )
{
	SGraphPin::Construct( SGraphPin::FArguments(), InGraphPinObj );
}

TSharedRef<SWidget>	SLiveLinkSubjectRepresentationGraphPin::GetDefaultValueWidget()
{
	FString DefaultString = GraphPinObj->GetDefaultAsString();
	FLiveLinkSubjectRepresentation::StaticStruct()->ImportText(*DefaultString, &SubjectRepresentation, nullptr, EPropertyPortFlags::PPF_None, GLog, FLiveLinkSubjectRepresentation::StaticStruct()->GetName());

	//Create widget
	return SNew(SLiveLinkSubjectRepresentationPicker)
		.ShowRole(true)
		.HasMultipleValues(false)
		.Value(this, &SLiveLinkSubjectRepresentationGraphPin::GetValue)
		.OnValueChanged(this, &SLiveLinkSubjectRepresentationGraphPin::SetValue);
}

FLiveLinkSubjectRepresentation SLiveLinkSubjectRepresentationGraphPin::GetValue() const
{
	return SubjectRepresentation;
}

void SLiveLinkSubjectRepresentationGraphPin::SetValue(FLiveLinkSubjectRepresentation NewValue)
{
	SubjectRepresentation = NewValue;

	FString ValueString;
	FLiveLinkSubjectRepresentation::StaticStruct()->ExportText(ValueString, &SubjectRepresentation, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, ValueString);
}

#undef LOCTEXT_NAMESPACE
