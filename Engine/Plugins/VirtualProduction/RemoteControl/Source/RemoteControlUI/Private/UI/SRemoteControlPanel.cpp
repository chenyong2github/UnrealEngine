// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "Algo/ForEach.h"
#include "Application/SlateApplicationBase.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Engine/Engine.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IPropertyRowGenerator.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlPanelStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "RemoteControlUIModule.h"
#include "SDropTarget.h"
#include "SSearchableItemList.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Templates/SharedPointer.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

namespace RemoteControlPanelUtil
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

	/** Handle creating the widget to select a function.  */
	TArray<UFunction*> GetExposableFunctions(UClass* Class)
	{
		auto FunctionFilter = [](const UFunction* TestFunction)
		{
			return TestFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_Public);
		};

		TArray<UFunction*> ExposableFunctions;
		TSet<FName> BaseActorFunctionNames;

		for (TFieldIterator<UFunction> FunctionIter(AActor::StaticClass(), EFieldIteratorFlags::IncludeSuper); FunctionIter; ++FunctionIter)
		{
			if (FunctionFilter(*FunctionIter))
			{
				BaseActorFunctionNames.Add(FunctionIter->GetFName());
			}
		}

		for (TFieldIterator<UFunction> FunctionIter(Class, EFieldIteratorFlags::IncludeSuper); FunctionIter; ++FunctionIter)
		{
			UFunction* TestFunction = *FunctionIter;

			if (FunctionFilter(TestFunction)
				&& !BaseActorFunctionNames.Contains(TestFunction->GetFName()))
			{
				if (!ExposableFunctions.FindByPredicate([FunctionName = TestFunction->GetFName()](const UObject* Func) { return Func->GetFName() == FunctionName; }))
				{
					ExposableFunctions.Add(*FunctionIter);
				}
			}
		}

		return ExposableFunctions;
	}

	const FString DefaultPrefix = TEXT("Default__");
	const FString CPostfix = TEXT("_C");

	struct FRCFunctionNode : public FRCBlueprintPickerTreeNode
	{
		FRCFunctionNode(UBlueprintFunctionLibrary* InOwner, UFunction* InFunction)
			: Owner(InOwner)
			, Function(InFunction)
		{
			if (Function)
			{
				Name = Function->GetName();
			}
		}
		
		//~ Begin FRCBlueprintPickerTreeNode interface
		virtual const FString& GetName() const override { return Name; }
		virtual bool IsFunctionNode() const override { return true; }
		virtual UFunction* GetFunction() const override { return Function; }
		virtual UBlueprintFunctionLibrary* GetLibrary() const override { return Owner; }
		virtual void GetChildNodes(TArray<TSharedPtr<FRCBlueprintPickerTreeNode>>& OutNodes) const {}
		//~ End FRCBlueprintPickerTreeNode interface

	private:
		/** The blueprint function library that owns this node's function. */
		UBlueprintFunctionLibrary* Owner = nullptr;
		/** This node's function. */
		UFunction* Function = nullptr;
		/** This node's function's name. */
		FString Name;
	};

	struct FRCLibraryNode : public FRCBlueprintPickerTreeNode
	{
		FRCLibraryNode(UBlueprintFunctionLibrary* Library)
			: WeakLibrary(Library)
		{
			if (ensure(Library && Library->GetFName().IsValid() && Library->GetFName() != NAME_None))
			{
				Name = Library->GetName();
				Name.RemoveFromStart(DefaultPrefix, ESearchCase::CaseSensitive);
				Name.RemoveFromEnd(CPostfix, ESearchCase::CaseSensitive);

				for (UFunction* Function : GetExposableFunctions(Library->GetClass()))
				{
					if (Function)
					{
						TSharedPtr<FRCFunctionNode> FunctionNode = MakeShared<FRCFunctionNode>(Library, Function);
						ChildNodes.Add(FunctionNode);
					}
				}
			}
		}

		//~ Begin FRCBlueprintPickerTreeNode interface
		virtual const FString& GetName() const override { return Name; }
		virtual bool IsFunctionNode() const override { return true; }
		virtual UBlueprintFunctionLibrary* GetLibrary() const override { return WeakLibrary.Get(); }
		virtual UFunction* GetFunction() const override{ return nullptr; }
		virtual void GetChildNodes(TArray<TSharedPtr<FRCBlueprintPickerTreeNode>>& OutNodes) const override { OutNodes.Append(ChildNodes); }
		//~ End FRCBlueprintPickerTreeNode

	private:
		/** The blueprint library represented by this node. */
		TWeakObjectPtr<UBlueprintFunctionLibrary> WeakLibrary;
		/** This node's child functions. */
		TArray<TSharedPtr<FRCBlueprintPickerTreeNode>> ChildNodes;
		/** This blueprint library's name. */
		FString Name;
	};

	TSharedRef<SWidget> CreateInvalidWidget()
	{
		return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f))
			.Text(LOCTEXT("WidgetCannotBeDisplayed", "Widget cannot be displayed."))
		];
	}

	struct FMakeFieldWidgetArgs
	{
		TSharedPtr<SWidget> DragHandle;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> RenameButton;
		TSharedPtr<SWidget> ValueWidget;
		TSharedPtr<SWidget> OptionsButton;
		TSharedPtr<SWidget> UnexposeButton;
	};

	TSharedRef<SWidget> MakeFieldWidget(const FMakeFieldWidgetArgs& Args)
	{
		auto WidgetOrNull = [](const TSharedPtr<SWidget>& Widget){return Widget ? Widget.ToSharedRef() : SNullWidget::NullWidget; };

		return SNew(SHorizontalBox)
		// Drag and drop handle
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.DragHandle)
		]
		// Field name
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.NameWidget)
		]
		// Rename button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.RenameButton)
		]
		// Field value
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.FillWidth(1.0f)
		[
			WidgetOrNull(Args.ValueWidget)
		]
		// Show options button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.OptionsButton)
		]
		// Unexpose button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.UnexposeButton)
		];
	}
}

class FExposedFieldDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExposedFieldDragDropOp, FDecoratedDragDropOp)

	FExposedFieldDragDropOp(TSharedPtr<SWidget> InWidget)
		: FieldWidget(InWidget)
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Active"))
			.Content()
			[
				FieldWidget.ToSharedRef()
			];

		Construct();
	}

	TSharedPtr<SRCPanelExposedField> GetFieldWidget() const
	{
		return StaticCastSharedPtr<SRCPanelExposedField>(FieldWidget);
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

	TSharedPtr<SWidget> FieldWidget;
	TSharedPtr<SWidget> DecoratorWidget;
};


class FFieldGroupDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFieldGroupDragDropOp, FDragDropOperation)

		FFieldGroupDragDropOp(TSharedPtr<SFieldGroup> InWidget)
		: FieldGroupWidget(InWidget)
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(0.f)
			.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			.Content()
			[
				FieldGroupWidget.ToSharedRef()
			];

		Construct();
	}

	TSharedPtr<FRCPanelGroup> GetDragOriginGroup() const
	{
		if (TSharedPtr<SFieldGroup> GroupWidget = GetFieldGroupWidget())
		{
			return GroupWidget->GetGroup();
		}
		return nullptr;
	}

	TSharedPtr<SFieldGroup> GetFieldGroupWidget() const
	{
		return StaticCastSharedPtr<SFieldGroup>(FieldGroupWidget);
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

	TSharedPtr<SWidget> FieldGroupWidget;
	TSharedPtr<SWidget> DecoratorWidget;
};

