// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/PropertyViewer/SPropertyViewerImpl.h"

#include "AdvancedWidgetsModule.h"
#include "Misc/IFilter.h"
#include "Misc/TextFilter.h"
#include "Framework/PropertyViewer/IFieldExpander.h"
#include "Framework/PropertyViewer/IFieldIterator.h"
#include "Framework/PropertyViewer/INotifyHook.h"
#include "Framework/PropertyViewer/PropertyPath.h"
#include "Framework/PropertyViewer/PropertyValueFactory.h"
#include "Framework/Views/TreeFilterHandler.h"

#include "Styling/SlateIconFinder.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/PropertyViewer/SFieldName.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"

#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/EditorEngine.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "SPropertyViewerImpl"

namespace UE::PropertyViewer::Private
{

/** 
 * Column name 
 */
static FName ColumnName_FieldPreWidget = "FieldPreWidget";
static FName ColumnName_Field = "Field";
static FName ColumnName_PropertyValue = "FieldValue";
static FName ColumnName_FieldPostWidget = "FieldPostWidget";


/**
 * FContainer
 */
FContainer::FContainer(SPropertyViewer::FHandle InIdentifier, UObject* InstanceToDisplay)
	: Identifier(InIdentifier)
	, Container(InstanceToDisplay->GetClass())
	, ObjectInstance(InstanceToDisplay)
	, bIsObject(true)
{
}


FContainer::FContainer(SPropertyViewer::FHandle InIdentifier, const UStruct* ClassToDisplay)
	: Identifier(InIdentifier)
	, Container(ClassToDisplay)
{
}


FContainer::FContainer(SPropertyViewer::FHandle InIdentifier, const UScriptStruct* Struct, void* Data)
	: Identifier(InIdentifier)
	, Container(Struct)
	, StructInstance(Data)
{
}


bool FContainer::IsValid() const
{
	if (const UClass* Class = Cast<UClass>(Container.Get()))
	{
		return !Class->HasAnyClassFlags(EClassFlags::CLASS_NewerVersionExists)
			&& (!bIsObject || (ObjectInstance.Get() && ObjectInstance.Get()->GetClass() == Class));
	}
	else if (const UScriptStruct* ScriptStruct = Cast<UScriptStruct>(Container.Get()))
	{
		return (ScriptStruct->StructFlags & (EStructFlags::STRUCT_Trashed)) != 0;
	}
	return false;
}


/**
 * FTreeNode
 */
TSharedRef<FTreeNode> FTreeNode::MakeContainer(const TSharedPtr<FContainer>& InContainer, TOptional<FText> InDisplayName)
{
	TSharedRef<FTreeNode> Result = MakeShared<FTreeNode>();
	Result->Container = InContainer;
	Result->OverrideDisplayName = InDisplayName;
	return Result;
}


TSharedRef<FTreeNode> FTreeNode::MakeField(TSharedPtr<FTreeNode> InParent, const FProperty* Property, TOptional<FText> InDisplayName)
{
	check(Property);
	TSharedRef<FTreeNode> Result = MakeShared<FTreeNode>();
	Result->Property = Property;
	Result->OverrideDisplayName = InDisplayName;
	Result->ParentNode = InParent;
	InParent->ChildNodes.Add(Result);
	return Result;
}


TSharedRef<FTreeNode> FTreeNode::MakeField(TSharedPtr<FTreeNode> InParent, const UFunction* Function, TOptional<FText> InDisplayName)
{
	check(Function);
	TSharedRef<FTreeNode> Result = MakeShared<FTreeNode>();
	Result->Function = Function;
	Result->OverrideDisplayName = InDisplayName;
	Result->ParentNode = InParent;
	InParent->ChildNodes.Add(Result);
	return Result;
}


FPropertyPath FTreeNode::GetPropertyPath() const
{
	const FTreeNode* CurrentNode = this;
	FPropertyPath::FPropertyArray Properties;
	while (CurrentNode)
	{
		if (CurrentNode->Property)
		{
			Properties.Insert(Property, 0);
		}

		TSharedPtr<FContainer> ContainerPin = CurrentNode->Container.Pin();
		if (ContainerPin)
		{
			if (ContainerPin->IsObjectInstance())
			{
				if (UObject* ObjectInstance = ContainerPin->GetObjectInstance())
				{
					return FPropertyPath(ObjectInstance, MoveTemp(Properties));
				}
			}
			else if (ContainerPin->IsScriptStructInstance())
			{
				if (const UStruct* ContainerStruct = ContainerPin->GetStruct())
				{
					return FPropertyPath(CastChecked<const UScriptStruct>(ContainerStruct), ContainerPin->GetScriptStructInstance(), MoveTemp(Properties));
				}
			}
			else if (const UClass* ContainerClass = Cast<const UClass>(ContainerPin->GetStruct()))
			{
				return FPropertyPath(ContainerClass->GetDefaultObject(), MoveTemp(Properties));
			}
			return FPropertyPath();
		}

		if (TSharedPtr<FTreeNode> ParentNodePin = CurrentNode->ParentNode.Pin())
		{
			CurrentNode = ParentNodePin.Get();
		}
		else if (ContainerPin == nullptr)
		{
			ensureMsgf(false, TEXT("The tree is not owned by a container"));
			return FPropertyPath();
		}
	}
	return FPropertyPath();
}


TSharedPtr<FContainer> FTreeNode::GetOwnerContainer() const
{
	TSharedPtr<FContainer> Result;
	const FTreeNode* CurrentNode = this;
	while(CurrentNode)
	{
		if (TSharedPtr<FContainer> ContainerPin = CurrentNode->Container.Pin())
		{
			return ContainerPin;
		}
		if (TSharedPtr<FTreeNode> ParentNodePin = CurrentNode->ParentNode.Pin())
		{
			CurrentNode = ParentNodePin.Get();
		}
		else
		{
			ensureMsgf(false, TEXT("The tree is not owned by a container"));
			break;
		}
	}
	return TSharedPtr<FContainer>();
}


void FTreeNode::GetFilterStrings(TArray<FString>& OutStrings) const
{
	if (Property)
	{
		OutStrings.Add(Property->GetName());
#if WITH_EDITORONLY_DATA
		OutStrings.Add(Property->GetDisplayNameText().ToString());
#endif
	}
	if (const UFunction* FunctionPtr = Function.Get())
	{
		OutStrings.Add(FunctionPtr->GetName());
#if WITH_EDITORONLY_DATA
		OutStrings.Add(FunctionPtr->GetDisplayNameText().ToString());
#endif
	}
	if (const TSharedPtr<FContainer> ContainerPtr = Container.Pin())
	{
		if (const UStruct* StructPtr = ContainerPtr->GetStruct())
		{
			OutStrings.Add(StructPtr->GetName());
#if WITH_EDITORONLY_DATA
			OutStrings.Add(StructPtr->GetDisplayNameText().ToString());
#endif
		}
	}
	if (GetOverrideDisplayName().IsSet())
	{
		OutStrings.Add(GetOverrideDisplayName().GetValue().ToString());
	}
}


void FTreeNode::BuildChildNodes(IFieldIterator& FieldIterator, IFieldExpander& FieldExpander, bool bSortChildNode)
{
	BuildChildNodesRecursive(FieldIterator, FieldExpander, bSortChildNode, 2);
}


void FTreeNode::BuildChildNodesRecursive(IFieldIterator& FieldIterator, IFieldExpander& FieldExpander, bool bSortChildNode, int32 RecursiveCount)
{
	if (RecursiveCount <= 0)
	{
		return;
	}
	--RecursiveCount;

	ChildNodes.Reset();

	const UStruct* ChildStructType = nullptr;
	if (Property)
	{
		if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
		{
			ChildStructType = StructProperty->Struct;
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
		{
			// If the container is an instance and the object is nullptr, do not expand this object.
			bool bIsNullptr = false;
			if (TSharedPtr<FContainer> OwnerContainer = GetOwnerContainer())
			{
				if (OwnerContainer->IsInstance())
				{
					const FPropertyPath PropertyPath = GetPropertyPath();
					if (const void* ContainerPtr = PropertyPath.GetContainerPtr())
					{
						bIsNullptr = ObjectProperty->GetObjectPropertyValue_InContainer(ContainerPtr) == nullptr;
					}
				}
			}

			if (!bIsNullptr && FieldExpander.CanExpandObject(ObjectProperty->PropertyClass))
			{
				//todo expand the object instance, not the object class
				//todo we want to confirm that that thing didn't change frame to frame
				ChildStructType = ObjectProperty->PropertyClass;
			}
		}
	}
	else if (const UFunction* FunctionPtr = Function.Get())
	{
		if (FieldExpander.CanExpandFunction(FunctionPtr))
		{
			ChildStructType = FunctionPtr;
		}
	}
	else if (const TSharedPtr<FContainer> ContainerPin = Container.Pin())
	{
		ChildStructType = ContainerPin->GetStruct();
	}

	if (ChildStructType)
	{
		for (const FFieldVariant FieldIt : FieldIterator.GetFields(ChildStructType))
		{
			if (const FProperty* PropertyIt = FieldIt.Get<FProperty>())
			{
				TSharedPtr<FTreeNode> Node = MakeField(AsShared(), PropertyIt, TOptional<FText>());
				Node->BuildChildNodesRecursive(FieldIterator, FieldExpander, bSortChildNode, RecursiveCount);
			}
			if (const UFunction* FunctionIt = FieldIt.Get<UFunction>())
			{
				TSharedPtr<FTreeNode> Node = MakeField(AsShared(), FunctionIt, TOptional<FText>());
				Node->BuildChildNodesRecursive(FieldIterator, FieldExpander, bSortChildNode, RecursiveCount);
			}
		}

		if (bSortChildNode)
		{
			ChildNodes.Sort(Sort);
		}
	}

	bChildGenerated = true;
}


bool FTreeNode::Sort(const TSharedPtr<FTreeNode>& NodeA, const TSharedPtr<FTreeNode>& NodeB)
{
	bool bIsContainerA = NodeA->IsContainer();
	bool bIsContainerB = NodeB->IsContainer();
	bool bIsObjectPropertyA = CastField<FObjectPropertyBase>(NodeA->Property) != nullptr;
	bool bIsObjectPropertyB = CastField<FObjectPropertyBase>(NodeB->Property) != nullptr;
	bool bIsFunctionA = NodeA->Function.Get() != nullptr;
	bool bIsFunctionB = NodeB->Function.Get() != nullptr;
	const FName NodeStrA = bIsContainerA ? NodeA->GetContainer()->GetStruct()->GetFName() : NodeA->GetField().GetFName();
	const FName NodeStrB = bIsContainerB ? NodeB->GetContainer()->GetStruct()->GetFName() : NodeB->GetField().GetFName();

	if (bIsFunctionA && bIsFunctionB)
	{
		return NodeStrA.LexicalLess(NodeStrB);
	}
	if (bIsObjectPropertyA && bIsObjectPropertyB)
	{
		return NodeStrA.LexicalLess(NodeStrB);
	}

	if (bIsFunctionA)
	{
		return true;
	}
	if (bIsFunctionB)
	{
		return false;
	}
	if (bIsObjectPropertyA)
	{
		return true;
	}
	if (bIsObjectPropertyB)
	{
		return false;
	}

	return NodeStrA.LexicalLess(NodeStrB);
};


/**
 * FPropertyViewerImpl
 */
FPropertyViewerImpl::FPropertyViewerImpl(const SPropertyViewer::FArguments& InArgs)
{
	FieldIterator = InArgs._FieldIterator;
	FieldExpander = InArgs._FieldExpander;
	if (FieldIterator == nullptr)
	{
		FieldIterator = new FFieldIterator_BlueprintVisible();
		bOwnFieldIterator = true;
	}
	if (FieldExpander == nullptr)
	{
		FieldExpander = new FFieldExpander_NoExpand();
		bOwnFieldExpander = true;
	}

	NotifyHook = InArgs._NotifyHook;
	OnGetPreSlot = InArgs._OnGetPreSlot;
	OnGetPostSlot = InArgs._OnGetPostSlot;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	PropertyVisibility = InArgs._PropertyVisibility;
	bSanitizeName = InArgs._bSanitizeName;
	bShowFieldIcon = InArgs._bShowFieldIcon;
	bSortChildNode = InArgs._bSortChildNode;

#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().AddRaw(this, &FPropertyViewerImpl::HandleBlueprintCompiled);
	}
#endif
}


FPropertyViewerImpl::~FPropertyViewerImpl()
{
	if (bOwnFieldIterator)
	{
		delete FieldIterator;
	}
	if (bOwnFieldExpander)
	{
		delete FieldExpander;
	}
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().RemoveAll(this);
	}
#endif
}


