// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourIsEqualModel.h"

#include "Modules/ModuleManager.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PropertyEditorModule.h"
#include "RCVirtualProperty.h"
#include "SRCBehaviourIsEqual.h"

#define LOCTEXT_NAMESPACE "FRCIsEqualBehaviourModel"

FRCIsEqualBehaviourModel::FRCIsEqualBehaviourModel(URCIsEqualBehaviour* IsEqualBehaviour)
	: FRCBehaviourModel(IsEqualBehaviour)
	, EqualBehaviourWeakPtr(IsEqualBehaviour)
{
	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);

	if (IsEqualBehaviour)
	{
		PropertyRowGenerator->SetStructure(IsEqualBehaviour->PropertySelfContainer->CreateStructOnScope());

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

TSharedRef<SWidget> FRCIsEqualBehaviourModel::GetBehaviourDetailsWidget()
{
	return SNew(SRCBehaviourIsEqual, SharedThis(this));
}

TSharedRef<SWidget> FRCIsEqualBehaviourModel::GetPropertyWidget() const
{
	if (const TSharedPtr<IDetailTreeNode> DetailTreeNode = DetailTreeNodeWeakPtr.Pin())
	{
		const FNodeWidgets NodeWidgets = DetailTreeNode->CreateNodeWidgets();

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
			// Property Value Widget
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				FieldWidget
			];
	}

	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE