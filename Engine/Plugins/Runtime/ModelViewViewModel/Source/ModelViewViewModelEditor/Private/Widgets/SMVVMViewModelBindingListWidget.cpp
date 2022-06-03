// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMViewModelBindingListWidget.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "Textures/SlateIcon.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMFieldVariant.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/SlateIconFinder.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STreeView.h"


#define LOCTEXT_NAMESPACE "SViewModelBindingListWidget"

namespace UE::MVVM
{

namespace Private
{

static TArray<FMVVMAvailableBinding> GetBindingsList(TSubclassOf<UObject> Class)
{
	TArray<FMVVMAvailableBinding> ViewModelAvailableBindingsList = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetAvailableBindings(Class);
	const TArray<FMVVMConstFieldVariant> ChildViewModels = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>()->GetChildViewModels(Class);
	for (const FMVVMConstFieldVariant& Field : ChildViewModels)
	{
		if (!ViewModelAvailableBindingsList.ContainsByPredicate([&Field](const FMVVMAvailableBinding& Other){ return Other.GetBindingName().ToName() == Field.GetName(); }))
		{
			ViewModelAvailableBindingsList.Add(FMVVMAvailableBinding(FMVVMBindingName(Field.GetName()), true, false));
		}
	}

	return ViewModelAvailableBindingsList;
}


/** 
 * 
 */
class FAvailableBindingNode : public TSharedFromThis<FAvailableBindingNode>
{
public:
	FAvailableBindingNode(SViewModelBindingListWidget* InOwner, const FMVVMAvailableBinding& InBinding, TSubclassOf<UObject> InViewModelClass)
		: Owner(InOwner)
		, bIsRoot(false)
		, Binding(InBinding)
		, ViewModelOwnerClass(InViewModelClass)
		, ViewModelClassProperty(GetCacheViewModelClassProperty())
	{
	}


	FAvailableBindingNode(SViewModelBindingListWidget* InOwner, const SViewModelBindingListWidget::FViewModel& InViewModel)
		: Owner(InOwner)
		, bIsRoot(true)
		, ViewModel(InViewModel)
	{
		check(InViewModel.Class && InViewModel.Class->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()));
	}

private:
	SViewModelBindingListWidget* Owner = nullptr;

	const bool bIsRoot = false;
	const SViewModelBindingListWidget::FViewModel ViewModel;

	const FMVVMAvailableBinding Binding;
	const TSubclassOf<UObject> ViewModelOwnerClass;
	const TSubclassOf<UObject> ViewModelClassProperty; // if the property is also a viewmodel

	bool bChildGenerated = false;
	TArray<TSharedPtr<FAvailableBindingNode>, TInlineAllocator<1>> ChildNodes;
	TWeakPtr<SInlineEditableTextBlock> ViewModelEditTextBlock;

public:
	bool ShowContextMenu() const
	{
		return bIsRoot;
	}

	bool CanRename() const
	{
		return bIsRoot && ViewModelEditTextBlock.IsValid();
	}


	void EnterRename() const
	{
		if (bIsRoot)
		{
			if (TSharedPtr<SInlineEditableTextBlock> SafeEditBox = ViewModelEditTextBlock.Pin())
			{
				SafeEditBox->EnterEditingMode();
			}
		}
	}


	TArray<TSharedPtr<FAvailableBindingNode>> GetChildNodes()
	{
		if (!bChildGenerated && ViewModelClassProperty.Get() != nullptr)
		{
			// Do not build when filtering (only search in what it's already been built)
			if (!Owner->FilterHandler->GetIsEnabled())
			{
				BuildChildNodesRecursive(ViewModelClassProperty, 2);
			}
		}
		return TArray<TSharedPtr<FAvailableBindingNode>>(ChildNodes);
	}


	void BuildChildNodes()
	{
		BuildChildNodesRecursive(ViewModel.Class, 3);
	}


	FText GetDisplayName()
	{
		if (bIsRoot)
		{
			return !ViewModel.Name.IsNone() ? FText::FromName(ViewModel.Name) : ViewModel.Class->GetDisplayNameText();
		}

		return FText::FromName(Binding.GetBindingName().ToName());
	}


