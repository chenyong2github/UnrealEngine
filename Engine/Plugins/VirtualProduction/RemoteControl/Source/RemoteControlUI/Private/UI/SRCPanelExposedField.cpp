// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedField.h"

#include "Algo/Transform.h"
#include "EditorStyleSet.h"
#include "Framework/SlateDelegates.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Layout/Visibility.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "RemoteControlField.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "SRCPanelDragHandle.h"
#include "SRCPanelTreeNode.h"
#include "SRemoteControlPanel.h"
#include "Styling/SlateBrush.h"
#include "ScopedTransaction.h"
#include "UObject/Object.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

namespace ExposedFieldUtils
{
	bool FindPropertyHandleRecursive(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		if (PropertyHandle && PropertyHandle->IsValidHandle())
		{
			uint32 ChildrenCount = 0;
			PropertyHandle->GetNumChildren(ChildrenCount);
			for (uint32 Index = 0; Index < ChildrenCount; ++Index)
			{
				TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
				if (FindPropertyHandleRecursive(ChildHandle, QualifiedPropertyName, bRequiresMatchingPath))
				{
					return true;
				}
			}

			if (PropertyHandle->GetProperty())
			{
				if (bRequiresMatchingPath)
				{
					if (PropertyHandle->GeneratePathToProperty() == QualifiedPropertyName)
					{
						return true;
					}
				}
				else if (PropertyHandle->GetProperty()->GetName() == QualifiedPropertyName)
				{
					return true;
				}
			}
		}

		return false;
	}

	TSharedPtr<IDetailTreeNode> FindTreeNodeRecursive(const TSharedRef<IDetailTreeNode>& RootNode, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		RootNode->GetChildren(Children);
		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(Child, QualifiedPropertyName, bRequiresMatchingPath);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		TSharedPtr<IPropertyHandle> Handle = RootNode->CreatePropertyHandle();
		if (FindPropertyHandleRecursive(Handle, QualifiedPropertyName, bRequiresMatchingPath))
		{
			return RootNode;
		}

		return nullptr;
	}

	/** Find a node by its name in a detail tree node hierarchy. */
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& RootNodes, const FString& QualifiedPropertyName, bool bRequiresMatchingPath)
	{
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(CategoryNode, QualifiedPropertyName, bRequiresMatchingPath);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		return nullptr;
	}

	TSharedRef<SWidget> CreateNodeValueWidget(const TSharedPtr<IDetailTreeNode>& Node)
	{
		FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();

		TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

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

		return FieldWidget;
	}
}

void SRCPanelExposedField::Construct(const FArguments& InArgs, const FRemoteControlField& Field)
{
	FieldType = Field.FieldType;
	FieldName = Field.FieldName;
	FieldLabel = Field.GetLabel();
	FieldPathInfo = Field.FieldPathInfo; 
	FieldId = Field.GetId();
	bEditMode = InArgs._EditMode;
	Preset = InArgs._Preset;
	bDisplayValues = InArgs._DisplayValues;

	if (FieldType == EExposedFieldType::Property)
	{
		ConstructPropertyWidget();
	}
	else
	{
		ConstructFunctionWidget();
	}
}

void SRCPanelExposedField::Tick(const FGeometry&, const double, const float)
{
	if (bNeedsRename)
	{
		if (NameTextBox)
		{
			NameTextBox->EnterEditingMode();
		}
		bNeedsRename = false;
	}
}

void SRCPanelExposedField::GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const
{
	OutChildren.Append(ChildWidgets);
}

TSharedPtr<SRCPanelExposedField> SRCPanelExposedField::AsField()
{
	return SharedThis(this);
}

FGuid SRCPanelExposedField::GetId() const
{
	return GetFieldId();
}

SRCPanelTreeNode::ENodeType SRCPanelExposedField::GetType() const
{
	return SRCPanelTreeNode::Field;
}

FName SRCPanelExposedField::GetFieldLabel() const
{
	return FieldLabel;
}

FGuid SRCPanelExposedField::GetFieldId() const
{
	return FieldId;
}

EExposedFieldType SRCPanelExposedField::GetFieldType() const
{
	return FieldType;
}

void SRCPanelExposedField::SetIsHovered(bool bInIsHovered)
{
	bIsHovered = bInIsHovered;
}

void SRCPanelExposedField::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	const TArray<TWeakObjectPtr<UObject>>& RowGeneratorObjects = RowGenerator->GetSelectedObjects();

	TArray<UObject*> NewObjectList;
	NewObjectList.Reserve(RowGeneratorObjects.Num());

	bool bObjectsReplaced = false;
	for (const TWeakObjectPtr<UObject>& Object : RowGeneratorObjects)
	{
		/** We might be looking for an object that's already been garbage collected. */
		UObject* Replacement = ReplacementObjectMap.FindRef(Object.GetEvenIfUnreachable());
		if (Replacement)
		{
			NewObjectList.Add(Replacement);
			bObjectsReplaced = true;
		}
		else
		{
			NewObjectList.Add(Object.Get());
		}
	}

	if (bObjectsReplaced)
	{
		RowGenerator->SetObjects(NewObjectList);
		ChildSlot.AttachWidget(ConstructWidget());
	}
}

void SRCPanelExposedField::Refresh()
{
	// This is needed in order to update the panel when an array is modified.
	TArray<UObject*> Objects;
	Algo::TransformIf(RowGenerator->GetSelectedObjects(), Objects,
		[](const TWeakObjectPtr<UObject>& WeakObject)
		{
			return WeakObject.IsValid();
		},
		[](const TWeakObjectPtr<UObject>& WeakObject)
		{
			return WeakObject.Get();
		});

	RowGenerator->SetObjects(Objects);
	ChildSlot.AttachWidget(ConstructWidget());
}

void SRCPanelExposedField::GetBoundObjects(TSet<UObject*>& OutBoundObjects) const
{
	OutBoundObjects.Reserve(OutBoundObjects.Num() + RowGenerator->GetSelectedObjects().Num());
	Algo::TransformIf(RowGenerator->GetSelectedObjects(), OutBoundObjects,
		[](const TWeakObjectPtr<UObject>& WeakObject)
		{
			return WeakObject.IsValid();
		},
		[](const TWeakObjectPtr<UObject>& WeakObject)
		{
			return WeakObject.Get();
		});
}


void SRCPanelExposedField::SetBoundObjects(const TArray<UObject*>& InObjects)
{
	RowGenerator->SetObjects(InObjects);
	ChildSlot.AttachWidget(ConstructWidget());
}

TSharedRef<SWidget> SRCPanelExposedField::ConstructWidget()
{
	if (bDisplayValues)
	{
		if (FieldType == EExposedFieldType::Property && RowGenerator->GetSelectedObjects().Num())
		{
			if (TSharedPtr<IDetailTreeNode> Node = ExposedFieldUtils::FindNode(RowGenerator->GetRootTreeNodes(), FieldPathInfo.ToPathPropertyString(), true))
			{
				TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
				Node->GetChildren(ChildNodes);
				ChildWidgets.Reset(ChildNodes.Num());

				for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
				{
					ChildWidgets.Add(SNew(SRCPanelFieldChildNode, ChildNode));
				}

				return MakeFieldWidget(ExposedFieldUtils::CreateNodeValueWidget(MoveTemp(Node)));
			}
		}

		return MakeFieldWidget(PanelTreeNode::CreateInvalidWidget());
	}

	return MakeFieldWidget(SNullWidget::NullWidget);
}

TSharedRef<SWidget> SRCPanelExposedField::MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
{
	PanelTreeNode::FMakeNodeWidgetArgs Args;
	Args.DragHandle = SNew(SBox)
		.Visibility(this, &SRCPanelExposedField::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
		[
			SNew(SRCPanelDragHandle<FExposedEntityDragDrop>, FieldId)
			.Widget(AsShared())
		];

	Args.NameWidget = SAssignNew(NameTextBox, SInlineEditableTextBlock)
		.Text(FText::FromName(FieldLabel))
		.OnTextCommitted(this, &SRCPanelExposedField::OnLabelCommitted)
		.OnVerifyTextChanged(this, &SRCPanelExposedField::OnVerifyItemLabelChanged)
		.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); });

	Args.RenameButton = SNew(SButton)
		.Visibility(this, &SRCPanelExposedField::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton")
		.OnClicked_Lambda([this]() {
			bNeedsRename = true;
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
				.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf044"))) /*fa-edit*/)
		];

	Args.ValueWidget = InWidget;

	Args.UnexposeButton = SNew(SButton)
		.Visibility(this, &SRCPanelExposedField::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
		.OnPressed(this, &SRCPanelExposedField::HandleUnexposeField)
		.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
		[
			SNew(STextBlock)
			.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
		];

	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(this, &SRCPanelExposedField::GetBorderImage)
		[
			PanelTreeNode::MakeNodeWidget(Args)
		];
}

EVisibility SRCPanelExposedField::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

const FSlateBrush* SRCPanelExposedField::GetBorderImage() const
{
	return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.ExposedFieldBorder");
}

void SRCPanelExposedField::HandleUnexposeField()
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		FText TransactionText;
		if (FieldType == EExposedFieldType::Function)
		{
			TransactionText = LOCTEXT("UnexposeFunction", "Unexpose Function");
		}
		else
		{
			TransactionText = LOCTEXT("UnexposeProperty", "Unexpose Property");
		}

		FScopedTransaction Transaction(MoveTemp(TransactionText));
		RCPreset->Modify();
		RCPreset->Unexpose(FieldLabel);
	}
}