class SDragHandle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDragHandle)
	{}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, FieldWidget)
		SLATE_ARGUMENT(TSharedPtr<SFieldGroup>, GroupWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		FieldWidget = InArgs._FieldWidget;
		GroupWidget = InArgs._GroupWidget;
		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(25.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(5.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("VerticalBoxDragIndicatorShort"))
				]
			]
		];
	}

	FReply OnMouseButtonDown(const FGeometry & MyGeometry, const FPointerEvent & MouseEvent) override
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	};

	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			TSharedPtr<FDragDropOperation> DragDropOp = CreateDragDropOperation();
			if (DragDropOp.IsValid())
			{
				return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
			}
		}

		return FReply::Unhandled();
	}

	TSharedPtr<FDragDropOperation> CreateDragDropOperation()
	{
		if (TSharedPtr<SWidget> FieldWidgetPtr = FieldWidget.Pin())
		{
			return MakeShared<FExposedFieldDragDropOp>(MoveTemp(FieldWidgetPtr));
		}
		else if (TSharedPtr<SFieldGroup> GroupWidgetPtr = GroupWidget.Pin())
		{
			return MakeShared<FFieldGroupDragDropOp>(MoveTemp(GroupWidgetPtr));
		}

		return nullptr;
	}

private:
	TWeakPtr<SWidget> FieldWidget;
	TWeakPtr<SFieldGroup> GroupWidget;
};

void FRCPanelFieldChildNode::Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	InNode->GetChildren(ChildNodes);

	Algo::Transform(ChildNodes, ChildrenNodes, [](const TSharedRef<IDetailTreeNode>& ChildNode) { return SNew(FRCPanelFieldChildNode, ChildNode); });
	
	FNodeWidgets Widgets = InNode->CreateNodeWidgets();
	RemoteControlPanelUtil::FMakeFieldWidgetArgs Args;
	Args.NameWidget = Widgets.NameWidget;
	Args.ValueWidget = RemoteControlPanelUtil::CreateNodeValueWidget(InNode);

	ChildSlot
	[
		RemoteControlPanelUtil::MakeFieldWidget(Args)
	];
}

void SRCPanelExposedField::Construct(const FArguments& InArgs, const FRemoteControlField& Field, TSharedRef<IPropertyRowGenerator> InRowGenerator, TWeakPtr<SRemoteControlPanel> InPanel)
{
	FieldType = Field.FieldType;
	FieldName = Field.FieldName;
	FieldLabel = Field.Label;
	FieldPathInfo = Field.FieldPathInfo;
	FieldId = Field.Id;
	RowGenerator = MoveTemp(InRowGenerator);
	OptionsWidget = InArgs._OptionsContent.Widget;
	bEditMode = InArgs._EditMode;
	if (InArgs._ChildWidgets)
	{
		ChildWidgets = *InArgs._ChildWidgets;
	}

	WeakPanel = MoveTemp(InPanel);

	if (InArgs._Content.Widget != SNullWidget::NullWidget)
	{
		ChildSlot
			[
				MakeFieldWidget(InArgs._Content.Widget)
			];
	}
	else
	{
		Refresh();
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

void SRCPanelExposedField::GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren)
{
	OutChildren.Append(ChildWidgets);
}

TSharedPtr<SRCPanelExposedField> SRCPanelExposedField::AsField()
{
	return SharedThis(this);
}

FGuid SRCPanelExposedField::GetId()
{
	return GetFieldId();
}

FRCPanelTreeNode::ENodeType SRCPanelExposedField::GetType()
{
	return FRCPanelTreeNode::Field;
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
	if (FieldType == EExposedFieldType::Property)
	{
		if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), FieldPathInfo.ToPathPropertyString(), true))
		{
			TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
			Node->GetChildren(ChildNodes);
			ChildWidgets.Reset(ChildNodes.Num());

			for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
			{
				ChildWidgets.Add(SNew(FRCPanelFieldChildNode, ChildNode));
			}

			return MakeFieldWidget(RemoteControlPanelUtil::CreateNodeValueWidget(MoveTemp(Node)));
		}
	}
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SRCPanelExposedField::MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
{
	RemoteControlPanelUtil::FMakeFieldWidgetArgs Args;
	Args.DragHandle = SNew(SBox)
		.Visibility(this, &SRCPanelExposedField::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
		[
			SNew(SDragHandle)
			.FieldWidget(AsShared())
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
						.OnClicked(this, &SRCPanelExposedField::HandleUnexposeField)
						.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
						];
	Args.OptionsButton = SNew(SButton)
						.Visibility(this, &SRCPanelExposedField::GetOptionsButtonVisibility)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton")
						.OnClicked_Lambda([this]() { bShowOptions = !bShowOptions; return FReply::Handled(); })
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
						];

	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(this, &SRCPanelExposedField::GetBorderImage)
		[
			RemoteControlPanelUtil::MakeFieldWidget(Args)
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

FReply SRCPanelExposedField::HandleUnexposeField()
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
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
		Panel->GetPreset()->Modify();
		Panel->GetPreset()->Unexpose(FieldLabel);
	}
	return FReply::Handled();
}

EVisibility SRCPanelExposedField::GetOptionsButtonVisibility() const
{
	if (OptionsWidget == SNullWidget::NullWidget)
	{
		return EVisibility::Collapsed;
	}

	return GetVisibilityAccordingToEditMode(EVisibility::Collapsed);
}

bool SRCPanelExposedField::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		if (URemoteControlPreset* Preset = Panel->GetPreset())
		{
			if (InLabel.ToString() != FieldLabel.ToString() && Panel->GetPreset()->GetFieldId(FName(*InLabel.ToString())).IsValid())
			{
				OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
				return false;
			}
		}
	}

	return true;
}

void SRCPanelExposedField::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		if (URemoteControlPreset* Preset = Panel->GetPreset())
		{
			FScopedTransaction Transaction(LOCTEXT("RenameField", "Rename Field"));
			Preset->Modify();
			Preset->RenameField(FieldLabel, FName(*InLabel.ToString()));
			FieldLabel = FName(*InLabel.ToString());
			NameTextBox->SetText(FText::FromName(FieldLabel));
		}
	}
}

void FRCPanelGroup::GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren)
{
	OutChildren.Append(Fields);
}

FGuid FRCPanelGroup::GetId()
{
	return Id;
}

FRCPanelTreeNode::ENodeType FRCPanelGroup::GetType()
{
	return FRCPanelTreeNode::Group;
}

TSharedPtr<FRCPanelGroup> FRCPanelGroup::AsGroup()
{
	return SharedThis(this);
}

/** Wrapper around a weak object pointer and the object's name. */
struct FListEntry
{
	FString Name;
	FSoftObjectPtr ObjectPtr;
};