TSharedRef<SWidget> FPropertyViewerImpl::Construct(const SPropertyViewer::FArguments& InArgs)
{
	TSharedPtr<SHorizontalBox> SearchBox;
	if (InArgs._bShowSearchBox || InArgs._SearchBoxPreSlot.Widget != SNullWidget::NullWidget || InArgs._SearchBoxPostSlot.Widget != SNullWidget::NullWidget)
	{
		SearchBox = SNew(SHorizontalBox);
		if (InArgs._SearchBoxPreSlot.Widget != SNullWidget::NullWidget)
		{
			SearchBox->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					InArgs._SearchBoxPreSlot.Widget
				];
		}
		if (InArgs._bShowSearchBox)
		{
			SearchBox->AddSlot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					CreateSearch()
				];
		}
		else
		{
			SearchBox->AddSlot()
				.FillWidth(1.f)
				[
					SNullWidget::NullWidget
				];
		}
		if (InArgs._SearchBoxPostSlot.Widget != SNullWidget::NullWidget)
		{
			SearchBox->AddSlot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					InArgs._SearchBoxPostSlot.Widget
				];
		}
	}

	TSharedRef<SWidget> ConstructedTree = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Recessed"))
		.Padding(0)
		[
			CreateTree(OnGetPreSlot.IsBound(), PropertyVisibility != SPropertyViewer::EPropertyVisibility::Hidden, OnGetPostSlot.IsBound())
		];

	if (SearchBox)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(4)
			.AutoHeight()
			[
				SearchBox.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				ConstructedTree
			];
	}
	else
	{
		return ConstructedTree;
	}
}