	void GetFilterStrings(TArray<FString>& OutStrings) const
	{
		auto AddClass = [&OutStrings](UClass* Class)
		{
			OutStrings.Add(Class->GetName());
			OutStrings.Add(Class->GetDisplayNameText().ToString());
		};

		if (bIsRoot)
		{
			if (!ViewModel.Name.IsNone())
			{
				OutStrings.Add(ViewModel.Name.ToString());
			}
			AddClass(ViewModel.Class);
		}
		else
		{
			OutStrings.Add(Binding.GetBindingName().ToString());
			if (ViewModelOwnerClass.Get())
			{
				AddClass(ViewModelOwnerClass.Get());
			}
			if (ViewModelClassProperty.Get())
			{
				AddClass(ViewModelClassProperty.Get());
			}
		}
	}


	FText GetToolTip()
	{
		if (bIsRoot)
		{
			return ViewModel.Class->GetDisplayNameText();
		}

		FMVVMFieldVariant FieldVariant = BindingHelper::FindFieldByName(ViewModelOwnerClass, Binding.GetBindingName());
		if (FieldVariant.IsProperty())
		{
			FProperty* Property = FieldVariant.GetProperty();
			check(Property);
			return FText::FormatOrdered(INVTEXT("{0}\n{1}"), FText::FromString(Property->GetCPPType()), Property->GetMetaDataText("ToolTip"));
		}

		if (FieldVariant.IsFunction())
		{
			UFunction* Function = FieldVariant.GetFunction();
			check(Function);
			FProperty* Property = Function->GetReturnProperty();
			if (!Property)
			{
				return FText::FormatOrdered(INVTEXT("void\n{0}"), Function->GetMetaDataText("ToolTip"));
			}
			return FText::FormatOrdered(INVTEXT("{0}\n{1}"), FText::FromString(Property->GetCPPType()), Function->GetMetaDataText("ToolTip"));
		}

		return FText::GetEmpty();
	}


	TSharedRef<SWidget> CreateIconWidget()
	{
		if (bIsRoot)
		{
			return SNew(SImage).Image(FSlateIconFinder::FindIconBrushForClass(ViewModel.Class));
		}

		FMVVMFieldVariant FieldVariant = BindingHelper::FindFieldByName(ViewModelOwnerClass, Binding.GetBindingName());
			
		return SNew(SMVVMFieldIcon)
			.Field(FieldVariant);
	}


	TSharedRef<SWidget> CreateDetailWidget(TAttribute<FText> GetFilterText, const SViewModelBindingListWidget::FOnViewModelRenamed& OnRenamed)
	{
		ViewModelEditTextBlock.Reset();
		if (OnRenamed.IsBound() && bIsRoot)
		{
			return SAssignNew(ViewModelEditTextBlock, SInlineEditableTextBlock)
				.Text(GetDisplayName())
				.HighlightText(GetFilterText)
				.OnVerifyTextChanged(this, &FAvailableBindingNode::HandleVerifyNameTextChanged, OnRenamed)
				.OnTextCommitted(this, &FAvailableBindingNode::HandleNameTextCommited, OnRenamed);
		}

		return SNew(STextBlock)
			.Text(GetDisplayName())
			.HighlightText(GetFilterText);
	}
	
private:
	bool HandleVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage, SViewModelBindingListWidget::FOnViewModelRenamed OnRenamed)
	{
		return OnRenamed.Execute(ViewModel, InText, false, OutErrorMessage);
	}
	
	
	void HandleNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo, SViewModelBindingListWidget::FOnViewModelRenamed OnRenamed)
	{
		if (CommitInfo == ETextCommit::OnEnter)
		{
			FText OutErrorMessage;
			OnRenamed.Execute(ViewModel, InText, true, OutErrorMessage);
		}
	}


	static bool Sort(const TSharedPtr<FAvailableBindingNode>& Node, const TSharedPtr<FAvailableBindingNode>& OtherNode)
	{
		TSubclassOf<UObject> NodeClass = Node->ViewModelClassProperty;
		TSubclassOf<UObject> OtherNodeClass = OtherNode->ViewModelClassProperty;
		const FName NodeStr = Node->Binding.GetBindingName().ToName();
		const FName OtherNodeStr = OtherNode->Binding.GetBindingName().ToName();

		if (NodeClass && OtherNodeClass)
		{
			return NodeStr.LexicalLess(OtherNodeStr);
		}

		if (NodeClass)
		{
			return true;
		}

		if (OtherNodeClass)
		{
			return false;
		}

		return NodeStr.LexicalLess(OtherNodeStr);
	}