FExposableProperty::FExposableProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle, const TArray<UObject*>& InOwnerObjects)
{
	if (!PropertyHandle || !PropertyHandle->IsValidHandle() || !PropertyHandle->GetProperty())
	{
		return;
	}

	PropertyDisplayName = PropertyHandle->GetPropertyDisplayName().ToString();
	PropertyName = PropertyHandle->GetProperty()->GetFName();

	if (InOwnerObjects.Num() > 0 && InOwnerObjects[0])
	{
		//Build qualified property path for this field
		constexpr bool bCleanDuplicates = true; //GeneratePathToProperty duplicates container name (Array.Array[1], Set.Set[1], etc...)
		FieldPathInfo = FRCFieldPathInfo(PropertyHandle->GeneratePathToProperty(), bCleanDuplicates);
		
		//Build the component hierarchy if any
		if (InOwnerObjects[0] && InOwnerObjects[0]->IsA<UActorComponent>())
		{
			UObject* CurrentOuter = InOwnerObjects[0];
			for (;;)
			{
				if (!CurrentOuter || CurrentOuter->IsA<AActor>())
				{
					break;
				}
				ComponentChain.Insert(CurrentOuter->GetName(), 0);
				CurrentOuter = CurrentOuter->GetOuter();
			}
		}

		// Components are not supported as top level objects, so use their parent actor instead.
		for (UObject* OwnerObject : InOwnerObjects)
		{
			if (UActorComponent* ActorComponent = Cast<UActorComponent>(OwnerObject))
			{
				OwnerObjects.Add(ActorComponent->GetOwner());
			}
			else
			{
				OwnerObjects.Add(OwnerObject);
			}
		}
	}
}

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);

	TArray<TSharedRef<SWidget>> ExtensionWidgets;
	FRemoteControlUIModule::Get().GetExtensionGenerators().Broadcast(ExtensionWidgets);

	RegisterPresetDelegates();
	RegisterEvents();

	TSharedPtr<SHorizontalBox> TopExtensions;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			// Top tool bar
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[	
				SNew(SButton)
				.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
				.ButtonStyle(FEditorStyle::Get(), "FlatButton")
				.OnClicked(this, &SRemoteControlPanel::OnCreateGroup)
				[
					SNew(STextBlock)
					.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FText::FromString(FString(TEXT("\xf07b"))) /*fa-plus-square-o*/)
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(5.0f, 0.0f))
			.AutoWidth()
			[
				CreateBlueprintLibraryPicker()
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.FillWidth(1.0f)
			.Padding(0, 7.0f)
			[
				SAssignNew(TopExtensions, SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EditModeLabel", "Edit Mode: "))
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return this->bIsInEditMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &SRemoteControlPanel::OnEditModeCheckboxToggle)
				]
			]
		]
		+ SVerticalBox::Slot()
		[
			SAssignNew(TreeView, STreeView<TSharedPtr<FRCPanelTreeNode>>)
			.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<FRCPanelTreeNode>>*>(&FieldGroups))
			.ItemHeight(24.0f)
			.OnGenerateRow(this, &SRemoteControlPanel::OnGenerateRow)
			.OnGetChildren(this, &SRemoteControlPanel::OnGetGroupChildren)
			.OnSelectionChanged(this, &SRemoteControlPanel::OnSelectionChanged)
			.ClearSelectionOnClick(false)
		]
	];

	for (const TSharedRef<SWidget>& Widget : ExtensionWidgets)
	{
		// We want to insert the widgets before the edit mode buttons.
		TopExtensions->InsertSlot(TopExtensions->NumSlots()-2)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			Widget
		];
	}

	Refresh();
}

void SRemoteControlPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bTriggerRefreshForPIE)
	{
		for (TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
		{
			for (TSharedPtr<SRCPanelExposedField> Field : Target->GetFieldWidgets())
			{
				if (TOptional<FExposedProperty> Property = Preset->ResolveExposedProperty(Field->GetFieldLabel()))
				{
					Field->SetBoundObjects(Property->OwnerObjects);
				}
			}
		}

		TreeView->RequestListRefresh();
		bTriggerRefreshForPIE = false;
	}
}

SRemoteControlPanel::~SRemoteControlPanel()
{
	UnregisterEvents();
	UnregisterPresetDelegates();
}

void SRemoteControlPanel::PostUndo(bool bSuccess)
{
	Refresh();
}

void SRemoteControlPanel::PostRedo(bool bSuccess)
{
	Refresh();
}

bool SRemoteControlPanel::IsExposed(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	bool bAllObjectsExposed = true;
	for (UObject* Object : OuterObjects)
	{
		bool bObjectExposed = false;
		FExposableProperty Property{ PropertyHandle, { Object }};
		if (Property.IsValid())
		{
			for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
			{
				FRemoteControlTarget& Target = Tuple.Value;
				if (Target.HasBoundObjects({ Property.OwnerObjects[0]}))
				{
					if (Target.FindFieldLabel(Property.FieldPathInfo) == NAME_None)
					{
						return false;
					}
					else
					{
						bObjectExposed = true;
					}
				}
			}
		}
		bAllObjectsExposed &= bObjectExposed;
	}
	return bAllObjectsExposed;
}

void SRemoteControlPanel::ToggleProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	if (IsExposed(PropertyHandle))
	{
		FScopedTransaction Transaction(LOCTEXT("UnexposeProperty", "Unexpose Property"));
		Preset->Modify();
		Unexpose(PropertyHandle);
		return;
	}

	for (UObject* Object : OuterObjects)
	{
		FScopedTransaction Transaction(LOCTEXT("ExposeProperty", "Expose Property"));
		Preset->Modify();
		Expose({ PropertyHandle, { Object } });
	}
}

FRemoteControlPresetLayout& SRemoteControlPanel::GetLayout()
{
	return Preset->Layout;
}

void SRemoteControlPanel::RegisterEvents()
{
	OnPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SRemoteControlPanel::OnObjectPropertyChange);

	MapChangedHandle = FEditorDelegates::MapChange.AddLambda([this](uint32) { Refresh(); });
	GEngine->OnLevelActorDeleted().AddSP(this, &SRemoteControlPanel::OnActorDeleted);

	if (GEditor)
	{
		FEditorDelegates::PostPIEStarted.AddSP(this, &SRemoteControlPanel::OnPieEvent);
		FEditorDelegates::EndPIE.AddSP(this, &SRemoteControlPanel::OnPieEvent);

		GEditor->OnObjectsReplaced().AddSP(this, &SRemoteControlPanel::OnObjectsReplaced);
		GEditor->OnBlueprintReinstanced().AddSP(this, &SRemoteControlPanel::RefreshBlueprintLibraryNodes);
	}
}

void SRemoteControlPanel::UnregisterEvents()
{
	if (GEditor)
	{
		GEditor->OnBlueprintReinstanced().RemoveAll(this);
		GEditor->OnObjectsReplaced().RemoveAll(this);

		FEditorDelegates::EndPIE.RemoveAll(this);
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
	}

	GEngine->OnLevelActorDeleted().RemoveAll(this);

	FEditorDelegates::MapChange.Remove(MapChangedHandle);

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandle);
}

void SRemoteControlPanel::Refresh()
{
	if (Preset)
	{
		RefreshBlueprintLibraryNodes();
		GenerateFieldWidgets();
		RefreshGroups();
		
		for (const TSharedPtr<FRCPanelGroup>& Group : FieldGroups)
		{
			TreeView->SetItemExpansion(Group, true);
		}

		for (const TTuple<FGuid, TSharedPtr<SRCPanelExposedField>>& FieldTuple : FieldWidgetMap)
		{
			TreeView->SetItemExpansion(FieldTuple.Value, false);
		}
	}
}

void SRemoteControlPanel::OnSelectionChanged(TSharedPtr<FRCPanelTreeNode> Node, ESelectInfo::Type SelectInfo)
{
	if (!Node || SelectInfo != ESelectInfo::OnMouseClick)
	{
		return;
	}

	if (TSharedPtr<SRCPanelExposedField> Field = Node->AsField())
	{
		TSet<UObject*> Objects;
		Field->GetBoundObjects(Objects);

		TArray<UObject*> OwnerActors;
		for (UObject* Object : Objects)
		{
			if (Object->IsA<AActor>())
			{
				OwnerActors.Add(Object);
			}
			else
			{
				OwnerActors.Add(Object->GetTypedOuter<AActor>());
			}
		}
		
		for (UObject* Object : Objects)
		{
			SelectActorsInlevel(OwnerActors);
		}
	}
}