void FPropertyViewerImpl::AddContainer(SPropertyViewer::FHandle Identifier, const UStruct* Struct)
{
	TSharedPtr<FContainer> NewContainer = MakeShared<FContainer>(Identifier, Struct);
	Containers.Add(NewContainer);

	AddContainerInternal(Identifier, NewContainer);
}


void FPropertyViewerImpl::AddContainerInstance(SPropertyViewer::FHandle Identifier, UObject* Object)
{
	TSharedPtr<FContainer> NewContainer = MakeShared<FContainer>(Identifier, Object);
	Containers.Add(NewContainer);

	AddContainerInternal(Identifier, NewContainer);
}


void FPropertyViewerImpl::AddContainerInstance(SPropertyViewer::FHandle Identifier, const UScriptStruct* Struct, void* Data)
{
	TSharedPtr<FContainer> NewContainer = MakeShared<FContainer>(Identifier, Struct, Data);
	Containers.Add(NewContainer);

	AddContainerInternal(Identifier, NewContainer);
}


void FPropertyViewerImpl::AddContainerInternal(SPropertyViewer::FHandle Identifier, TSharedPtr<FContainer>& NewContainer)
{
	TSharedPtr<FTreeNode> NewNode = FTreeNode::MakeContainer(NewContainer, TOptional<FText>());
	NewNode->BuildChildNodes(*FieldIterator, *FieldExpander, bSortChildNode);
	TreeSource.Add(NewNode);

	if (TreeWidget)
	{
		TreeWidget->SetItemExpansion(NewNode, true);
	}
	if (FilterHandler)
	{
		FilterHandler->RefreshAndFilterTree();
	}
}


