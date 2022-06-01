// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCActionModel.h"

#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Controller/RCController.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "UI/RCUIHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRCPanelDragHandle.h"
#include "UI/SRCPanelExposedEntity.h"

FRCActionModel::FRCActionModel(URCAction* InAction)
	: ActionWeakPtr(InAction)
{
}

URCAction* FRCActionModel::GetAction() const
{
	return ActionWeakPtr.Get();
}

FRCPropertyActionModel::FRCPropertyActionModel(URCPropertyAction* InPropertyAction)
	: FRCActionModel(InPropertyAction)
	, PropertyActionWeakPtr(InPropertyAction)
{
	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);

	if (InPropertyAction)
	{
		if (const TSharedPtr<FStructOnScope> StructOnScope = InPropertyAction->PropertySelfContainer->CreateStructOnScope())
		{
			PropertyRowGenerator->SetStructure(StructOnScope);

			for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGenerator->GetRootTreeNodes())
			{
				TArray<TSharedRef<IDetailTreeNode>> Children;
				CategoryNode->GetChildren(Children);
				for (const TSharedRef<IDetailTreeNode>& Child : Children)
				{
					DetailTreeNodeWeakPtr = Child;
					break;
				}
			}
		}
	}
}

const FName& FRCPropertyActionModel::GetPropertyName() const
{
	return PropertyActionWeakPtr.Get()->PropertySelfContainer->PropertyName;
}

TSharedRef<SWidget> FRCPropertyActionModel::GetWidget() const
{
	if (DetailTreeNodeWeakPtr.IsValid())
	{
		const FNodeWidgets NodeWidgets = DetailTreeNodeWeakPtr.Pin()->CreateNodeWidgets();

		const TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);
	
		if (NodeWidgets.ValueWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					NodeWidgets.ValueWidget.ToSharedRef()
				];
		}
		else if (NodeWidgets.WholeRowWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.WholeRowWidget.ToSharedRef()
				];
		}

		TSharedRef<SBorder> BorderWidget = SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.ExposedFieldBorder"));

		FLinearColor TypeColor;
		if (PropertyActionWeakPtr.IsValid())
		{
			TypeColor = UE::RCUIHelpers::GetFieldClassTypeColor(PropertyActionWeakPtr->PropertySelfContainer->GetProperty());
		}
	
		return SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)

			// Variable Color Bar
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Fill)
			.AutoWidth()
			.Padding(FMargin(3.f))
			[
				SNew(SBorder)
				.Visibility(EVisibility::HitTestInvisible)
				.BorderImage(FAppStyle::Get().GetBrush("NumericEntrySpinBox.NarrowDecorator"))
				.BorderBackgroundColor(TypeColor)
				.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
			]
			// Field name
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				NodeWidgets.NameWidget.ToSharedRef()
			]
			// Value Widget
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				FieldWidget
			];
	}
	
	return FRCActionModel::GetWidget();
}

FRCFunctionActionModel::FRCFunctionActionModel(URCFunctionAction* InFunctionAction)
	: FRCActionModel(InFunctionAction)
	, FunctionActionWeakPtr(InFunctionAction)
{
}

TSharedRef<SWidget> FRCFunctionActionModel::GetWidget() const
{
	if (FunctionActionWeakPtr.IsValid())
	{
		if (URemoteControlPreset* Preset = FunctionActionWeakPtr->PresetWeakPtr.Get())
		{
			if (const TSharedPtr<FRemoteControlFunction> RemoteControlField = Preset->GetExposedEntity<FRemoteControlFunction>(FunctionActionWeakPtr->ExposedFieldId).Pin())
			{
				const FString FinalNameStr = RemoteControlField->GetLabel().ToString();

				return SNew(STextBlock).Text(FText::FromString(FinalNameStr));
			}
		}
	}
	
	return FRCActionModel::GetWidget();
}