FRemoteControlTarget* SRemoteControlPanel::Expose(FExposableProperty&& Property)
{
	if (!Property.IsValid())
	{
		return nullptr;
	}

	FRemoteControlTarget* LastModifiedTarget = nullptr;

	FGuid GroupId;
	TArray<TSharedPtr<FRCPanelTreeNode>> SelectedGroups = TreeView->GetSelectedItems();
	if (SelectedGroups.Num() && SelectedGroups[0])
	{
		if (SelectedGroups[0]->GetType() == FRCPanelTreeNode::Group)
		{
			GroupId = SelectedGroups[0]->GetId();
		}
	}

	auto ExposePropertyLambda = 
		[this, &LastModifiedTarget, GroupId](FRemoteControlTarget& Target, const FExposableProperty& Property)
		{ 
			if (TOptional<FRemoteControlProperty> RCProperty = Target.ExposeProperty(Property.FieldPathInfo, Property.ComponentChain, Property.PropertyDisplayName, GroupId))
			{
				LastModifiedTarget = &Target;
			}
		};

	// Find a section with the same object.
	for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
	{
		FRemoteControlTarget& Target = Tuple.Value;
		if (Target.HasBoundObjects(Property.OwnerObjects))
		{
			ExposePropertyLambda(Target, Property);
		}
	}
	
	// If no section was found create a new one.
	if (!LastModifiedTarget)
	{
		// If grouping is disallowed, create a new section for every object
		FName TargetAlias = Preset->CreateTarget(Property.OwnerObjects);
		if (TargetAlias != NAME_None)
		{
			LastModifiedTarget = &Preset->GetRemoteControlTargets().FindChecked(TargetAlias);
			ExposePropertyLambda(*LastModifiedTarget, Property);
		}
	}

	return LastModifiedTarget;
}

void SRemoteControlPanel::Unexpose(const TSharedPtr<IPropertyHandle>& Handle)
{
	TArray<UObject*> OuterObjects;
	Handle->GetOuterObjects(OuterObjects);
	for (UObject* Object : OuterObjects)
	{
		FExposableProperty Property{ Handle, { Object } };
		for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
		{
			if (Tuple.Value.HasBoundObjects({ Property.OwnerObjects[0] }))
			{
				Preset->Unexpose(Tuple.Value.FindFieldLabel(Property.FieldPathInfo));
			}
		}
	}
}

void SRemoteControlPanel::RefreshBlueprintLibraryNodes()
{
	using namespace RemoteControlPanelUtil;

	BlueprintLibraryNodes.Reset();
	TSet<FName> LibraryNames;

	auto LibraryFilter = [&LibraryNames](UBlueprintFunctionLibrary* Library)
	{	
		FName Name = Library->GetClass()->GetFName();
		const FString& NameStr = Library->GetClass()->GetName();

		return !LibraryNames.Contains(Library->GetClass()->GetFName())
			&& Library->GetClass() != UBlueprintFunctionLibrary::StaticClass()
			&& Name.IsValid() 
			&& Name != NAME_None
			&& !Library->GetClass()->HasAllClassFlags(CLASS_NewerVersionExists | CLASS_CompiledFromBlueprint)
			&& !NameStr.Contains(TEXT("SKEL"), ESearchCase::CaseSensitive);
	};

	for (auto It = TObjectIterator<UBlueprintFunctionLibrary>(EObjectFlags::RF_NoFlags); It; ++It)
	{
		if (LibraryFilter(*It))
		{
			UClass* Class = It->GetClass();
			TSharedPtr<FRCLibraryNode> Node = MakeShared<FRCLibraryNode>(*It);

			BlueprintLibraryNodes.Add(Node);
			LibraryNames.Add(It->GetClass()->GetFName());
		}
	}

	if (BlueprintLibrariesTreeView)
	{
		BlueprintLibrariesTreeView->Refresh();
	}
}

TSharedRef<SWidget> SRemoteControlPanel::CreateBlueprintLibraryPicker()
{
	using namespace RemoteControlPanelUtil;

	auto OnGetChildren = []
	(TSharedPtr<FRCBlueprintPickerTreeNode> Node, TArray<TSharedPtr<FRCBlueprintPickerTreeNode>>& OutChildren)
	{
		Node->GetChildNodes(OutChildren);
	};

	return SNew(SComboButton)
		.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FSlateColor::UseForeground())
		.OnComboBoxOpened_Lambda([this]()
			{
				if (BlueprintLibrariesTreeView)
				{
					BlueprintLibrariesTreeView->ClearSearchBox();
					BlueprintLibrariesTreeView->Focus();
				}
			})
		.CollapseMenuOnParentFocus(true)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(2.0f, 0.0f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
				.Text(LOCTEXT("FunctionLibrariesLabel", "Function Libraries"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				.Image(FEditorStyle::Get().GetBrush("GraphEditor.Function_16x"))
			]
		]
		.MenuContent()
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			.MaxDesiredHeight(200.0f)
			[
				SAssignNew(BlueprintLibrariesTreeView, SSearchableTreeView<TSharedPtr<FRCBlueprintPickerTreeNode>>)
				.Items(&BlueprintLibraryNodes)
				.OnGetChildren_Lambda(OnGetChildren)
				.OnGetDisplayName_Lambda([](TSharedPtr<FRCBlueprintPickerTreeNode> InEntry) { return InEntry->GetName(); })
				.IsSelectable_Lambda([](TSharedPtr<FRCBlueprintPickerTreeNode> TreeNode) { return TreeNode->IsFunctionNode(); })
				.OnItemSelected_Lambda(
					[this] (TSharedPtr<FRCBlueprintPickerTreeNode> TreeNode) 
					{
						UBlueprintFunctionLibrary* Owner = TreeNode->GetLibrary();
						UFunction* Function = TreeNode->GetFunction();
						if (Owner && Function)
						{
							ExposeFunction(Owner, Function);
							FSlateApplication::Get().SetUserFocus(0, SharedThis(this));
						}
					})
			]
		];
}

TSharedRef<ITableRow> SRemoteControlPanel::OnGenerateRow(TSharedPtr<FRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (Node->GetType() == FRCPanelTreeNode::Group)
	{
		return SNew(SFieldGroup, OwnerTable, Node->AsGroup(), SharedThis<SRemoteControlPanel>(this))
				.OnFieldDropEvent_Raw(this, &SRemoteControlPanel::OnDropOnGroup)
				.OnGetGroupId_Raw(this, &SRemoteControlPanel::GetGroupId)
				.OnDeleteGroup_Raw(this, &SRemoteControlPanel::OnDeleteGroup)
				.EditMode_Lambda([this] () { return bIsInEditMode; });
	}
	else if (Node->GetType() == FRCPanelTreeNode::Field)
	{
		auto OnDropLambda = [this, Field = Node->AsField()]
		(const FDragDropEvent& Event)
		{
			if (TSharedPtr<FExposedFieldDragDropOp> DragDropOp = Event.GetOperationAs<FExposedFieldDragDropOp>())
			{
				FGuid GroupId = GetGroupId(Field);
				if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& TargetGroup) { return TargetGroup->Id == GroupId; }))
				{
					if (DragDropOp->IsOfType<FExposedFieldDragDropOp>())
					{
						return OnDropOnGroup(DragDropOp, Field, *Group);
					}
					else if (DragDropOp->IsOfType<FFieldGroupDragDropOp>())
					{
						return OnDropOnGroup(DragDropOp, nullptr, *Group);
					}
				}
			}

			return FReply::Unhandled();
		};

		return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
		.OnDragEnter_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(true); })
		.OnDragLeave_Lambda([Field = Node->AsField()](const FDragDropEvent& Event) { Field->SetIsHovered(false); })
		.OnDrop_Lambda(OnDropLambda)
		.Padding(FMargin(20.f, 0.f, 0.f, 0.f))
		.ShowSelection(false)
		[
			Node->AsField().ToSharedRef()
		];
	}
	else
	{
		return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
		.Padding(FMargin(30.f, 0.f, 0.f, 0.f))
		.ShowSelection(false)
		[
			Node->AsFieldChild().ToSharedRef()
		];
	}
}

void SRemoteControlPanel::OnGetGroupChildren(TSharedPtr<FRCPanelTreeNode> Node, TArray<TSharedPtr<FRCPanelTreeNode>>& OutNodes)
{
	if (Node.IsValid())
	{
		Node->GetNodeChildren(OutNodes);
	}
}

void SRemoteControlPanel::SelectActorsInlevel(const TArray<UObject*>& Objects)
{
	if (GEditor)
	{
		// Don't change selection if the target's component is already selected
		USelection* Selection = GEditor->GetSelectedComponents();
		if (Selection->Num() == 1 && Objects.Num() == 1 && Selection->GetSelectedObject(0)->GetTypedOuter<AActor>() == Objects[0])
		{
			return;
		}

		GEditor->SelectNone(false, true, false);

		for (UObject* Object : Objects)
		{
			if (AActor* Actor = Cast<AActor>(Object))
			{
				GEditor->SelectActor(Actor, true, true, true);
			}
		}
	}
}

void SRemoteControlPanel::GenerateFieldWidgets()
{
	TMap<FName, FRemoteControlTarget>& TargetMap = Preset->GetRemoteControlTargets();

	RemoteControlTargets.Reset(TargetMap.Num());
	FieldWidgetMap.Reset();

	TSharedRef<SRemoteControlPanel> PanelPtr = SharedThis<SRemoteControlPanel>(this);
	for (TTuple<FName, FRemoteControlTarget>& MapEntry : TargetMap)
	{
		TSharedRef<SRemoteControlTarget> Target = MakeShared<SRemoteControlTarget>(MapEntry.Key, PanelPtr);
		RemoteControlTargets.Add(Target);
		for (const TSharedRef<SRCPanelExposedField>& Widget : Target->GetFieldWidgets())
		{
			FieldWidgetMap.Add(Widget->GetFieldId(), Widget);
		}
	}
}

void SRemoteControlPanel::RefreshGroups()
{
	FieldGroups.Reset(Preset->Layout.GetGroups().Num());
	
	for (const FRemoteControlPresetGroup& RCGroup : Preset->Layout.GetGroups())
	{
		TSharedPtr<FRCPanelGroup> NewGroup = MakeShared<FRCPanelGroup>(RCGroup.Name, RCGroup.Id, SharedThis(this));
		FieldGroups.Add(NewGroup);
		NewGroup->Fields.Reserve(RCGroup.GetFields().Num());

		for (FGuid FieldId : RCGroup.GetFields())
		{
			if (TSharedPtr<SRCPanelExposedField>* Widget = FieldWidgetMap.Find(FieldId))
			{
				NewGroup->Fields.Add(*Widget);
			}
		}
	}

	TreeView->RequestListRefresh();
}

void SRemoteControlPanel::OnEditModeCheckboxToggle(ECheckBoxState State)
{
	bIsInEditMode = State == ECheckBoxState::Checked ? true : false;
	if (!bIsInEditMode)
	{
		TreeView->ClearSelection();
	}
	OnEditModeChange.ExecuteIfBound(SharedThis(this), bIsInEditMode);
}

void SRemoteControlPanel::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	for (TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
	{
		Target->OnObjectsReplaced(ReplacementObjectMap);
	}
}