void FPropertyViewerImpl::Remove(SPropertyViewer::FHandle Identifier)
{
	bool bRemoved = false;
	for (int32 Index = 0; Index < TreeSource.Num(); ++Index)
	{
		if (TSharedPtr<FContainer> Container = TreeSource[Index]->GetContainer())
		{
			if (Container->GetIdentifier() == Identifier)
			{
				TreeSource.RemoveAt(Index);
				bRemoved = true;
				break;
			}
		}
	}

	for (int32 Index = 0; Index < Containers.Num(); ++Index)
	{
		if (Containers[Index]->GetIdentifier() == Identifier)
		{
			Containers.RemoveAt(Index);
			break;
		}
	}

	if (bRemoved)
	{
		if (FilterHandler)
		{
			FilterHandler->RefreshAndFilterTree();
		}
		else
		{
			TreeWidget->RequestTreeRefresh();
		}
	}
}


void FPropertyViewerImpl::RemoveAll()
{
	bool bRemoved = TreeSource.Num() > 0 || Containers.Num() > 0;
	TreeSource.Reset();
	Containers.Reset();

	if (bRemoved)
	{
		if (FilterHandler)
		{
			FilterHandler->RefreshAndFilterTree();
		}
		else
		{
			TreeWidget->RequestTreeRefresh();
		}
	}
}


TSharedRef<SWidget> FPropertyViewerImpl::CreateSearch()
{
	SearchFilter = MakeShared<FTextFilter>(FTextFilter::FItemToStringArray::CreateSP(this, &FPropertyViewerImpl::HandleGetFilterStrings));

	FilterHandler = MakeShared<FTreeFilter>();
	FilterHandler->SetFilter(SearchFilter.Get());
	FilterHandler->SetRootItems(&TreeSource, &FilteredTreeSource);
	FilterHandler->SetGetChildrenDelegate(FTreeFilter::FOnGetChildren::CreateRaw(this, &FPropertyViewerImpl::HandleGetChildren));

	return SAssignNew(SearchBoxWidget, SSearchBox)
		.HintText(LOCTEXT("SearchHintText", "Search"))
		.OnTextChanged(this, &FPropertyViewerImpl::HandleSearchChanged);
}


FText FPropertyViewerImpl::SetRawFilterText(const FText& InFilterText)
{
	const bool bNewFilteringEnabled = !InFilterText.IsEmpty();
	FilterHandler->SetIsEnabled(bNewFilteringEnabled);
	SearchFilter->SetRawFilterText(InFilterText);
	FilterHandler->RefreshAndFilterTree();

	for (const TSharedPtr<FTreeNode>& Node : FilteredTreeSource)
	{
		SetHighlightTextRecursive(Node, InFilterText);
	}

	return SearchFilter->GetFilterErrorText();
}


TSharedRef<SWidget> FPropertyViewerImpl::CreateTree(bool bHasPreWidget, bool bShowPropertyValue, bool bHasPostWidget)
{
	TSharedPtr<SHeaderRow> HeaderRowWidget;
	if (bHasPreWidget || bShowPropertyValue || bHasPostWidget)
	{
		bUseRows = true;

		HeaderRowWidget = SNew(SHeaderRow)
		.Visibility(EVisibility::Collapsed);

		if (bHasPreWidget)
		{
			HeaderRowWidget->AddColumn(
				SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnName_FieldPreWidget)
				.DefaultLabel(LOCTEXT("PropertyPreWidget", ""))
			);
		}

		HeaderRowWidget->AddColumn(
			SHeaderRow::FColumn::FArguments()
			.ColumnId(ColumnName_Field)
			.DefaultLabel(LOCTEXT("FieldName", "Field Name"))
			.FillWidth(0.75f)
		);

		if (bShowPropertyValue)
		{
			HeaderRowWidget->AddColumn(
				SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnName_PropertyValue)
				.FillSized(100)
				.DefaultLabel(LOCTEXT("PropertyValue", "Field Value"))
				.FillWidth(0.25f)
			);
		}
		if (bHasPostWidget)
		{
			HeaderRowWidget->AddColumn(
				SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnName_FieldPostWidget)
				.DefaultLabel(LOCTEXT("PropertyPostWidget", ""))
			);
		}
	}

	if (FilterHandler)
	{
		SAssignNew(TreeWidget, STreeView<TSharedPtr<FTreeNode>>)
			.ItemHeight(1.0f)
			.TreeItemsSource(&FilteredTreeSource)
			.SelectionMode(ESelectionMode::Single)
			.OnGetChildren(FilterHandler.ToSharedRef(), &FTreeFilter::OnGetFilteredChildren)
			.OnGenerateRow(this, &FPropertyViewerImpl::HandleGenerateRow)
			.OnSelectionChanged(this, &FPropertyViewerImpl::HandleSelectionChanged)
			.OnContextMenuOpening(this, &FPropertyViewerImpl::HandleContextMenuOpening)
			.HeaderRow(HeaderRowWidget);
		FilterHandler->SetTreeView(TreeWidget.Get());
	}
	else
	{
		SAssignNew(TreeWidget, STreeView<TSharedPtr<FTreeNode>>)
			.ItemHeight(1.0f)
			.TreeItemsSource(&TreeSource)
			.SelectionMode(ESelectionMode::Single)
			.OnGetChildren(this, &FPropertyViewerImpl::HandleGetChildren)
			.OnGenerateRow(this, &FPropertyViewerImpl::HandleGenerateRow)
			.OnSelectionChanged(this, &FPropertyViewerImpl::HandleSelectionChanged)
			.OnContextMenuOpening(this, &FPropertyViewerImpl::HandleContextMenuOpening)
			.HeaderRow(HeaderRowWidget);
	}

	return TreeWidget.ToSharedRef();
}


void FPropertyViewerImpl::SetHighlightTextRecursive(const TSharedPtr<FTreeNode>& OwnerNode, const FText& HighlightText)
{
	if (TSharedPtr<SFieldName> PropertyNameWidget = OwnerNode->PropertyWidget.Pin())
	{
		PropertyNameWidget->SetHighlightText(HighlightText);
	}

	if (OwnerNode->bChildGenerated)
	{
		for (const TSharedPtr<FTreeNode>& Node : OwnerNode->ChildNodes)
		{
			SetHighlightTextRecursive(Node, HighlightText);
		}
	}
}


void FPropertyViewerImpl::HandleGetFilterStrings(TSharedPtr<FTreeNode> Item, TArray<FString>& OutStrings)
{
	Item->GetFilterStrings(OutStrings);
}


TSharedRef<ITableRow> FPropertyViewerImpl::HandleGenerateRow(TSharedPtr<FTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FText HighlightText = SearchFilter ? SearchFilter->GetRawFilterText() : FText::GetEmpty();

	TSharedPtr<SWidget> ItemWidget;
	if (TSharedPtr<FContainer> ContainerPin = Item->GetContainer())
	{
		if (ContainerPin->IsValid())
		{
			if (const UClass* Class = Cast<const UClass>(ContainerPin->GetStruct()))
			{
				ItemWidget = SNew(SFieldName, Class)
					.bShowIcon(true)
					.bSanitizeName(bSanitizeName)
					.OverrideDisplayName(Item->GetOverrideDisplayName());
			}
			if (const UScriptStruct* Struct = Cast<const UScriptStruct>(ContainerPin->GetStruct()))
			{
				ItemWidget = SNew(SFieldName, Struct)
					.bShowIcon(true)
					.bSanitizeName(bSanitizeName)
					.OverrideDisplayName(Item->GetOverrideDisplayName());
			}
		}
	}
	else if (TSharedPtr<FContainer> OwnerContainerPin = Item->GetOwnerContainer())
	{
		if (OwnerContainerPin->IsValid())
		{
			if (FFieldVariant FieldVariant = Item->GetField())
			{
				if (FProperty* Property = FieldVariant.Get<FProperty>())
				{
					TSharedPtr<SFieldName> FieldName = SNew(SFieldName, Property)
						.bShowIcon(bShowFieldIcon)
						.bSanitizeName(bSanitizeName)
						.OverrideDisplayName(Item->GetOverrideDisplayName())
						.HighlightText(HighlightText);
					Item->PropertyWidget = FieldName;
					ItemWidget = FieldName;
				}
				else if (UFunction* Function = FieldVariant.Get<UFunction>())
				{
					TSharedPtr<SFieldName> FieldName = SNew(SFieldName, Function)
						.bShowIcon(bShowFieldIcon)
						.bSanitizeName(bSanitizeName)
						.OverrideDisplayName(Item->GetOverrideDisplayName())
						.HighlightText(HighlightText);
					Item->PropertyWidget = FieldName;
					ItemWidget = FieldName;
				}
			}
		}
	}

	struct SMultiRowType : public SMultiColumnTableRow<TSharedPtr<FTreeNode>>
	{
		void Construct(const FArguments& Args, const TSharedRef<FPropertyViewerImpl> PropertyViewer, const TSharedRef<STableViewBase>& OwnerTableView, TSharedRef<FTreeNode> InItem, TSharedRef<SWidget> InFieldWidget)
		{
			PropertyViewOwner = PropertyViewer;
			Item = InItem;
			FieldWidget = InFieldWidget;
			SMultiColumnTableRow<TSharedPtr<FTreeNode>>::Construct(Args, OwnerTableView);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			if (ColumnName == ColumnName_Field)
			{
				return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
					.IndentAmount(16)
					.ShouldDrawWires(true)
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					FieldWidget.ToSharedRef()
				];
			}

			if (ColumnName == ColumnName_PropertyValue)
			{
				TSharedPtr<FTreeNode> ItemPin = Item.Pin();
				TSharedPtr<FPropertyViewerImpl> PropertyViewOwnerPin = PropertyViewOwner.Pin();
				if (ItemPin && PropertyViewOwnerPin)
				{
					if (ItemPin->IsField())
					{
						FFieldVariant Field = ItemPin->GetField();
						if (!Field.IsUObject())
						{
							bool bCanEditContainer = false;
							if (const TSharedPtr<FContainer> OwnerContainer = ItemPin->GetOwnerContainer())
							{
								bCanEditContainer = OwnerContainer->CanEdit();
							}

							FPropertyValueFactory::FGenerateArgs Args;
							Args.Path = ItemPin->GetPropertyPath();
							Args.NotifyHook = PropertyViewOwnerPin->NotifyHook;
							Args.bCanEditValue = bCanEditContainer
								&& PropertyViewOwnerPin->PropertyVisibility == SPropertyViewer::EPropertyVisibility::Editable
								&& Args.Path.GetLastProperty() != nullptr
								&& !Args.Path.GetLastProperty()->HasAllPropertyFlags(CPF_BlueprintReadOnly);

							FAdvancedWidgetsModule& Module = FAdvancedWidgetsModule::GetModule();
							TSharedPtr<SWidget> ValueWidget = Module.GetPropertyValueFactory().Generate(Args);
							if (!ValueWidget)
							{
								ValueWidget = Module.GetPropertyValueFactory().GenerateDefault(Args);
							}

							if (ValueWidget)
							{
								return SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									ValueWidget.ToSharedRef()
								]
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								[
									SNullWidget::NullWidget
								];
							}
						}
					}
				}
			}

			if (ColumnName == ColumnName_FieldPreWidget || ColumnName == ColumnName_FieldPostWidget)
			{
				TSharedPtr<FTreeNode> ItemPin = Item.Pin();
				TSharedPtr<FPropertyViewerImpl> PropertyViewOwnerPin = PropertyViewOwner.Pin();
				if (ItemPin && PropertyViewOwnerPin)
				{
					SPropertyViewer::FGetFieldWidget& OnGetWidget = (ColumnName == ColumnName_FieldPreWidget) ? PropertyViewOwnerPin->OnGetPreSlot : PropertyViewOwnerPin->OnGetPostSlot;
					TSharedPtr<SWidget> PreWidget;
					if (TSharedPtr<FContainer> ContainerPin = ItemPin->GetContainer())
					{
						PreWidget = OnGetWidget.Execute(ContainerPin->GetIdentifier(), FFieldVariant(ContainerPin->GetStruct()));
					}
					else if (FFieldVariant FieldVariant = ItemPin->GetField())
					{
						PreWidget = OnGetWidget.Execute(SPropertyViewer::FHandle(), FieldVariant);
					}

					if (PreWidget)
					{
						return PreWidget.ToSharedRef();
					}
				}
			}

			return SNullWidget::NullWidget;
		}

	private:
		TWeakPtr<FTreeNode> Item;
		TSharedPtr<SWidget> FieldWidget;
		TWeakPtr<FPropertyViewerImpl> PropertyViewOwner;
	};


	TSharedRef<SWidget> FieldWidget = ItemWidget ? ItemWidget.ToSharedRef() : SNullWidget::NullWidget;
	if (bUseRows)
	{
		return SNew(SMultiRowType, AsShared(), OwnerTable, Item.ToSharedRef(), FieldWidget)
			.Padding(0.0f);
	}

	using SSimpleRowType = STableRow<TSharedPtr<FTreeNode>>;
	return SNew(SSimpleRowType, OwnerTable)
		.Padding(0.0f)
		.Content()
		[
			FieldWidget
		];
}


