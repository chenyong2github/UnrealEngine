// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourRangeMapModel.h"

#include "Action/RCAction.h"
#include "Action/RCPropertyAction.h"
#include "Controller/RCController.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "SRCBehaviourRangeMap.h"
#include "UI/Action/RangeMap/RCActionRangeMapModel.h"
#include "UI/Action/SRCActionPanel.h"
#include "UI/Action/SRCActionPanelList.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FRCRangeMapBehaviourModel"

FRCRangeMapBehaviourModel::FRCRangeMapBehaviourModel(URCRangeMapBehaviour* RangeMapBehaviour)
	: FRCBehaviourModel(RangeMapBehaviour)
	, RangeMapBehaviourWeakPtr(RangeMapBehaviour)
{
	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);

	if (RangeMapBehaviour)
	{
		PropertyRowGenerator->SetStructure(RangeMapBehaviour->PropertyContainer->CreateStructOnScope());

		for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGenerator->GetRootTreeNodes())
		{
			TArray<TSharedRef<IDetailTreeNode>> Children;
			CategoryNode->GetChildren(Children);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				DetailTreeNodeWeakPtrArray.Add(Child);
			}
		}
	}
}

TSharedRef<SWidget> FRCRangeMapBehaviourModel::GetBehaviourDetailsWidget()
{
	return SNew(SRCBehaviourRangeMap, SharedThis(this));
}

TSharedRef<SWidget> FRCRangeMapBehaviourModel::GetPropertyWidget() const
{
	const TSharedRef<SVerticalBox> FieldWidget = SNew(SVerticalBox);
	
	auto DetailTreeItr = DetailTreeNodeWeakPtrArray.CreateConstIterator();

	TSharedPtr<IDetailTreeNode> DetailTreeMinValue = DetailTreeItr->Pin();
	TSharedPtr<IDetailTreeNode> DetailTreeMaxValue = (++DetailTreeItr)->Pin();
	FNodeWidgets MinValueWidget = DetailTreeMinValue->CreateNodeWidgets();
	FNodeWidgets MaxValueWidget = DetailTreeMaxValue->CreateNodeWidgets();

	if (MinValueWidget.ValueWidget && MaxValueWidget.ValueWidget)
	{
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Left)
			.AutoHeight()
			[
				CreateMinMaxWidget(DetailTreeMinValue, DetailTreeMaxValue)
			];
	}
	
	TSharedPtr<IDetailTreeNode> DetailTreeNodeThreshold = (++DetailTreeItr)->Pin();
	TSharedPtr<IDetailTreeNode> DetailTreeNodeStep = (++DetailTreeItr)->Pin();
	const FNodeWidgets ThresholdWidget = DetailTreeNodeThreshold->CreateNodeWidgets();
	const FNodeWidgets StepWidget = DetailTreeNodeStep->CreateNodeWidgets();
	
	FieldWidget->AddSlot()
		.Padding(FMargin(3.0f, 2.0f))
		.HAlign(HAlign_Center)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				ThresholdWidget.NameWidget.ToSharedRef()
			]
			+SHorizontalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				ThresholdWidget.ValueWidget.ToSharedRef()
			]
		];

	FieldWidget->AddSlot()
		.Padding(FMargin(3.0f, 6.0f))
		.HAlign(HAlign_Right)
		.AutoHeight()
		[
			SNew(SBorder)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StepInputValue", "Step Value for Action"))
					.Font(FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.BehaviourDescription"))
				]
				+SHorizontalBox::Slot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.FillWidth(1.0f)
				.AutoWidth()
				[
					StepWidget.ValueWidget.ToSharedRef()
				]
			]
		];

	return 	SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Top)
		.FillWidth(1.0f)
		.AutoWidth()
		[
			FieldWidget
		];
}

void FRCRangeMapBehaviourModel::OnActionAdded(URCAction* Action)
{
	if (URCRangeMapBehaviour* RangeMapBehaviour = Cast<URCRangeMapBehaviour>(GetBehaviour()))
	{
		if (URCPropertyAction* PropertyAction = Cast<URCPropertyAction>(Action))
		{
			RangeMapBehaviour->OnActionAdded(Action,PropertyAction->PropertySelfContainer);
		}
	}
}

TSharedPtr<SRCLogicPanelListBase> FRCRangeMapBehaviourModel::GetActionsListWidget(TSharedRef<SRCActionPanel> InActionPanel)
{
	return SNew(SRCActionPanelList<FRCActionRangeMapModel>, InActionPanel, SharedThis(this));
}

URCAction* FRCRangeMapBehaviourModel::AddAction(const TSharedRef<const FRemoteControlField> InRemoteControlField)
{
	URCAction* NewAction = nullptr;

	if (URCRangeMapBehaviour* RangeMapBehaviour = Cast<URCRangeMapBehaviour>(GetBehaviour()))
	{
		double StepValue;
		RangeMapBehaviour->PropertyContainer->GetVirtualProperty(FName("Step"))->GetValueDouble(StepValue);

		const FGuid FieldId = InRemoteControlField->GetId();
		if (TSharedPtr<FRemoteControlProperty> RemoteProperty = RangeMapBehaviour->ControllerWeakPtr.Get()->PresetWeakPtr.Get()->GetExposedEntity<FRemoteControlProperty>(FieldId).Pin())
		{
			TObjectPtr<URCVirtualPropertySelfContainer> VirtualPropertySelfContainer = NewObject<URCVirtualPropertySelfContainer>();
			VirtualPropertySelfContainer->DuplicateProperty(RemoteProperty->GetProperty()->GetFName(), RemoteProperty->GetProperty());
			
			NewAction = RangeMapBehaviour->AddAction(InRemoteControlField);

			OnActionAdded(NewAction);
		}
	}

	return NewAction;
}

TSharedRef<SWidget> FRCRangeMapBehaviourModel::CreateMinMaxWidget(TSharedPtr<IDetailTreeNode> MinRangeDetailTree, TSharedPtr<IDetailTreeNode> MaxRangeDetailTree) const
{
	const FNodeWidgets MinValueWidget = MinRangeDetailTree->CreateNodeWidgets();
	const FNodeWidgets MaxValueWidget = MaxRangeDetailTree->CreateNodeWidgets();
	
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.Padding(FMargin(3.0f, 2.0f))
		.HAlign(HAlign_Right)
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				MinValueWidget.NameWidget.ToSharedRef()
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				MinValueWidget.ValueWidget.ToSharedRef()
			]
		]
		+SHorizontalBox::Slot()
		.Padding(FMargin(3.0f, 2.0f))
		.HAlign(HAlign_Right)
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				MaxValueWidget.NameWidget.ToSharedRef()
			]
			+SVerticalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				MaxValueWidget.ValueWidget.ToSharedRef()
			]
		];
}

#undef LOCTEXT_NAMESPACE