void SRemoteControlPanel::OnObjectPropertyChange(UObject* InObject, FPropertyChangedEvent& InChangeEvent)
{
	EPropertyChangeType::Type TypesNeedingRefresh = EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayClear | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ValueSet;
	auto IsRelevantProperty = [] (FFieldClass* PropertyClass) 
		{
			return PropertyClass && (PropertyClass == FArrayProperty::StaticClass() || PropertyClass == FSetProperty::StaticClass() || PropertyClass == FMapProperty::StaticClass());
		};

	if ((InChangeEvent.ChangeType & TypesNeedingRefresh) != 0 && InChangeEvent.MemberProperty && IsRelevantProperty(InChangeEvent.MemberProperty->GetClass()))
	{
		for (const TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
		{
			if (Target->GetBoundObjects().Contains(InObject))
			{
				Target->RefreshTargetWidgets();
			}
		}

		TreeView->RequestTreeRefresh();
	}
}

FGuid SRemoteControlPanel::GetGroupId(const TSharedPtr<SRCPanelExposedField>& Field)
{
	FGuid GroupId;
	if (Field)
	{
		if (FRemoteControlPresetGroup* Group = GetLayout().FindGroupFromField(Field->GetFieldId()))
		{
			GroupId = Group->Id;
		}
	}

	return GroupId;
}

FReply SRemoteControlPanel::OnCreateGroup()
{
	FScopedTransaction Transaction(LOCTEXT("CreateGroup", "Create Group"));
	Preset->Modify();
	GetLayout().CreateGroup();
	return FReply::Handled();
}

void SRemoteControlPanel::OnDeleteGroup(const TSharedPtr<FRCPanelGroup>& FieldGroup)
{
	FScopedTransaction Transaction(LOCTEXT("DeleteGroup", "Delete Group"));
	Preset->Modify();
	GetLayout().DeleteGroup(FieldGroup->Id);
}

void SRemoteControlPanel::ExposeFunction(UObject* Object, UFunction* Function)
{
	bool bFoundTarget = false;

	FGuid GroupId;
	TArray<TSharedPtr<FRCPanelTreeNode>> SelectedGroups = TreeView->GetSelectedItems();
	if (SelectedGroups.Num() && SelectedGroups[0])
	{
		if (SelectedGroups[0]->GetType() == FRCPanelTreeNode::Group)
		{
			GroupId = SelectedGroups[0]->GetId();
		}
	}

	auto ExposeFunctionLambda = 
		[this, GroupId](FRemoteControlTarget& Target, UFunction* Function)
	{
		 Target.ExposeFunction(Function->GetName(), Function->GetDisplayNameText().ToString(), GroupId);
	};

	FScopedTransaction Transaction(LOCTEXT("ExposeFunction", "ExposeFunction"));
	Preset->Modify();

	for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
	{
		FRemoteControlTarget& Target = Tuple.Value;
		if (Target.HasBoundObjects({ Object }))
		{
			bFoundTarget = true;
			if (Target.FindFieldLabel(Function->GetFName()) == NAME_None)
			{
				ExposeFunctionLambda(Target, Function);
			}
		}
	}

	if (!bFoundTarget)
	{
		FName Alias = Preset->CreateTarget({Object});
		if (FRemoteControlTarget* Target = Preset->GetRemoteControlTargets().Find(Alias))
		{
			ExposeFunctionLambda(*Target, Function);
		}
	}
}

void SRemoteControlPanel::OnActorDeleted(AActor* Actor)
{
	UWorld* World = Actor->GetWorld();
	if (!World->IsPreviewWorld())
	{
		TSet<FName> TargetsToRemove;

		for (TTuple<FName, FRemoteControlTarget>& Tuple : Preset->GetRemoteControlTargets())
		{
			if (Tuple.Value.HasBoundObjects({ Actor }))
			{
				TargetsToRemove.Add(Tuple.Key);
			}
		}

		Preset->Modify();
		for (FName TargetName : TargetsToRemove)
		{
			Preset->DeleteTarget(TargetName);
		}

		if (TargetsToRemove.Num())
		{
			Refresh();
		}
	}
}

FReply SRemoteControlPanel::OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelExposedField>& TargetField, const TSharedPtr<FRCPanelGroup>& DragTargetGroup)
{
	checkSlow(DragTargetGroup);
	
	if (DragDropOperation->IsOfType<FExposedFieldDragDropOp>())
	{
		if (TSharedPtr<FExposedFieldDragDropOp> DragDropOp = StaticCastSharedPtr<FExposedFieldDragDropOp>(DragDropOperation))
		{
			FGuid DragOriginGroupId = GetGroupId(DragDropOp->GetFieldWidget());

			FRemoteControlPresetLayout::FFieldSwapArgs Args;
			Args.OriginGroupId = DragOriginGroupId;
			Args.TargetGroupId = DragTargetGroup->Id;
			Args.DraggedFieldId = DragDropOp->GetFieldWidget()->GetFieldId();

			if (TargetField)
			{
				Args.TargetFieldId = TargetField->GetFieldId();
			}
			else
			{
				if (Args.OriginGroupId == Args.TargetGroupId)
				{
					// No-op if dragged from the same group.
					return FReply::Unhandled();
				}
			}

			FScopedTransaction Transaction(LOCTEXT("MoveField", "Move exposed field"));
			Preset->Modify();
			GetLayout().SwapFields(Args);
			return FReply::Handled();
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			if (TSharedPtr<FRCPanelGroup> DragOriginUIGroup = DragDropOp->GetDragOriginGroup())
			{
				FGroupDragEvent Event(*DragOriginUIGroup, *DragTargetGroup);
				FGuid DragOriginGroupId = DragOriginUIGroup->Id;
				FGuid DragTargetGroupId = DragTargetGroup->Id;

				if (DragOriginGroupId == DragTargetGroupId)
				{
					// No-op if dragged from the same group.
					return FReply::Unhandled();
				}

				FScopedTransaction Transaction(LOCTEXT("MoveGroup", "Move Group"));
				Preset->Modify();
				GetLayout().SwapGroups(DragOriginGroupId, DragTargetGroupId);
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
}

void SRemoteControlPanel::RegisterPresetDelegates()
{
	FRemoteControlPresetLayout& Layout = GetLayout();
	Layout.OnGroupAdded().AddRaw(this, &SRemoteControlPanel::OnGroupAdded);
	Layout.OnGroupDeleted().AddRaw(this, &SRemoteControlPanel::OnGroupDeleted);
	Layout.OnGroupOrderChanged().AddRaw(this, &SRemoteControlPanel::OnGroupOrderChanged);
	Layout.OnGroupRenamed().AddRaw(this, &SRemoteControlPanel::OnGroupRenamed);
	Layout.OnFieldAdded().AddRaw(this, &SRemoteControlPanel::OnFieldAdded);
	Layout.OnFieldDeleted().AddRaw(this, &SRemoteControlPanel::OnFieldDeleted);
	Layout.OnFieldOrderChanged().AddRaw(this, &SRemoteControlPanel::OnFieldOrderChanged);
}

void SRemoteControlPanel::UnregisterPresetDelegates()
{
	if (Preset)
	{
		FRemoteControlPresetLayout& Layout = GetLayout();
		Layout.OnGroupAdded().RemoveAll(this);
		Layout.OnGroupDeleted().RemoveAll(this);
		Layout.OnGroupOrderChanged().RemoveAll(this);
		Layout.OnGroupRenamed().RemoveAll(this);
		Layout.OnFieldAdded().RemoveAll(this);
		Layout.OnFieldDeleted().RemoveAll(this);
		Layout.OnFieldOrderChanged().RemoveAll(this);
	}
}

void SRemoteControlPanel::OnPieEvent(bool)
{
	// Trigger the refresh on the next tick to make sure the PIE world exists when starting and doesn't exist when stopping.
	bTriggerRefreshForPIE = true;
}

void SRemoteControlPanel::OnGroupAdded(const FRemoteControlPresetGroup& Group)
{
	TSharedPtr<FRCPanelGroup> NewGroup = MakeShared<FRCPanelGroup>(Group.Name, Group.Id, SharedThis(this));
	FieldGroups.Add(NewGroup);
	NewGroup->Fields.Reserve(Group.GetFields().Num());

	for (FGuid FieldId : Group.GetFields())
	{
		if (TSharedPtr<SRCPanelExposedField>* Widget = FieldWidgetMap.Find(FieldId))
		{
			NewGroup->Fields.Add(*Widget);
		}
	}
	TreeView->SetSelection(NewGroup);
	TreeView->ScrollToBottom();
	TreeView->RequestListRefresh();
}

void SRemoteControlPanel::OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup)
{
	int32 Index = FieldGroups.IndexOfByPredicate([&DeletedGroup](const TSharedPtr<FRCPanelGroup>& Group){ return Group->Id == DeletedGroup.Id; });
	if (Index != INDEX_NONE)
	{
		FieldGroups.RemoveAt(Index);
		TreeView->RequestListRefresh();
	}
}

void SRemoteControlPanel::OnGroupOrderChanged(const TArray<FGuid>& GroupIds)
{
	TMap<FGuid, int32> IndicesMap;
	IndicesMap.Reserve(GroupIds.Num());
	for (auto It = GroupIds.CreateConstIterator(); It; ++It)
	{
		IndicesMap.Add(*It, It.GetIndex());
	}

	auto SortFunc = [&IndicesMap]
		(const TSharedPtr<FRCPanelGroup>& A, const TSharedPtr<FRCPanelGroup>& B)
		{
			return IndicesMap.FindChecked(A->Id) < IndicesMap.FindChecked(B->Id);
		};

	FieldGroups.Sort(SortFunc);
	TreeView->RequestListRefresh();
}

void SRemoteControlPanel::OnGroupRenamed(const FGuid& GroupId, FName NewName)
{
	if (TSharedPtr<FRCPanelGroup>* TargetGroup = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& Group) {return Group->Id == GroupId; }))
	{
		if (*TargetGroup)
		{
			(*TargetGroup)->Name = NewName;
			if (TSharedPtr<SFieldGroup> GroupWidget = StaticCastSharedPtr<SFieldGroup>(TreeView->WidgetFromItem(*TargetGroup)))
			{
				GroupWidget->SetName(NewName);
			}
		}
	}
}

void SRemoteControlPanel::OnFieldAdded(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	FName TargetName = Preset->GetOwnerAlias(FieldId);
	if (TargetName == NAME_None)
	{
		return;
	}

	auto GetFieldWidget = [this, &FieldId] (const TSharedRef<SRemoteControlTarget>& Target)
	{
		TSharedPtr<SRCPanelExposedField> FieldWidget;
		if (TOptional<FRemoteControlProperty> Property = Preset->GetProperty(FieldId))
		{
			FieldWidget = Target->AddExposedProperty(*Property);
		}
		else if (TOptional<FRemoteControlFunction> Function = Preset->GetFunction(FieldId))
		{
			FieldWidget = Target->AddExposedFunction(*Function);
		}

		return FieldWidget;
	};
	
	if (TOptional<FRemoteControlField> Field = Preset->GetField(FieldId))
	{
		TSharedPtr<SRCPanelExposedField> FieldWidget;

		// If target already exists in the panel.
		if (TSharedRef<SRemoteControlTarget>* Target = RemoteControlTargets.FindByPredicate([TargetName](const TSharedRef<SRemoteControlTarget>& InTarget) { return InTarget->GetTargetAlias() == TargetName; }))
		{
			FieldWidget = GetFieldWidget(*Target);
		}
		else
		{
			const FRemoteControlTarget& FieldOwnerTarget = Preset->GetRemoteControlTargets().FindChecked(TargetName);

			TSharedRef<SRemoteControlTarget> PanelTarget = MakeShared<SRemoteControlTarget>(TargetName, SharedThis<SRemoteControlPanel>(this));
			RemoteControlTargets.Add(PanelTarget);
			FieldWidget = GetFieldWidget(PanelTarget);
		}
 
		FieldWidgetMap.Add(FieldId, FieldWidget);
		if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
		{
			if (*Group)
			{
				(*Group)->Fields.Insert(FieldWidget, FieldPosition);
				TreeView->SetItemExpansion(*Group, true);
			}
		}
	}

	TreeView->RequestListRefresh();
}

void SRemoteControlPanel::OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition)
{
	FName TargetName = Preset->GetOwnerAlias(FieldId);
	if (TargetName == NAME_None)
	{
		return;
	}

	if (TSharedRef<SRemoteControlTarget>* Target = RemoteControlTargets.FindByPredicate([TargetName](const TSharedRef<SRemoteControlTarget>& InTarget) { return InTarget->GetTargetAlias() == TargetName; }))
	{
		if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
		{
			if (*Group)
			{
				(*Group)->Fields.RemoveAt(FieldPosition);
			}
		}

		TArray<TSharedRef<SRCPanelExposedField>>& FieldWdigets = (*Target)->GetFieldWidgets();
		int32 Index = FieldWdigets.IndexOfByPredicate([&FieldId](const TSharedRef<SRCPanelExposedField>& Widget) {return Widget->GetFieldId() == FieldId;});
		if (Index != INDEX_NONE)
		{
			FieldWdigets.RemoveAt(Index);
			FieldWidgetMap.Remove(FieldId);
		}
	}

	TreeView->RequestListRefresh();
}

void SRemoteControlPanel::OnFieldOrderChanged(const FGuid& GroupId, const TArray<FGuid>& Fields)
{
	if (TSharedPtr<FRCPanelGroup>* Group = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FRCPanelGroup>& InGroup) {return InGroup->Id == GroupId; }))
	{
		// Sort the group's fields according to the fields array.
		TMap<FGuid, int32> OrderMap;
		OrderMap.Reserve(Fields.Num());
		for (auto It = Fields.CreateConstIterator(); It; ++It)
		{
			OrderMap.Add(*It, It.GetIndex());
		}

		if (*Group)
		{
			(*Group)->Fields.Sort(
				[&OrderMap]
				(const TSharedPtr<SRCPanelExposedField>& A, const TSharedPtr<SRCPanelExposedField>& B)
				{
					return OrderMap.FindChecked(A->GetFieldId()) < OrderMap.FindChecked(B->GetFieldId());
				});
		}
	}

	TreeView->RequestListRefresh();
}

void SFieldGroup::Tick(const FGeometry&, const double, const float)
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

void SFieldGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FRCPanelGroup>& InFieldGroup, const TSharedPtr<SRemoteControlPanel>& OwnerPanel)
{
	checkSlow(InFieldGroup);

	FieldGroup = InFieldGroup;
	OnFieldDropEvent = InArgs._OnFieldDropEvent;
	OnGetGroupId = InArgs._OnGetGroupId;
	OnDeleteGroup = InArgs._OnDeleteGroup;
	WeakPanel = OwnerPanel;
	bEditMode = InArgs._EditMode;

	this->ChildSlot
		[
			SNew(SBorder)
			.Padding(0.f)
			.BorderImage(this, &SFieldGroup::GetBorderImage)
			.VAlign(VAlign_Fill)
			[
				SNew(SDropTarget)
				.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
				.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
				.OnDrop_Lambda([this] (TSharedPtr<FDragDropOperation> DragDropOperation){ return OnFieldDropGroup(DragDropOperation, nullptr);} )
				.OnAllowDrop(this, &SFieldGroup::OnAllowDropFromOtherGroup)
				.OnIsRecognized(this, &SFieldGroup::OnAllowDropFromOtherGroup)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SExpanderArrow, SharedThis(this))
					]
					// Drag and drop handle
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SBox)
						.Padding(FMargin(0.0f, 2.0f) )
						.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
						[
							SNew(SDragHandle)
							.GroupWidget(SharedThis(this))
						]
					]
					+ SHorizontalBox::Slot()
					// Group name
					.FillWidth(1.0f)
					.VAlign(VAlign_Fill)
					.Padding(FMargin(0.f, 0.f, 0.f, 2.f))
					.AutoWidth()
					[
						SAssignNew(NameTextBox, SInlineEditableTextBlock)
						.ColorAndOpacity(this, &SFieldGroup::GetGroupNameTextColor)
						.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
						.Text(FText::FromName(FieldGroup->Name))
						.OnTextCommitted(this, &SFieldGroup::OnLabelCommitted)
						.OnVerifyTextChanged(this, &SFieldGroup::OnVerifyItemLabelChanged)
						.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); })
					]
					+ SHorizontalBox::Slot()
					// Rename button
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(SButton)
						.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton")
						.OnClicked_Lambda([this] () 
							{
								bNeedsRename = true;
								return FReply::Handled();	
							})
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf044"))) /*fa-edit*/)
						]
					]
					// Spacer
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					// Remove group button
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Right)
					.Padding(0, 2.0f)
					.AutoWidth()
					[
						SNew(SButton)
						.Visibility_Raw(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
						.OnClicked(this, &SFieldGroup::HandleDeleteGroup)
						.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
						[
							SNew(STextBlock)
							.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
							.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
						]
					]
				]
			]

		];

	STableRow<TSharedPtr<FRCPanelGroup>>::ConstructInternal(
		STableRow::FArguments()
		.ShowSelection(false),
		InOwnerTableView
	);
}

void SFieldGroup::Refresh()
{
	if (FieldsListView)
	{
		FieldsListView->RequestListRefresh();
	}
}

FName SFieldGroup::GetGroupName() const
{
	FName Name;
	if (FieldGroup)
	{
		Name = FieldGroup->Name;
	}
	return Name;
}

TSharedPtr<FRCPanelGroup> SFieldGroup::GetGroup() const
{
	return FieldGroup;
}

void SFieldGroup::SetName(FName Name)
{
	NameTextBox->SetText(FText::FromName(Name));
}

FReply SFieldGroup::OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SRCPanelExposedField> TargetField)
{
	if (TSharedPtr<FExposedFieldDragDropOp> DragDropOp = Event.GetOperationAs<FExposedFieldDragDropOp>())
	{
		return OnFieldDropGroup(DragDropOp, TargetField);
	}
	return FReply::Unhandled();
}

FReply SFieldGroup::OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SRCPanelExposedField> TargetField)
{
	if (DragDropOperation)
	{
		if (DragDropOperation->IsOfType<FExposedFieldDragDropOp>() && OnFieldDropEvent.IsBound())
		{
			return OnFieldDropEvent.Execute(DragDropOperation, TargetField, FieldGroup);
		}
		else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>() && OnFieldDropEvent.IsBound())
		{
			return OnFieldDropEvent.Execute(DragDropOperation, nullptr, FieldGroup);
		}
	}

	return FReply::Unhandled();
}

bool SFieldGroup::OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FExposedFieldDragDropOp>())
	{
		if (TSharedPtr<FExposedFieldDragDropOp> DragDropOp = StaticCastSharedPtr<FExposedFieldDragDropOp>(DragDropOperation))
		{
			if (TSharedPtr<SRCPanelExposedField> FieldWidget = DragDropOp->GetFieldWidget())
			{
				if (OnGetGroupId.IsBound())
				{
					FGuid OriginGroupId = OnGetGroupId.Execute(FieldWidget);
					if (FieldGroup && OriginGroupId != FieldGroup->Id)
					{
						return true;
					}
				}
			}
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			if (TSharedPtr<FRCPanelGroup> DragOriginGroup = DragDropOp->GetDragOriginGroup())
			{
				if (DragOriginGroup && FieldGroup && DragOriginGroup->Id != FieldGroup->Id)
				{
					return true;
				}
			}
		}
	}

	return false;
}

FReply SFieldGroup::HandleDeleteGroup()
{
	OnDeleteGroup.ExecuteIfBound(FieldGroup);
	return FReply::Handled();
}

FSlateColor SFieldGroup::GetGroupNameTextColor() const
{
	checkSlow(FieldGroup);
	return FLinearColor(1, 1, 1, 0.7);
}

const FSlateBrush* SFieldGroup::GetBorderImage() const
{
	if (IsSelected())
	{
		return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupRowSelected");
	}
	else
	{
		return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder");
	}
}

EVisibility SFieldGroup::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

bool SFieldGroup::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		FName TentativeName = FName(*InLabel.ToString());
		if (TentativeName != FieldGroup->Name && !!Panel->GetLayout().GetGroupByName(TentativeName))
		{
			OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
			return false;
		}
	}

	return true;
}

void SFieldGroup::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		FScopedTransaction Transaction(LOCTEXT("RenameGroup", "Rename Group"));
		Panel->GetPreset()->Modify();
		Panel->GetLayout().RenameGroup(FieldGroup->Id, FName(*InLabel.ToString()));
	}
}

SRemoteControlTarget::SRemoteControlTarget(FName Alias, TSharedRef<SRemoteControlPanel> InOwnerPanel)
	: TargetAlias(Alias)
	, WeakPanel(MoveTemp(InOwnerPanel))
{
	Algo::ForEach(GetUnderlyingTarget().ExposedProperties, [this] (const FRemoteControlProperty& RCProperty) {AddExposedProperty(RCProperty); });
	Algo::ForEach(GetUnderlyingTarget().ExposedFunctions, [this] (const FRemoteControlFunction& RCFunction) {AddExposedFunction(RCFunction); });
}

void SRemoteControlTarget::RefreshTargetWidgets()
{
	for (TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->Refresh();
	}
}

TSharedPtr<SRCPanelExposedField> SRemoteControlTarget::AddExposedProperty(const FRemoteControlProperty& RCProperty)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FPropertyRowGeneratorArgs GeneratorArgs;
	TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);

	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		if (TOptional<FExposedProperty> Property = Panel->Preset->ResolveExposedProperty(RCProperty.Label))
		{
			RowGenerator->SetObjects(Property->OwnerObjects);
			if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), RCProperty.FieldPathInfo.ToPathPropertyString(), true))
			{
				return ExposedFieldWidgets.Add_GetRef(SNew(SRCPanelExposedField, RCProperty, RowGenerator, WeakPanel)
					.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode));
			}
		}
	}

	return ExposedFieldWidgets.Add_GetRef(SNew(SRCPanelExposedField, RCProperty, RowGenerator, WeakPanel)
		.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
		[
			RemoteControlPanelUtil::CreateInvalidWidget()
		]);
}

TSharedPtr<SRCPanelExposedField> SRemoteControlTarget::AddExposedFunction(const FRemoteControlFunction& RCFunction)
{
	TSharedPtr<SRCPanelExposedField> ExposedFieldWidget;
	TSharedRef<SVerticalBox> ArgsTarget = SNew(SVerticalBox);

	FPropertyRowGeneratorArgs GeneratorArgs;
	GeneratorArgs.bShouldShowHiddenProperties = true;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
	if (!RCFunction.Function)
	{
		ExposedFieldWidgets.Add(
			SAssignNew(ExposedFieldWidget, SRCPanelExposedField, RCFunction, RowGenerator, WeakPanel)
			.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
			[
				RemoteControlPanelUtil::CreateInvalidWidget()
			]
		);
		return ExposedFieldWidget;
	}

	RowGenerator->SetStructure(RCFunction.FunctionArguments);
	TArray<TSharedPtr<FRCPanelFieldChildNode>> ChildNodes;
	for (TFieldIterator<FProperty> It(RCFunction.Function); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Parm) || It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
		{
			continue;
		}

		if (TSharedPtr<IDetailTreeNode> PropertyNode = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), It->GetFName().ToString(), false))
		{
			ChildNodes.Add(SNew(FRCPanelFieldChildNode, PropertyNode.ToSharedRef()));
		}
	}

	TSharedRef<SWidget> ButtonWidget = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Left)
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.OnClicked_Raw(this, &SRemoteControlTarget::OnClickFunctionButton, RCFunction)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CallFunctionLabel", "Call Function"))
			]
		];

	RemoteControlPanelUtil::FMakeFieldWidgetArgs Args;

	ExposedFieldWidgets.Add(
		SAssignNew(ExposedFieldWidget, SRCPanelExposedField, RCFunction, RowGenerator, WeakPanel)
		.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
		.ChildWidgets(&ChildNodes)
		.Content()
		[
			MoveTemp(ButtonWidget)
		]
	);

	return ExposedFieldWidget;
}

TSet<UObject*> SRemoteControlTarget::GetBoundObjects() const
{
	TSet<UObject*> Objects;
	for (const TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->GetBoundObjects(Objects);
	}
	return Objects;
}

void SRemoteControlTarget::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	for (TSharedRef<SRCPanelExposedField>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->OnObjectsReplaced(ReplacementObjectMap);
	}
}

FRemoteControlTarget& SRemoteControlTarget::GetUnderlyingTarget()
{
	TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin();
	check(Panel);
	return Panel->GetPreset()->GetRemoteControlTargets().FindChecked(TargetAlias);
}

FReply SRemoteControlTarget::OnClickFunctionButton(FRemoteControlFunction FunctionField)
{
	FScopedTransaction FunctionTransaction(LOCTEXT("CallExposedFunction", "Called a function through preset."));
	FEditorScriptExecutionGuard ScriptGuard;

	if (URemoteControlPreset* Preset = GetPreset())
	{
		if (TOptional<FExposedFunction> Function = Preset->ResolveExposedFunction(FunctionField.Label))
		{
			for (UObject* Object : Function->OwnerObjects)
			{
				if (FunctionField.FunctionArguments)
				{
					Object->ProcessEvent(FunctionField.Function, FunctionField.FunctionArguments->GetStructMemory());
				}
				else
				{
					ensureAlwaysMsgf(false, TEXT("Verify what causes this"));
				}
			}
		}
	}

	return FReply::Handled();
}

EVisibility SRemoteControlTarget::GetVisibilityAccordingToEditMode() const
{
	if (const TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		return Panel->IsInEditMode() ? EVisibility::Visible : EVisibility::Collapsed;
	}
	return EVisibility::All;
}

bool SRemoteControlTarget::GetPanelEditMode() const
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		return Panel->IsInEditMode();
	}
	return false;
}

URemoteControlPreset* SRemoteControlTarget::GetPreset()
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		return Panel->GetPreset();
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE /* RemoteControlPanel */