void FPropertyViewerImpl::HandleGetChildren(TSharedPtr<FTreeNode> InParent, TArray<TSharedPtr<FTreeNode>>& OutChildren)
{
	if (!InParent->bChildGenerated)
	{			
		// Do not build when filtering (only search in what it's already been built)
		if (FilterHandler == nullptr || !FilterHandler->GetIsEnabled())
		{
			InParent->BuildChildNodes(*FieldIterator, *FieldExpander, bSortChildNode);
		}
	}
	OutChildren = InParent->ChildNodes;
}


TSharedPtr<SWidget> FPropertyViewerImpl::HandleContextMenuOpening()
{
	if (OnContextMenuOpening.IsBound())
	{
		TArray<TSharedPtr<FTreeNode>> Items = TreeWidget->GetSelectedItems();
		if (Items.Num() == 1 && Items[0].IsValid())
		{
			if (TSharedPtr<FContainer> ContainerPin = Items[0]->GetContainer())
			{
				return OnContextMenuOpening.Execute(ContainerPin->GetIdentifier(), FFieldVariant(ContainerPin->GetStruct()));
			}
			else if (FFieldVariant FieldVariant = Items[0]->GetField())
			{
				return OnContextMenuOpening.Execute(SPropertyViewer::FHandle(), FieldVariant);
			}
		}
	}
	return TSharedPtr<SWidget>();
}


