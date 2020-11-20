// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDetailsCustomization.h"

#include "SOptimusDataTypeSelector.h"

#include "OptimusDataTypeRegistry.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"


#define LOCTEXT_NAMESPACE "OptimusDetailCustomization"


TSharedRef<IPropertyTypeCustomization> FOptimusDataTypeRefCustomization::MakeInstance()
{
	return MakeShareable(new FOptimusDataTypeRefCustomization);
}


void FOptimusDataTypeRefCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> InPropertyHandle, 
	FDetailWidgetRow& InHeaderRow, 
	IPropertyTypeCustomizationUtils& InCustomizationUtils
	)
{
	EOptimusDataTypeUsageFlags UsageMask = EOptimusDataTypeUsageFlags::Node;
	
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInResource"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Resource;
	}
	if (InPropertyHandle->HasMetaData(FName(TEXT("UseInVariable"))))
	{
		UsageMask |= EOptimusDataTypeUsageFlags::Variable;
	}

	TypeNameProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FOptimusDataTypeRef, TypeName));

	InHeaderRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SOptimusDataTypeSelector)
		.CurrentDataType(this, &FOptimusDataTypeRefCustomization::GetCurrentDataType)
		.UsageMask(UsageMask)
		.Font(InCustomizationUtils.GetRegularFont())
		.OnDataTypeChanged(this, &FOptimusDataTypeRefCustomization::OnDataTypeChanged)
	];
}


void FOptimusDataTypeRefCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	// FIXME: This doesn't update quite properly. Need a better approach.
	FDetailWidgetRow& DeclarationRow = InChildBuilder.AddCustomRow(LOCTEXT("Declaration", "Declaration"));

	DeclarationRow
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget(LOCTEXT("Declaration", "Declaration"))
	]
	.ValueContent()
	.HAlign(HAlign_Fill)
	[
		SNew(SBox)
		.MinDesiredWidth(180.0f)
		[
			SNew(SMultiLineEditableTextBox)
			.Text(this, &FOptimusDataTypeRefCustomization::GetDeclarationText)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", InCustomizationUtils.GetRegularFont().Size))
			.IsReadOnly(true)
		]
	];
}


FOptimusDataTypeHandle FOptimusDataTypeRefCustomization::GetCurrentDataType() const
{
	FName TypeName;
	TypeNameProperty->GetValue(TypeName);
	return FOptimusDataTypeRegistry::Get().FindType(TypeName);
}


void FOptimusDataTypeRefCustomization::OnDataTypeChanged(FOptimusDataTypeHandle InDataType)
{
	CurrentDataType = InDataType;
	TypeNameProperty->SetValue(InDataType.IsValid() ? InDataType->TypeName : NAME_None);
}


FText FOptimusDataTypeRefCustomization::GetDeclarationText() const
{
	FOptimusDataTypeHandle DataType = GetCurrentDataType();

	if (DataType.IsValid() && DataType->ShaderValueType.IsValid())
	{
		const FShaderValueType* ValueType = DataType->ShaderValueType.ValueTypePtr;
		FText Declaration;
		if (ValueType->Type == EShaderFundamentalType::Struct)
		{
			return FText::FromString(ValueType->GetTypeDeclaration());
		}
		else
		{
			return FText::FromString(ValueType->ToString());
		}
	}
	else
	{
		return FText::GetEmpty();
	}
}


#undef LOCTEXT_NAMESPACE
