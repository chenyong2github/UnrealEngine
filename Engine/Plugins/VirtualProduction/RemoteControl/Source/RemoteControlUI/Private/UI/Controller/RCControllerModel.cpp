// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCControllerModel.h"

#include "IDetailTreeNode.h"
#include "RemoteControlPreset.h"
#include "RCVirtualProperty.h"
#include "UI/SRemoteControlPanel.h"

FRCControllerModel::FRCControllerModel(const FName& InPropertyName, const TSharedRef<IDetailTreeNode>& InTreeNode, const TSharedPtr<SRemoteControlPanel> InRemoteControlPanel)
	: FRCLogicModeBase(InRemoteControlPanel)
	, DetailTreeNodeWeakPtr(InTreeNode)
	, PropertyName(InPropertyName)
{
}

TSharedRef<SWidget> FRCControllerModel::GetWidget() const
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
	
	return 	SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		// Field name
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()		
		[
			NodeWidgets.NameWidget.ToSharedRef()
		]
		// Value Widget
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(FMargin(3.f))
		[
			FieldWidget
		];
}

URCVirtualPropertyBase* FRCControllerModel::GetVirtualProperty() const
{
	if (const URemoteControlPreset* Preset = GetPreset())
	{
		return Preset->GetVirtualProperty(PropertyName);
	}

	return nullptr;
}

const FName& FRCControllerModel::GetPropertyName() const
{
	return PropertyName;
}

TSharedPtr<FRCBehaviourModel> FRCControllerModel::GetSelectedBehaviourModel() const
{
	return SelectedBehaviourModelWeakPtr.Pin();
}

void FRCControllerModel::UpdateSelectedBehaviourModel(TSharedPtr<FRCBehaviourModel> InModel)
{
	SelectedBehaviourModelWeakPtr = InModel;
}