private:
	TSubclassOf<UObject> GetCacheViewModelClassProperty() const
	{
		if (bIsRoot)
		{
			return nullptr;
		}

		FMVVMFieldVariant FieldVariant = BindingHelper::FindFieldByName(ViewModelOwnerClass, Binding.GetBindingName());
		if (FieldVariant.IsProperty())
		{
			FProperty* Property = FieldVariant.GetProperty();

			if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				if (ObjectProperty->PropertyClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
				{
					return ObjectProperty->PropertyClass;
				}
			}
		}

		return nullptr;
	}


	void BuildChildNodesRecursive(UClass* ViewModelClass, int32 RecursiveCount)
	{
		if (RecursiveCount <= 0)
		{
			return;
		}
		--RecursiveCount;

		ChildNodes.Reset();
		for (const FMVVMAvailableBinding& ChildBinding : Private::GetBindingsList(ViewModelClass))
		{
			TSharedRef<Private::FAvailableBindingNode> ChildNode = MakeShared<Private::FAvailableBindingNode>(Owner, ChildBinding, ViewModelClass);
			ChildNodes.Add(ChildNode);
			if (ChildNode->ViewModelClassProperty.Get() != nullptr)
			{
				ChildNode->BuildChildNodesRecursive(ChildNode->ViewModelClassProperty, RecursiveCount);
			}
		}
		ChildNodes.Sort(Private::FAvailableBindingNode::Sort);
		bChildGenerated = true;
	}
};

} //namespace Private


void SViewModelBindingListWidget::Construct(const FArguments& InArgs)
{
	OnRenamed = InArgs._OnRenamed;
	OnDeleted = InArgs._OnDeleted;
	OnDuplicated = InArgs._OnDuplicated;

	SearchFilter = MakeShared<ViewModelTextFilter>(ViewModelTextFilter::FItemToStringArray::CreateSP(this, &SViewModelBindingListWidget::HandleGetFilterStrings));

	FilterHandler = MakeShared<ViewModelTreeFilter>();
	FilterHandler->SetFilter(SearchFilter.Get());
	FilterHandler->SetRootItems(&TreeSource, &FilteredTreeSource);
	FilterHandler->SetGetChildrenDelegate(ViewModelTreeFilter::FOnGetChildren::CreateRaw(this, &SViewModelBindingListWidget::HandleGetChildren));

	SetViewModels(InArgs._ViewModels);
	CreateCommandList();

	SAssignNew(TreeWidget, STreeView<TSharedPtr<Private::FAvailableBindingNode>>)
		.ItemHeight(1.0f)
		.TreeItemsSource(&FilteredTreeSource)
		.SelectionMode(ESelectionMode::Single)
		.OnGetChildren(FilterHandler.ToSharedRef(), &ViewModelTreeFilter::OnGetFilteredChildren)
		.OnGenerateRow(this, &SViewModelBindingListWidget::HandleGenerateRow)
		//	.OnSelectionChanged(this, &SMVVMViewModelPanel::HandleSelectionChanged)
		.OnContextMenuOpening(this, &SViewModelBindingListWidget::HandleContextMenuOpening);

	FilterHandler->SetTreeView(TreeWidget.Get());

	ChildSlot
	[
		TreeWidget.ToSharedRef()
	];
}


void SViewModelBindingListWidget::SetViewModel(TSubclassOf<UObject> ViewModelClass, FName InViewModelName)
{
	FViewModel ViewModel;
	ViewModel.Class = ViewModelClass;
	ViewModel.Name = InViewModelName;

	SetViewModels(MakeArrayView(&ViewModel, 1));
}


void SViewModelBindingListWidget::SetViewModels(TArrayView<const FViewModel> ViewModels)
{
	TreeSource.Reset();
	for (const FViewModel& ViewModel : ViewModels)
	{
		if (ViewModel.Class && ViewModel.Class->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
		{
			TSharedRef<Private::FAvailableBindingNode> RootNode = MakeShared<Private::FAvailableBindingNode>(this, ViewModel);
			RootNode->BuildChildNodes();
			TreeSource.Add(RootNode);

			if (TreeWidget)
			{
				TreeWidget->SetItemExpansion(RootNode, true);
			}
		}
	}

	if (FilterHandler)
	{
		FilterHandler->RefreshAndFilterTree();
	}
}


FText SViewModelBindingListWidget::SetRawFilterText(const FText& InFilterText)
{
	const bool bNewFilteringEnabled = !InFilterText.IsEmpty();
	FilterHandler->SetIsEnabled(bNewFilteringEnabled);
	SearchFilter->SetRawFilterText(InFilterText);
	FilterHandler->RefreshAndFilterTree();
	return SearchFilter->GetFilterErrorText();
}


FText SViewModelBindingListWidget::GetRawFilterText() const
{
	return SearchFilter->GetRawFilterText();
}

FReply SViewModelBindingListWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}


