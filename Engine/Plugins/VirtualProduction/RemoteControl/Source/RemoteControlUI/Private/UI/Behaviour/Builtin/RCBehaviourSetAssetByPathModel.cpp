// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourSetAssetByPathModel.h"

#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "RCVirtualProperty.h"
#include "RCVirtualPropertyContainer.h"
#include "RemoteControlPreset.h"
#include "SRCBehaviourSetAssetByPath.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FRCSetAssetByPathBehaviourModel"

FRCSetAssetByPathBehaviourModel::FRCSetAssetByPathBehaviourModel(URCSetAssetByPathBehaviour* SetAssetByPathBehaviour)
	: FRCBehaviourModel(SetAssetByPathBehaviour)
	, SetAssetByPathBehaviourWeakPtr(SetAssetByPathBehaviour)
{
	FPropertyRowGeneratorArgs Args;
	Args.bShouldShowHiddenProperties = true;
	PropertyRowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);
	PropertyRowGeneratorArray = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(Args);

	if (SetAssetByPathBehaviour)
	{
		PropertyRowGenerator->SetStructure(SetAssetByPathBehaviour->PropertyInContainer->CreateStructOnScope());
		DetailTreeNodeWeakPtr.Empty();
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGenerator->GetRootTreeNodes())
		{
			TArray<TSharedRef<IDetailTreeNode>> Children;
			CategoryNode->GetChildren(Children);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				DetailTreeNodeWeakPtr.Add(Child);
			}
		}

		// Secondary TArray Struct
		PropertyRowGeneratorArray->SetStructure(MakeShareable(new FStructOnScope(FRCSetAssetPath::StaticStruct(), (uint8*) &SetAssetByPathBehaviour->PathStruct)));
		PropertyRowGeneratorArray->OnRowsRefreshed().AddLambda([this]()
		{
			RegeneratePathArrayWidget();
		});
		DetailTreeNodeWeakPtrArray.Empty();
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGeneratorArray->GetRootTreeNodes())
		{
			TArray<TSharedRef<IDetailTreeNode>> Children;
			CategoryNode->GetChildren(Children);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				DetailTreeNodeWeakPtrArray.Add(Child);
			}
		}
	}
	PathArrayWidget = SNew(SBox);
	RegeneratePathArrayWidget();
}

TSharedRef<SWidget> FRCSetAssetByPathBehaviourModel::GetBehaviourDetailsWidget()
{
	return SNew(SRCBehaviourSetAssetByPath, SharedThis(this));
}

TSharedRef<SWidget> FRCSetAssetByPathBehaviourModel::GetPropertyWidget() const
{
	TSharedRef<SVerticalBox> FieldWidget = SNew(SVerticalBox);
	
	const TSharedPtr<IDetailTreeNode> PinnedNodeTargetProperty = DetailTreeNodeWeakPtr[0];
	const TSharedPtr<IDetailTreeNode> PinnedNodeDefault = DetailTreeNodeWeakPtr[1];

	const FNodeWidgets NodeWidgetsTarget = PinnedNodeTargetProperty->CreateNodeWidgets();
	const FNodeWidgets NodeWidgetsDefault = PinnedNodeDefault->CreateNodeWidgets();
	if (NodeWidgetsTarget.ValueWidget && NodeWidgetsDefault.ValueWidget)
	{
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					NodeWidgetsTarget.NameWidget.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				[
					NodeWidgetsDefault.NameWidget.ToSharedRef()
				]
			];
		
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					NodeWidgetsTarget.ValueWidget.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				[
					NodeWidgetsDefault.ValueWidget.ToSharedRef()
				]
			];
	}
	else if (NodeWidgetsTarget.WholeRowWidget && NodeWidgetsDefault.WholeRowWidget)
	{
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoHeight()
			[
				NodeWidgetsTarget.WholeRowWidget.ToSharedRef()
			];
		
		FieldWidget->AddSlot()
			.Padding(FMargin(3.0f, 2.0f))
			.AutoHeight()
			[
				NodeWidgetsDefault.WholeRowWidget.ToSharedRef()
			];
	}

	FieldWidget->AddSlot()
		.Padding(FMargin(3.f, 0.f))
		.AutoHeight()
		[
			PathArrayWidget->AsShared()
		];
	
	return 	SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.FillWidth(1.0f)
		[
			FieldWidget
		];
}

void FRCSetAssetByPathBehaviourModel::RegenerateWeakPtrInternal()
{
	DetailTreeNodeWeakPtrArray.Empty();
	for (const TSharedRef<IDetailTreeNode>& CategoryNode : PropertyRowGeneratorArray->GetRootTreeNodes())
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		CategoryNode->GetChildren(Children);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			DetailTreeNodeWeakPtrArray.Add(Child);
		}
	}
}

void FRCSetAssetByPathBehaviourModel::RegeneratePathArrayWidget()
{
	TSharedPtr<SVerticalBox> FieldArrayWidget = SNew(SVerticalBox);

	RegenerateWeakPtrInternal();
	
	for (const TSharedPtr<IDetailTreeNode>& DetailTreeNodeArray : DetailTreeNodeWeakPtrArray)
	{
		const TSharedPtr<IDetailTreeNode> PinnedNode = DetailTreeNodeArray;

		TArray<TSharedRef<IDetailTreeNode>> Children;
		PinnedNode->GetChildren(Children);
		Children.Insert(PinnedNode.ToSharedRef(), 0);
		
		for (uint8 Counter = 0; Counter < Children.Num(); Counter++)
		{
			const FNodeWidgets NodeWidgets = Children[Counter]->CreateNodeWidgets();
			if (NodeWidgets.ValueWidget)
			{
				FieldArrayWidget->AddSlot()
					.Padding(FMargin(3.0f, 2.0f))
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						[
							NodeWidgets.NameWidget.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						[
							NodeWidgets.ValueWidget.ToSharedRef()
						]
					];
			}
			else if (NodeWidgets.WholeRowWidget)
			{
				FieldArrayWidget->AddSlot()
					.Padding(FMargin(3.0f, 2.0f))
					.AutoHeight()
					[
						NodeWidgets.WholeRowWidget.ToSharedRef()
					];
			}
		}
	}
	PathArrayWidget->SetContent(FieldArrayWidget.ToSharedRef());
}

#undef LOCTEXT_NAMESPACE
