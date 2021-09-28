// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeParameterLayoutDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "StateTree.h"
#include "StateTreeDelegates.h"
#include "StateTreeState.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "StateTreePropertyHelpers.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeParameterLayoutDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeParameterLayoutDetails);
}

void FStateTreeParameterLayoutDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	ParametersProperty = StructProperty->GetChildHandle(TEXT("Parameters"));

	UE::StateTree::Delegates::OnParametersInvalidated.AddSP(this, &FStateTreeParameterLayoutDetails::OnParametersInvalidated);

	HeaderRow
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			// Name
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				StructPropertyHandle->CreatePropertyNameWidget()
			]
			// Array controls
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(12.0f, 0.0f))
			[
				ParametersProperty->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeParameterLayoutDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Inline variable binding array as children
	if (ParametersProperty)
	{
		uint32 ChildNum = 0;
		if (ParametersProperty->GetNumChildren(ChildNum) == FPropertyAccess::Success)
		{
			for (uint32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				TSharedPtr<IPropertyHandle> ChildHandle = ParametersProperty->GetChildHandle(ChildIndex);
				if (ChildHandle)
				{
					// Set the property name to the name of the variable instead of array index.
					TSharedPtr<IPropertyHandle> NameProperty = ChildHandle->GetChildHandle(TEXT("Name"));
					TSharedPtr<IPropertyHandle> VariableProperty = ChildHandle->GetChildHandle(TEXT("Variable"));

					if (NameProperty && VariableProperty)
					{
						IDetailPropertyRow& PropertyRow = StructBuilder.AddProperty(VariableProperty.ToSharedRef());

						FName Name;
						NameProperty->GetValue(Name);

						TSharedPtr<SWidget> NameWidget;
						TSharedPtr<SWidget> ValueWidget;
						FDetailWidgetRow Row;
						PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

						PropertyRow
							.CustomWidget()
							.NameContent()
							[
								SNew(SHorizontalBox)
								// Icon slot
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(SImage)
									.Image(GetTypeIcon(VariableProperty))
									.ColorAndOpacity(FLinearColor::White)
								]
								// Name
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(FMargin(6.0f, 2.0f))
								[
									SNew(STextBlock)
									.Text(FText::FromName(Name))
									.Font(IDetailLayoutBuilder::GetDetailFontBold())
								]
							]
							.ValueContent()
							.MinDesiredWidth(250.0f)
							.VAlign(VAlign_Center)
							[
								ValueWidget.ToSharedRef()
							];

					}
				}
			}
		}
	}
}

const FSlateBrush* FStateTreeParameterLayoutDetails::GetTypeIcon(TSharedPtr<IPropertyHandle> VariableProperty) const
{
	EStateTreeVariableType Type = EStateTreeVariableType::Void;

	TSharedPtr<IPropertyHandle> TypeProperty = VariableProperty->GetChildHandle(TEXT("Type"));
	if (TypeProperty)
	{
		uint8 Value;
		if (TypeProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			Type = EStateTreeVariableType(Value);
		}
	}

	return UE::StateTree::PropertyHelpers::GetTypeIcon(Type);
}

void FStateTreeParameterLayoutDetails::OnParametersInvalidated(const UStateTree& StateTree)
{
	// Check if the change is relevant to us.
	bool bShouldUpdate = false;
	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);
	for (UObject* Outer : OuterObjects)
	{
		if (Outer->GetTypedOuter<UStateTree>() == &StateTree)
		{
			bShouldUpdate = true;
			break;
		}
	}

	if (bShouldUpdate && PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