bool SRCPanelExposedField::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		if (InLabel.ToString() != FieldLabel.ToString() && RCPreset->GetFieldId(FName(*InLabel.ToString())).IsValid())
		{
			OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
			return false;
		}
	}

	return true;
}

void SRCPanelExposedField::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		FScopedTransaction Transaction(LOCTEXT("RenameField", "Rename Field"));
		RCPreset->Modify();
		RCPreset->RenameField(FieldLabel, FName(*InLabel.ToString()));
		FieldLabel = FName(*InLabel.ToString());
		NameTextBox->SetText(FText::FromName(FieldLabel));
	}
}

void SRCPanelExposedField::ConstructPropertyWidget()
{
	RowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());

	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		if (TOptional<FExposedProperty> Property = RCPreset->ResolveExposedProperty(FieldLabel))
		{
			RowGenerator->SetObjects(Property->OwnerObjects);
		}
	}

	ChildSlot.AttachWidget(ConstructWidget());
}

void SRCPanelExposedField::ConstructFunctionWidget()
{
	TSharedPtr<SRCPanelExposedField> ExposedFieldWidget;
	FPropertyRowGeneratorArgs GeneratorArgs;
	GeneratorArgs.bShouldShowHiddenProperties = true;
	RowGenerator = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreatePropertyRowGenerator(GeneratorArgs);

	URemoteControlPreset* RCPreset = Preset.Get();
	if (TOptional<FRemoteControlFunction> RCFunction = RCPreset ? RCPreset->GetFunction(FieldId) : TOptional<FRemoteControlFunction>())
	{
		if (RCFunction->Function)
		{
			RowGenerator->SetStructure(RCFunction->FunctionArguments);
			
			if (bDisplayValues)
			{
				TArray<TSharedPtr<SRCPanelFieldChildNode>> ChildNodes;
				for (TFieldIterator<FProperty> It(RCFunction->Function); It; ++It)
				{
					if (!It->HasAnyPropertyFlags(CPF_Parm) || It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
					{
						continue;
					}

					if (TSharedPtr<IDetailTreeNode> PropertyNode = ExposedFieldUtils::FindNode(RowGenerator->GetRootTreeNodes(), It->GetFName().ToString(), false))
					{
						ChildNodes.Add(SNew(SRCPanelFieldChildNode, PropertyNode.ToSharedRef()));
					}
				}

				ChildSlot.AttachWidget(MakeFieldWidget(ConstructCallFunctionButton()));
				ChildWidgets = ChildNodes;
			}
			else
			{
				ChildSlot.AttachWidget(MakeFieldWidget(SNullWidget::NullWidget));
			}

			return;
		}
	}

	ChildSlot.AttachWidget(ConstructWidget());
}

TSharedRef<SWidget> SRCPanelExposedField::ConstructCallFunctionButton()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Left)
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.OnClicked_Raw(this, &SRCPanelExposedField::OnClickFunctionButton)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CallFunctionLabel", "Call Function"))
			]
		];
}

FReply SRCPanelExposedField::OnClickFunctionButton()
{
	FScopedTransaction FunctionTransaction(LOCTEXT("CallExposedFunction", "Called a function through preset."));
	FEditorScriptExecutionGuard ScriptGuard;

	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		if (TOptional<FExposedFunction> Function = RCPreset->ResolveExposedFunction(FieldLabel))
		{
			for (UObject* Object : Function->OwnerObjects)
			{
				if (Function->DefaultParameters && Function->DefaultParameters->IsValid())
				{
					Object->ProcessEvent(Function->Function, Function->DefaultParameters->GetStructMemory());
				}
				else
				{
					ensureAlwaysMsgf(false, TEXT("Function default arguments could not be resolved."));
				}
			}
		}
	}

	return FReply::Handled();
}

void SRCPanelFieldChildNode::Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	InNode->GetChildren(ChildNodes);

	Algo::Transform(ChildNodes, ChildrenNodes, [](const TSharedRef<IDetailTreeNode>& ChildNode) { return SNew(SRCPanelFieldChildNode, ChildNode); });

	FNodeWidgets Widgets = InNode->CreateNodeWidgets();
	PanelTreeNode::FMakeNodeWidgetArgs Args;
	Args.NameWidget = Widgets.NameWidget;
	Args.ValueWidget = ExposedFieldUtils::CreateNodeValueWidget(InNode);

	ChildSlot
		[
			PanelTreeNode::MakeNodeWidget(Args)
		];
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/