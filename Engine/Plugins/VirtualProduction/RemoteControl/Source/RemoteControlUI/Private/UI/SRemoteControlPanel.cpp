// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRemoteControlPanel.h"

#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "Application/SlateApplicationBase.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IHotReload.h"
#include "IPropertyRowGenerator.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RemoteControlPanelStyle.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "SDropTarget.h"
#include "SSearchableItemList.h"
#include "SSearchableTreeView.h"
#include "UObject/StructOnScope.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

namespace RemoteControlPanelUtil
{
	bool FindPropertyHandleRecursive(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& QualifiedPropertyName)
	{
		if (PropertyHandle && PropertyHandle->IsValidHandle())
		{
			uint32 ChildrenCount = 0;
			PropertyHandle->GetNumChildren(ChildrenCount);
			for (uint32 Index = 0; Index < ChildrenCount; ++Index)
			{
				TSharedPtr<IPropertyHandle> ChildHandle = PropertyHandle->GetChildHandle(Index);
				if (FindPropertyHandleRecursive(ChildHandle, QualifiedPropertyName))
				{
					return true;
				}
			}

			if (PropertyHandle->GetProperty())
			{
				if (PropertyHandle->GeneratePathToProperty() == QualifiedPropertyName)
				{
					return true;
				}
			}
		}

		return false;
	}

	TSharedPtr<IDetailTreeNode> FindTreeNodeRecursive(const TSharedRef<IDetailTreeNode>& RootNode, const FString& QualifiedPropertyName)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		RootNode->GetChildren(Children);
		for (TSharedRef<IDetailTreeNode>& Child : Children)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(Child, QualifiedPropertyName);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		TSharedPtr<IPropertyHandle> Handle = RootNode->CreatePropertyHandle();
		if (FindPropertyHandleRecursive(Handle, QualifiedPropertyName))
		{
			return RootNode;
		}

		return nullptr;
	}

	/** Find a node by its name in a detail tree node hierarchy. */
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& RootNodes, const FString& QualifiedPropertyName)
	{
		for (const TSharedRef<IDetailTreeNode>& CategoryNode : RootNodes)
		{
			TSharedPtr<IDetailTreeNode> FoundNode = FindTreeNodeRecursive(CategoryNode, QualifiedPropertyName);
			if (FoundNode.IsValid())
			{
				return FoundNode;
			}
		}

		return nullptr;
	}
	
	/** Recursively create a property widget. */
	TSharedRef<SWidget> CreatePropertyWidget(const TSharedPtr<IDetailTreeNode>& Node, FName FieldLabel)
	{
		FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();

		TSharedRef<SVerticalBox> VerticalWrapper = SNew(SVerticalBox);
		TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

		if (NodeWidgets.NameWidget && NodeWidgets.ValueWidget)
		{
			TSharedPtr<SWidget> NameWidget;
			if (FieldLabel == NAME_None)
			{
				NameWidget = NodeWidgets.NameWidget;
			}
			else
			{
				NameWidget = SNullWidget::NullWidget;
			}

			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NameWidget.ToSharedRef()
				];

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

		VerticalWrapper->AddSlot()
			.AutoHeight()
			[
				FieldWidget
			];

		TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
		Node->GetChildren(ChildNodes);
		
		for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
		{
			VerticalWrapper->AddSlot()
				.AutoHeight()
				.Padding(5.0f, 0.0f)
				[
					CreatePropertyWidget(ChildNode, TEXT(""))
				];
		}

		return VerticalWrapper;
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


	struct FTreeNode
	{
		virtual ~FTreeNode() = default;
		virtual FString GetName() = 0;
		virtual bool IsFunctionNode() const { return false; };
		virtual UBlueprintFunctionLibrary* GetLibrary() = 0;
		virtual UFunction* GetFunction() { return nullptr; };
	};


	struct FRCLibraryNode : public FTreeNode
	{
		virtual FString GetName() override
		{
			return Library ? Library->GetName().RightChop(DefaultPrefix.Len()) : FString();
		}

		virtual UBlueprintFunctionLibrary* GetLibrary() override
		{
			return Library;
		}

		UBlueprintFunctionLibrary* Library = nullptr;

	private:
		const FString DefaultPrefix = TEXT("Default__");
	};

	struct FRCFunctionNode : public FTreeNode
	{
		FRCFunctionNode(UBlueprintFunctionLibrary* InOwner, UFunction* InFunction)
			: Owner(InOwner)
			, Function(InFunction)
		{}

		virtual FString GetName() override
		{
			return Function ? Function->GetName() : FString();
		}

		virtual bool IsFunctionNode() const { return true; };

		virtual UFunction* GetFunction() { return Function; }

		virtual UBlueprintFunctionLibrary* GetLibrary() override { return Owner; }

		UBlueprintFunctionLibrary* Owner = nullptr;
		UFunction* Function = nullptr;
	};

	auto OnGetChildren = []
	(TSharedPtr<FTreeNode> Node, TArray<TSharedPtr<FTreeNode>>& OutChildren)
	{
		if (Node->IsFunctionNode())
		{
			return;
		}

		if (TSharedPtr<FRCLibraryNode> LibraryNode = StaticCastSharedPtr<FRCLibraryNode>(Node))
		{
			for (UFunction* Function : GetExposableFunctions(LibraryNode->Library->GetClass()))
			{
				if (Function)
				{
					TSharedPtr<FRCFunctionNode> FunctionNode = MakeShared<FRCFunctionNode>(LibraryNode->Library, Function);
					OutChildren.Add(FunctionNode);
				}
			}
		}
	};
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

	TSharedPtr<SExposedFieldWidget> GetFieldWidget() const
	{
		return StaticCastSharedPtr<SExposedFieldWidget>(FieldWidget);
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

	TSharedPtr<FFieldGroup> GetDragOriginGroup() const
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
	SLATE_DEFAULT_SLOT(FArguments, Content)
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
				.VAlign(VAlign_Top)
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
		//Get the nested path
		FieldPathInfo = FFieldPathInfo(PropertyHandle->GeneratePathToProperty());
		
		//Build the component hierarchy
		if (InOwnerObjects[0]->IsA<UActorComponent>())
		{
			UObject* CurrentOuter = InOwnerObjects[0];
			for (;;)
			{
				if (!CurrentOuter || CurrentOuter->IsA<AActor>())
				{
					break;
				}
				FieldPathInfo.ComponentChain.Insert(CurrentOuter->GetName(), 0);
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

/**
 * Widget that displays an exposed field.
 */
struct SExposedFieldWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SExposedFieldWidget)
		: _Content()
		, _EditMode(true)
		{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_NAMED_SLOT(FArguments, OptionsContent)
	SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const FRemoteControlField& Field, TSharedRef<IPropertyRowGenerator> InRowGenerator, TWeakPtr<SRemoteControlPanel> InPanel)
	{
		FieldType = Field.FieldType;
		FieldName = Field.FieldName;
		FieldLabel = Field.Label;
		QualifiedFieldName = Field.GetQualifiedFieldName();
		FieldId = Field.Id;
		RowGenerator = MoveTemp(InRowGenerator);
		OptionsWidget = InArgs._OptionsContent.Widget;
		bEditMode = InArgs._EditMode;
		WeakPanel = MoveTemp(InPanel);

		ChildSlot
		[
			MakeFieldWidget(InArgs._Content.Widget)
		];
	}

	void Tick(const FGeometry&, const double, const float)
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

	FName GetFieldLabel() const
	{
		return FieldLabel;
	}

	FGuid GetFieldId() const
	{
		return FieldId;
	}

	EExposedFieldType GetFieldType() const
	{
		return FieldType;
	}

	void SetIsHovered(bool bInIsHovered)
	{
		bIsHovered = bInIsHovered;
	}

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
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
			BindObjects(NewObjectList);
		}	
	}

	void Refresh()
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

		CreateWidget(Objects);
	}

	TSharedRef<SWidget> MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
	{
		const FMargin TopButtonPadding(0.f, 1.5f, 0.f, 0.f);
		return SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(this, &SExposedFieldWidget::GetBorderImage)
			[
				 SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(2.0f)
				.AutoWidth()
				[
					SNew(SBox)
					.Padding(TopButtonPadding)
					.Visibility(this, &SExposedFieldWidget::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
					[
						SNew(SDragHandle)
						.FieldWidget(AsShared())
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Left)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.VAlign(VAlign_Top)
							.FillHeight(1.0f)
							.Padding(0, 4.0f)
							[
								SAssignNew(NameTextBox, SInlineEditableTextBlock)
								.Text(FText::FromName(FieldLabel))
								.OnTextCommitted(this, &SExposedFieldWidget::OnLabelCommitted)
								.OnVerifyTextChanged(this, &SExposedFieldWidget::OnVerifyItemLabelChanged)
								.IsReadOnly_Lambda([this] () { return !bEditMode.Get(); })
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Left)
						.Padding(0, 1.0f)
						[
							SNew(SButton)
							.Visibility(this, &SExposedFieldWidget::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
							.ButtonStyle(FEditorStyle::Get(), "FlatButton")
							.OnClicked_Lambda([this] () {
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
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.HAlign(HAlign_Right)
						[
							InWidget
						]
				
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Right)
						.Padding(0, 1.0f)
						[
							SNew(SButton)
							.Visibility(this, &SExposedFieldWidget::GetOptionsButtonVisibility)
							.ButtonStyle(FEditorStyle::Get(), "FlatButton")
							.OnClicked(this, &SExposedFieldWidget::OnClickExpandButton)
							[
								SNew(STextBlock)
								.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
								.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
							]
						]
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Top)
						.Padding(TopButtonPadding)
						.AutoWidth()
						[
							SNew(SButton)
							.Visibility(this, &SExposedFieldWidget::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
							.OnClicked(this, &SExposedFieldWidget::HandleUnexposeField)
							.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
							[
								SNew(STextBlock)
								.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
								.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
								.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.Padding(FMargin(0.0f, 0.0f))
						.Visibility_Lambda([this]() {return bShowOptions ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							OptionsWidget.ToSharedRef()
						]
					]
				]
			];
	}

	void BindObjects(const TArray<UObject*>& InObjects)
	{
		CreateWidget(InObjects);
	}

	void GetBoundObjects(TSet<UObject*>& OutBoundObjects) const
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

	void CreateWidget(const TArray<UObject*>& InObjects)
	{
		RowGenerator->SetObjects(InObjects);	
		if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), QualifiedFieldName))
		{
			ChildSlot.AttachWidget(MakeFieldWidget(RemoteControlPanelUtil::CreatePropertyWidget(MoveTemp(Node), FieldLabel)));
		}

		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->RefreshLayout();
		}
	}

private:
	void OnPropertyChange(const FPropertyChangedEvent& Event)
	{
		Refresh();
	}

	EVisibility GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
	{
		return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
	}

	/** Handle displaying/hiding a field's options when prompted. */
	FReply OnClickExpandButton()
	{
		bShowOptions = !bShowOptions;
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->RefreshLayout();
		}
		return FReply::Handled();
	}

	const FSlateBrush* GetBorderImage() const
	{
		return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HeaderSectionBorder");
	}

	FReply HandleUnexposeField()
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			Panel->GetPreset()->Unexpose(FieldLabel);
			Panel->RefreshLayout();
		}
		return FReply::Handled();
	}

	EVisibility GetOptionsButtonVisibility() const
	{
		if (OptionsWidget == SNullWidget::NullWidget)
		{
			return EVisibility::Collapsed;
		}

		return GetVisibilityAccordingToEditMode(EVisibility::Collapsed);
	}

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
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

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
		{
			if (URemoteControlPreset* Preset = Panel->GetPreset())
			{
				Preset->RenameField(FieldLabel, FName(*InLabel.ToString()));
				FieldLabel = FName(*InLabel.ToString());
				NameTextBox->SetText(FText::FromName(FieldLabel));
			}
		}
	}

private:
	/** Type of the exposed field. */
	EExposedFieldType FieldType;
	/** Name of the field's underlying property. */
	FName FieldName;
	/** Display name of the field. */
	FName FieldLabel;
	/** Qualified field name, with its path to parent */
	FString QualifiedFieldName;
	/** Id of the field. */
	FGuid FieldId;
	/** Whether the row should display its options. */
	bool bShowOptions = false;
	/** Whether the widget is currently hovered by a drag and drop operation. */
	bool bIsHovered = false;
	/** Whether the editable text box for the label needs to enter edit mode. */
	bool bNeedsRename = false;
	/** The widget that displays the field's options ie. Function arguments or metadata. */
	TSharedPtr<SWidget> OptionsWidget;
	/** Holds the generator that creates the widgets. */
	TSharedPtr<IPropertyRowGenerator> RowGenerator;
	/** Whether the panel is in edit mode or not. */
	TAttribute<bool> bEditMode;
	/** Weak ptr to the panel */
	TWeakPtr<SRemoteControlPanel> WeakPanel;
	/** The textbox for the row's name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
};

void SRemoteControlPanel::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset)
{
	OnEditModeChange = InArgs._OnEditModeChange;
	Preset = TStrongObjectPtr<URemoteControlPreset>(InPreset);
	bIsInEditMode = true;

	OnPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SRemoteControlPanel::OnObjectPropertyChange);

	ReloadBlueprintLibraries();
	GEditor->OnObjectsReplaced().AddSP(this, &SRemoteControlPanel::OnObjectsReplaced);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
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
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0)
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
			SAssignNew(GroupsListView, SListView<TSharedPtr<FFieldGroup>>)
			.ListItemsSource(&FieldGroups)
			.ItemHeight(24.0f)
			.OnGenerateRow(this, &SRemoteControlPanel::OnGenerateGroupRow)
			.OnSelectionChanged(this, &SRemoteControlPanel::OnGroupSelectionChanged)
		]
	];

	Refresh();
}

SRemoteControlPanel::~SRemoteControlPanel()
{
	if (GEditor)
	{
		GEditor->OnObjectsReplaced().RemoveAll(this);
	}

	 FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedHandle);
}

void SRemoteControlPanel::Tick(const FGeometry&, const double, const float)
{
	if (LastSelectedGroupId.IsValid())
	{
		if (TSharedPtr<FFieldGroup>* Group = FieldGroups.FindByPredicate([this](const TSharedPtr<FFieldGroup>& InGroup) { return InGroup->Id == LastSelectedGroupId; }))
		{
			SelectGroup(*Group);
			LastSelectedGroupId.Invalidate();
		}
	}

	if (GroupsPendingRefresh.Num() > 0)
	{
		GroupsListView->RebuildList();
		for (FGuid GroupId : GroupsPendingRefresh)
		{
			if (TSharedPtr<FFieldGroup>* TargetGroup = FieldGroups.FindByPredicate([GroupId](const TSharedPtr<FFieldGroup>& Group) {return Group->Id == GroupId; }))
			{
				if (TSharedPtr<ITableRow> UIGroup = GroupsListView->WidgetFromItem(*TargetGroup))
				{
					StaticCastSharedPtr<SFieldGroup>(UIGroup)->Refresh();
				}
			}
		}

		GroupsListView->RequestListRefresh();
		GroupsPendingRefresh.Empty();
	}
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
					if (Target.FindFieldLabel(Property.PropertyName) == NAME_None)
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
		Unexpose(PropertyHandle);
		return;
	}

	for (UObject* Object : OuterObjects)
	{
		Expose({ PropertyHandle, { Object } });
	}

	Refresh();
}

FRemoteControlPresetLayout& SRemoteControlPanel::GetLayout()
{
	return Preset->Layout;
}

FReply SRemoteControlPanel::OnMouseButtonUp(const FGeometry&, const FPointerEvent&)
{
	return FReply::Handled();
}

void SRemoteControlPanel::RegisterEvents()
{
	if (GEditor)
	{
		FEditorDelegates::OnAssetsDeleted.AddLambda([this](const TArray<UClass*>&) { Refresh(); });
		GEditor->OnBlueprintReinstanced().AddLambda(
			[this]()
			{
				ReloadBlueprintLibraries();
				Refresh();
			});
		IHotReloadModule::Get().OnHotReload().AddLambda(
			[this](bool)
			{
				ReloadBlueprintLibraries();
				Refresh();
			});
	}
}

void SRemoteControlPanel::Refresh()
{
	if (Preset)
	{
		TSharedPtr<FFieldGroup> SelectedGroup;
		
		if (GroupsListView->GetNumItemsSelected() > 0)
		{
			SelectedGroup = GroupsListView->GetSelectedItems()[0];
		}

		GenerateFieldWidgets();
		RefreshGroups();

		GroupsListView->RequestListRefresh();

		for (const TSharedPtr<FFieldGroup>& FieldGroup : FieldGroups)
		{
			if (TSharedPtr<ITableRow> UIGroup = GroupsListView->WidgetFromItem(FieldGroup))
			{
				StaticCastSharedPtr<SFieldGroup>(UIGroup)->Refresh();
			}
		}

		if (SelectedGroup)
		{
			LastSelectedGroupId = SelectedGroup->Id;
		}
	}
}

void SRemoteControlPanel::RefreshLayout()
{
	RefreshGroups();

	for (const TSharedPtr<FFieldGroup>& FieldGroup : FieldGroups)
	{
		if (TSharedPtr<ITableRow> UIGroup = GroupsListView->WidgetFromItem(FieldGroup))
		{
			StaticCastSharedPtr<SFieldGroup>(UIGroup)->Refresh();
		}
	}

	GroupsListView->RequestListRefresh();
}

void SRemoteControlPanel::ClearSelection()
{
	GroupsListView->ClearSelection();
}

FRemoteControlTarget* SRemoteControlPanel::Expose(FExposableProperty&& Property)
{
	if (!Property.IsValid())
	{
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("OnRemoteControlPropertyExposed", "Remote Control Property Exposed"));
	Preset->Modify();

	FGuid GroupId;
	TArray<TSharedPtr<FFieldGroup>> SelectedGroups = GroupsListView->GetSelectedItems();
	if (SelectedGroups.Num() && SelectedGroups[0])
	{
		GroupId = SelectedGroups[0]->Id;
	}

	FRemoteControlTarget* LastModifiedTarget = nullptr;

	auto ExposePropertyLambda = 
		[this, &LastModifiedTarget, GroupId](FRemoteControlTarget& Target, const FExposableProperty& Property)
		{ 
			if (TOptional<FRemoteControlProperty> RCProperty = Target.ExposeProperty(Property.FieldPathInfo, Property.PropertyDisplayName, GroupId))
			{
				LastModifiedTarget = &Target;
				if (FRemoteControlPresetGroup* Group = Preset->Layout.FindGroupFromField(RCProperty->Id))
				{
					GroupsPendingRefresh.Add(Group->Id);
				}
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
				Preset->Unexpose(Tuple.Value.FindFieldLabel(Property.PropertyName));
			}
		}
	}

	Refresh();
}

TSharedRef<SWidget> SRemoteControlPanel::CreateBlueprintLibraryPicker()
{
	using namespace RemoteControlPanelUtil;

	TArray<TSharedPtr<FTreeNode>> Nodes;
	for (auto It = TObjectIterator<UBlueprintFunctionLibrary>(EObjectFlags::RF_NoFlags); It; ++It)
	{
		if (It->GetClass() != UBlueprintFunctionLibrary::StaticClass())
		{
			TSharedPtr<FRCLibraryNode> Node = MakeShared<FRCLibraryNode>();
			Node->Library = *It;
			Nodes.Add(Node);
		}
	}

	return SAssignNew(BlueprintLibraryPicker, SComboButton)
		.Visibility_Lambda([this]() { return bIsInEditMode ? EVisibility::Visible : EVisibility::Collapsed; })
		.ButtonStyle(FEditorStyle::Get(), "PropertyEditor.AssetComboStyle")
		.ForegroundColor(FSlateColor::UseForeground())
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
				SNew(SSearchableTreeView<TSharedPtr<FTreeNode>>)
				.Items(Nodes)
				.OnGetChildren_Lambda(OnGetChildren)
				.OnGetDisplayName_Lambda([](TSharedPtr<FTreeNode> InEntry) { return InEntry->GetName(); })
				.IsSelectable_Lambda([](TSharedPtr<FTreeNode> TreeNode) { return TreeNode->IsFunctionNode(); })
				.OnItemSelected_Lambda(
					[this] (TSharedPtr<FTreeNode> TreeNode) 
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

TSharedRef<ITableRow> SRemoteControlPanel::OnGenerateGroupRow(TSharedPtr<FFieldGroup> Group, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SFieldGroup, OwnerTable, MoveTemp(Group), SharedThis<SRemoteControlPanel>(this))
			.OnFieldDropEvent_Raw(this, &SRemoteControlPanel::OnDropOnGroup)
			.OnGetGroupId_Raw(this, &SRemoteControlPanel::GetGroupId)
			.OnDeleteGroup_Raw(this, &SRemoteControlPanel::OnDeleteGroup)
			.EditMode_Lambda([this] () { return bIsInEditMode; });
}

void SRemoteControlPanel::OnGroupSelectionChanged(TSharedPtr<FFieldGroup> InGroup, ESelectInfo::Type InSelectInfo)
{
	if (!bIsInEditMode)
	{
		return;
	}

	if (InGroup && InSelectInfo != ESelectInfo::Direct)
	{
		LastSelectedGroupId = InGroup->Id;
	}
	else
	{
		LastSelectedGroupId = FGuid();
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

	RemoteControlTargets.Reserve(TargetMap.Num());

	TSharedRef<SRemoteControlPanel> PanelPtr = SharedThis<SRemoteControlPanel>(this);
	for (TTuple<FName, FRemoteControlTarget>& MapEntry : TargetMap)
	{
		TSharedRef<SRemoteControlTarget> Target = MakeShared<SRemoteControlTarget>(MapEntry.Key, PanelPtr);
		RemoteControlTargets.Add(Target);
		for (const TSharedPtr<SExposedFieldWidget>& Widget : Target->GetFieldWidgets())
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
		TSharedPtr<FFieldGroup>& NewGroup = FieldGroups.Add_GetRef(MakeShared<FFieldGroup>(RCGroup.Name, RCGroup.Id, SharedThis(this)));
		NewGroup->Fields.Reserve(RCGroup.GetFields().Num());

		for (FGuid FieldId : RCGroup.GetFields())
		{
			if (TSharedPtr<SExposedFieldWidget>* Widget = FieldWidgetMap.Find(FieldId))
			{
				NewGroup->Fields.Add(*Widget);
			}
		}
	}
}

void SRemoteControlPanel::SelectGroup(const TSharedPtr<FFieldGroup>& FieldGroup)
{
	GroupsListView->SetSelection(FieldGroup);
	GroupsListView->RequestScrollIntoView(FieldGroup);
}

void SRemoteControlPanel::OnEditModeCheckboxToggle(ECheckBoxState State)
{
	bIsInEditMode = State == ECheckBoxState::Checked ? true : false;
	if (!bIsInEditMode)
	{
		GroupsListView->ClearSelection();
	}
	OnEditModeChange.ExecuteIfBound(SharedThis(this), bIsInEditMode);
}

void SRemoteControlPanel::ReloadBlueprintLibraries()
{
	BlueprintLibraries.Reset();
	for (auto It = TObjectIterator<UBlueprintFunctionLibrary>(EObjectFlags::RF_NoFlags); It; ++It)
	{
		if (It->GetClass() != UBlueprintFunctionLibrary::StaticClass())
		{
			FListEntry ListEntry{ It->GetClass()->GetName(), FSoftObjectPtr(*It) };
			BlueprintLibraries.Add(MakeShared<FListEntry>(MoveTemp(ListEntry)));
		}
	}
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
	if ((InChangeEvent.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayClear | EPropertyChangeType::ArrayRemove)) != 0)
	{
		for (const TSharedRef<SRemoteControlTarget>& Target : RemoteControlTargets)
		{
			if (Target->GetBoundObjects().Contains(InObject))
			{
				Target->RefreshTargetWidgets();
			}
		}
	}
}

FGuid SRemoteControlPanel::GetGroupId(const TSharedPtr<SExposedFieldWidget>& Field)
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
	if (FRemoteControlPresetGroup* NewGroup = GetLayout().CreateGroup())
	{
		TSharedPtr<FFieldGroup> FieldGroup = MakeShared<FFieldGroup>(NewGroup->Name, NewGroup->Id, SharedThis(this));
		FieldGroups.Add(FieldGroup);
		GroupsListView->SetSelection(FieldGroup);
		GroupsListView->RequestListRefresh();
		GroupsListView->ScrollToBottom();
	}
	
	return FReply::Handled();
}

void SRemoteControlPanel::OnDeleteGroup(const TSharedPtr<FFieldGroup>& FieldGroup)
{
	if (FieldGroup)
	{
		for (const TSharedPtr<SExposedFieldWidget>& FieldWidget: FieldGroup->Fields)
		{
			Preset->Unexpose(FieldWidget->GetFieldLabel());
		}
	}

	FieldGroups.Remove(FieldGroup);
	GetLayout().DeleteGroup(FieldGroup->Id);
	GroupsListView->RequestListRefresh();
}

void SRemoteControlPanel::ExposeFunction(UObject* Object, UFunction* Function)
{
	bool bFoundTarget = false;
	auto ExposeFunctionLambda = 
		[this](FRemoteControlTarget& Target, UFunction* Function) 
	{
		if (TOptional<FRemoteControlFunction> RCFunction = Target.ExposeFunction(Function->GetName(), Function->GetDisplayNameText().ToString()))
		{
			if (FRemoteControlPresetGroup* Group = Preset->Layout.FindGroupFromField(RCFunction->Id))
			{
				GroupsPendingRefresh.Add(Group->Id);
			}
			Refresh();
		}
	};

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

FReply SRemoteControlPanel::OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SExposedFieldWidget>& TargetField, const TSharedPtr<FFieldGroup>& DragTargetGroup)
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

			GetLayout().SwapFields(Args);
			RefreshLayout();
			return FReply::Handled();
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			if (TSharedPtr<FFieldGroup> DragOriginUIGroup = DragDropOp->GetDragOriginGroup())
			{
				FGroupDragEvent Event(*DragOriginUIGroup, *DragTargetGroup);
				FGuid DragOriginGroupId = DragOriginUIGroup->Id;
				FGuid DragTargetGroupId = DragTargetGroup->Id;

				if (DragOriginGroupId == DragTargetGroupId)
				{
					// No-op if dragged from the same group.
					return FReply::Unhandled();
				}

				GetLayout().SwapGroups(DragOriginGroupId, DragTargetGroupId);
				RefreshLayout();
				return FReply::Handled();
			}
		}
	}

	return FReply::Unhandled();
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

void SFieldGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FFieldGroup>& InFieldGroup, const TSharedPtr<SRemoteControlPanel>& OwnerPanel)
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
			SNew(SBox)
			.Padding(FMargin(2.f, 5.0f))
			[
				SNew(SBorder)
				.Padding(0.f)
				.BorderImage(this, &SFieldGroup::GetBorderImage)
				.VAlign(VAlign_Fill)
				[
					SNew(SDropTarget)
					.BackgroundImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder"))
					.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
					.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
					.OnDrop_Lambda([this] (TSharedPtr<FDragDropOperation> DragDropOperation){ return OnFieldDropGroup(DragDropOperation, nullptr);} )
					.OnAllowDrop(this, &SFieldGroup::OnAllowDropFromOtherGroup)
					.OnIsRecognized(this, &SFieldGroup::OnAllowDropFromOtherGroup)
					[
						SNew(SBorder)
						.Padding(0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.Padding(2.0f)
							.AutoWidth()
							[
								SNew(SBox)
								.Padding(FMargin(0.0f, 2.0f) )
								.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode)
								[
									SNew(SDragHandle)
									.GroupWidget(SharedThis(this))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Fill)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(5.0f, 2.0f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									[
										SAssignNew(NameTextBox, SInlineEditableTextBlock)
										.ColorAndOpacity(this, &SFieldGroup::GetGroupNameTextColor)
										.Text(FText::FromName(FieldGroup->Name))
										.OnTextCommitted(this, &SFieldGroup::OnLabelCommitted)
										.OnVerifyTextChanged(this, &SFieldGroup::OnVerifyItemLabelChanged)
										.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); })
									]
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Top)
									.HAlign(HAlign_Left)
									.Padding(0, 1.0f)
									[
										SNew(SButton)
										.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode)
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
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(5.0f, 0.0f)
								[
									SAssignNew(FieldsListView, SListView<TSharedPtr<SExposedFieldWidget>>)
									.ListItemsSource(&FieldGroup->Fields)
									.OnGenerateRow(this, &SFieldGroup::OnGenerateRow)
								]
							]
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Top)
							.Padding(0, 2.0f)
							.AutoWidth()
							[
								SNew(SButton)
								.Visibility_Raw(this, &SFieldGroup::GetVisibilityAccordingToEditMode)
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
				]

			]
		];

	STableRow<TSharedPtr<FFieldGroup>>::ConstructInternal(
		STableRow::FArguments()
		.ShowSelection(false),
		InOwnerTableView
	);
}

TSharedRef<ITableRow> SFieldGroup::OnGenerateRow(TSharedPtr<SExposedFieldWidget> Field, const TSharedRef<STableViewBase>& InnerOwnerTable)
{
	return SNew(STableRow<TSharedPtr<FName>>, InnerOwnerTable)
		.OnDragEnter(this, &SFieldGroup::OnDragEnterGroup, Field)
		.OnDragLeave(this, &SFieldGroup::OnDragLeaveGroup, Field)
		.OnDrop(this, &SFieldGroup::OnFieldDropGroup, Field)
		.ShowSelection(false)
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 4.0f))
			[
				Field.ToSharedRef()
			]
		];
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

TSharedPtr<FFieldGroup> SFieldGroup::GetGroup() const
{
	return FieldGroup;
}

void SFieldGroup::OnDragEnterGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField)
{
	TargetField->SetIsHovered(true);
}

void SFieldGroup::OnDragLeaveGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField)
{
	TargetField->SetIsHovered(false);
}

FReply SFieldGroup::OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField)
{
	if (TSharedPtr<FExposedFieldDragDropOp> DragDropOp = Event.GetOperationAs<FExposedFieldDragDropOp>())
	{
		return OnFieldDropGroup(DragDropOp, TargetField);
	}
	return FReply::Unhandled();
}

FReply SFieldGroup::OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SExposedFieldWidget> TargetField)
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
			if (TSharedPtr<SExposedFieldWidget> FieldWidget = DragDropOp->GetFieldWidget())
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
			if (TSharedPtr<FFieldGroup> DragOriginGroup = DragDropOp->GetDragOriginGroup())
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

EVisibility SFieldGroup::GetVisibilityAccordingToEditMode() const
{
	if (bEditMode.Get() == true)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
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
		FieldGroup->Name = FName(*InLabel.ToString());
		Panel->GetLayout().RenameGroup(FieldGroup->Id, FieldGroup->Name);
		NameTextBox->SetText(FText::FromName(FieldGroup->Name));
	}
}

SRemoteControlTarget::SRemoteControlTarget(FName Alias, TSharedRef<SRemoteControlPanel>& InOwnerPanel) : TargetAlias(Alias)
, WeakPanel(InOwnerPanel)
{
	GenerateExposedPropertyWidgets();
	GenerateExposedFunctionWidgets();
	BindPropertyWidgets();
}
void SRemoteControlTarget::RefreshTargetWidgets()
{
	for (TSharedRef<SExposedFieldWidget>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->Refresh();
	}
}

TSet<UObject*> SRemoteControlTarget::GetBoundObjects() const
{
	TSet<UObject*> Objects;
	for (const TSharedRef<SExposedFieldWidget>& FieldWidget : ExposedFieldWidgets)
	{
		FieldWidget->GetBoundObjects(Objects);
	}
	return Objects;
}

void SRemoteControlTarget::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap)
{
	for (TSharedRef<SExposedFieldWidget>& FieldWidget : ExposedFieldWidgets)
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

void SRemoteControlTarget::GenerateExposedPropertyWidgets()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FPropertyRowGeneratorArgs GeneratorArgs;

	for (const FRemoteControlProperty& Property : GetUnderlyingTarget().ExposedProperties)
	{
		TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
		UObject* CDO = GetTargetClass()->GetDefaultObject();
		RowGenerator->SetObjects({ CDO });
		TSharedPtr<SWidget> CDOWidget;

		if (TSharedPtr<IDetailTreeNode> Node = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), Property.GetQualifiedFieldName()))
		{
			CDOWidget = RemoteControlPanelUtil::CreatePropertyWidget(Node, Property.Label);
		}
		else
		{
			CDOWidget = SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f))
					.Text(INVTEXT("Widget cannot be displayed while no object is bound."))
				];
		}

		ExposedFieldWidgets.Add(SNew(SExposedFieldWidget, Property, RowGenerator, WeakPanel)
			.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
			[
				CDOWidget.ToSharedRef()
			]);
	}
}

void SRemoteControlTarget::GenerateExposedFunctionWidgets()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	for (const FRemoteControlFunction& RCFunction : GetUnderlyingTarget().ExposedFunctions)
	{
		TSharedRef<SVerticalBox> ArgsTarget = SNew(SVerticalBox);

		FPropertyRowGeneratorArgs GeneratorArgs;
		GeneratorArgs.bShouldShowHiddenProperties = true;

		TSharedRef<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GeneratorArgs);
		if (!RCFunction.Function)
		{
			ExposedFieldWidgets.Add(
				SNew(SExposedFieldWidget, RCFunction, RowGenerator, WeakPanel)
				.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
				.Content()
				[
					SNew(SBox)
					.Padding(FMargin(2.0f, 4.0f))
				[
					SNew(STextBlock)
					.Text(INVTEXT("Invalid function"))
				]
				]
			);
			continue;
		}

		RowGenerator->SetStructure(RCFunction.FunctionArguments);
		for (TFieldIterator<FProperty> It(RCFunction.Function); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Parm) || It->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
			{
				continue;
			}

			if (TSharedPtr<IDetailTreeNode> PropertyNode = RemoteControlPanelUtil::FindNode(RowGenerator->GetRootTreeNodes(), It->GetFName().ToString()))
			{
				FNodeWidgets Widget = PropertyNode->CreateNodeWidgets();

				if (Widget.NameWidget && Widget.ValueWidget)
				{
					TSharedPtr<SBox> ValueBox;
					TSharedRef<SHorizontalBox> FieldWidget =
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(FMargin(3.0f, 7.0f, 3.0f, 0.0f))
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(FText::FromString(It->GetName()))
						]
					+ SHorizontalBox::Slot()
						.Padding(FMargin(3.0f, 2.0f))
						.AutoWidth()
						[
							Widget.ValueWidget.ToSharedRef()
						];

					ArgsTarget->AddSlot()
						.AutoHeight()
						.Padding(FMargin(0.0f, 0.0f))
						[
							FieldWidget
						];
				}
				else if (Widget.WholeRowWidget)
				{
					ArgsTarget->AddSlot()
						.AutoHeight()
						[
							Widget.WholeRowWidget.ToSharedRef()
						];
				}
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
					.Text(FText::FromName(RCFunction.Label))
				]
			];

		TSharedRef<SWidget> ArgsWidget =
			SNew(SBorder)
			.Visibility_Raw(this, &SRemoteControlTarget::GetVisibilityAccordingToEditMode)
			.BorderImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HeaderSectionBorder"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			.Padding(FMargin(5.0f, 3.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ArgumentsLabel", "Arguments"))
				]
				+ SVerticalBox::Slot()
				.Padding(5.0f, 2.0f, 5.0f, 0.0f)
				.AutoHeight()
				[
					ArgsTarget
				]
			];

		ExposedFieldWidgets.Add(
			SNew(SExposedFieldWidget, RCFunction, RowGenerator, WeakPanel)
			.EditMode_Raw(this, &SRemoteControlTarget::GetPanelEditMode)
			.Content()
			[
				MoveTemp(ButtonWidget)
			]
			.OptionsContent()
			[
				MoveTemp(ArgsWidget)
			]
		);
	}
}

void SRemoteControlTarget::BindPropertyWidgets()
{
	if (TSharedPtr<SRemoteControlPanel> Panel = WeakPanel.Pin())
	{
		for (const TSharedRef<SExposedFieldWidget>& FieldWidget : ExposedFieldWidgets)
		{
			if (FieldWidget->GetFieldType() == EExposedFieldType::Property)
			{
				if (TOptional<FExposedProperty> Property = Panel->Preset->ResolveExposedProperty(FieldWidget->GetFieldLabel()))
				{
					FieldWidget->BindObjects(Property->OwnerObjects);
				}
			}
		}
	}
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