void FPropertyViewerImpl::HandleSelectionChanged(TSharedPtr<FTreeNode> Item, ESelectInfo::Type SelectionType)
{
	if (OnSelectionChanged.IsBound())
	{
		if (TSharedPtr<FContainer> ContainerPin = Item->GetContainer())
		{
			OnSelectionChanged.Execute(ContainerPin->GetIdentifier(), FFieldVariant(ContainerPin->GetStruct()), SelectionType);
		}
		else if (FFieldVariant FieldVariant = Item->GetField())
		{
			OnSelectionChanged.Execute(SPropertyViewer::FHandle(), FieldVariant, SelectionType);
		}
	}
}


void FPropertyViewerImpl::HandleSearchChanged(const FText& InFilterText)
{
	if (SearchBoxWidget)
	{
		SearchBoxWidget->SetError(SetRawFilterText(InFilterText));
	}
}


#if WITH_EDITOR
void FPropertyViewerImpl::HandleBlueprintCompiled()
{
	bool bRemoved = false;
	for (int32 Index = TreeSource.Num()-1; Index >= 0 ; --Index)
	{
		if (TSharedPtr<FContainer> Container = TreeSource[Index]->GetContainer())
		{
			if (!Container->IsValid())
			{
				TreeSource.RemoveAt(Index);
				bRemoved = true;
			}
		}
	}

	for (int32 Index = Containers.Num()-1; Index >= 0; --Index)
	{
		if (!Containers[Index]->IsValid())
		{
			Containers.RemoveAt(Index);
		}
	}

	if (bRemoved)
	{
		if (FilterHandler)
		{
			FilterHandler->RefreshAndFilterTree();
		}
		else
		{
			TreeWidget->RequestTreeRefresh();
		}
	}
}
#endif //WITH_EDITOR


} //namespace

#undef LOCTEXT_NAMESPACE