void SViewModelBindingListWidget::HandleGetFilterStrings(TSharedPtr<Private::FAvailableBindingNode> Item, TArray<FString>& OutStrings)
{
	Item->GetFilterStrings(OutStrings);
}


TSharedRef<ITableRow> SViewModelBindingListWidget::HandleGenerateRow(TSharedPtr<Private::FAvailableBindingNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	using Private::FAvailableBindingNode;
	typedef STableRow<TSharedPtr<FAvailableBindingNode>> RowType;


	return SNew(RowType, OwnerTable)
		//.OnDragDetected(this, &SHierarchyViewItem::HandleDragDetected)
		//.OnDragEnter(this, &SHierarchyViewItem::HandleDragEnter)
		//.OnDragLeave(this, &SHierarchyViewItem::HandleDragLeave)
		.Padding(0.0f)
		.Content()
		[
			SNew(SHorizontalBox)
			.ToolTipText(Item->GetToolTip())
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				Item->CreateIconWidget()
			]
			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				Item->CreateDetailWidget(MakeAttributeSP(this, &SViewModelBindingListWidget::GetRawFilterText), OnRenamed)
			]
		];
}


void SViewModelBindingListWidget::HandleGetChildren(TSharedPtr<Private::FAvailableBindingNode> InParent, TArray<TSharedPtr<Private::FAvailableBindingNode>>& OutChildren)
{
	OutChildren = InParent->GetChildNodes();
}


void SViewModelBindingListWidget::CreateCommandList()
{
	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleDuplicateViewModel),
		FCanExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleCanDuplicateViewModel)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleDeleteViewModel),
		FCanExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleCanDeleteViewModel)
	);
	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleRenameViewModel),
		FCanExecuteAction::CreateSP(this, &SViewModelBindingListWidget::HandleCanRenameViewModel)
	);
}


TSharedPtr<SWidget> SViewModelBindingListWidget::HandleContextMenuOpening()
{
	TArray<TSharedPtr<Private::FAvailableBindingNode>> Items = TreeWidget->GetSelectedItems();
	if (Items.Num() == 1 && Items[0] && Items[0]->ShowContextMenu())
	{
		if (OnRenamed.IsBound() || OnDeleted.IsBound() || OnDuplicated.IsBound())
		{
			FMenuBuilder MenuBuilder(true, CommandList);

			MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
			{
				if (OnDuplicated.IsBound())
				{
					MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
				}
				if (OnDeleted.IsBound())
				{
					MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
				}
				if (OnRenamed.IsBound())
				{
					MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
				}
			}
			MenuBuilder.EndSection();

			return MenuBuilder.MakeWidget();
		}
	}
	return TSharedPtr<SWidget>();
}


void SViewModelBindingListWidget::HandleDuplicateViewModel()
{}


bool SViewModelBindingListWidget::HandleCanDuplicateViewModel()
{
	return true; //todo unless it's an autogen viewmodel
}


void SViewModelBindingListWidget::HandleDeleteViewModel()
{}


bool SViewModelBindingListWidget::HandleCanDeleteViewModel()
{
	return true; //todo unless it's an autogen viewmodel
}


void SViewModelBindingListWidget::HandleRenameViewModel()
{
	TArray<TSharedPtr<Private::FAvailableBindingNode>> Items = TreeWidget->GetSelectedItems();
	if (Items.Num() == 1 && Items[0])
	{
		Items[0]->EnterRename();
	}
}


bool SViewModelBindingListWidget::HandleCanRenameViewModel()
{
	TArray<TSharedPtr<Private::FAvailableBindingNode>> Items = TreeWidget->GetSelectedItems();
	return Items.Num() == 1 && Items[0] && Items[0]->CanRename();
}


} // namespace UE::MVVM

#undef LOCTEXT_NAMESPACE