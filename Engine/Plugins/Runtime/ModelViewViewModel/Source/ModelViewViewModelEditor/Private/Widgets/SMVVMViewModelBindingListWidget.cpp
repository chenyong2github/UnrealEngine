// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMVVMViewModelBindingListWidget.h"

#include "Bindings/MVVMBindingHelper.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "Styling/SlateIconFinder.h"
#include "Textures/SlateIcon.h"
#include "Types/MVVMAvailableBinding.h"
#include "Types/MVVMFieldVariant.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SMVVMFieldIcon.h"
#include "Widgets/Views/STreeView.h"

namespace UE::MVVM::Private
{
	class FMVVMAvailableBindingNode
	{
	public:
		FMVVMAvailableBindingNode(const FMVVMAvailableBinding& InBinding, TSubclassOf<UMVVMViewModelBase> InClass)
			:
			Binding(InBinding),
			Class(InClass),
			bIsRoot(false)
		{}

		FMVVMAvailableBindingNode(TSubclassOf<UMVVMViewModelBase> InClass)
			:
			Class(InClass),
			bIsRoot(true)
		{}

		const FMVVMAvailableBinding Binding;
		const TSubclassOf<UMVVMViewModelBase> Class;
		const bool bIsRoot = false;

		TArray<TSharedPtr<FMVVMAvailableBindingNode>> ChildNodes;

		TSubclassOf<UMVVMViewModelBase> GetViewModelClassProperty()
		{
			if (bIsRoot)
			{
				return nullptr;
			}

			UE::MVVM::FMVVMFieldVariant FieldVariant = UE::MVVM::BindingHelper::FindFieldByName(Class, Binding.GetBindingName());
			if (FieldVariant.IsProperty())
			{
				FProperty* Property = FieldVariant.GetProperty();

				if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
				{
					if (ObjectProperty->PropertyClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
					{
						return ObjectProperty->PropertyClass;
					}
				}
			}
			
			return nullptr;
		}

		FText GetDisplayName()
		{
			if (bIsRoot)
			{
				return Class->GetDisplayNameText();
			}

			return FText::FromString(Binding.GetBindingName().ToString());
		}

		FText GetToolTip()
		{
			if (bIsRoot)
			{
				return FText::GetEmpty();
			}

			UE::MVVM::FMVVMFieldVariant FieldVariant = UE::MVVM::BindingHelper::FindFieldByName(Class, Binding.GetBindingName());
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

		TSharedRef<SWidget> GetIcon()
		{
			if (bIsRoot)
			{
				return SNew(SImage).Image(FSlateIconFinder::FindIconBrushForClass(Class));
			}

			UE::MVVM::FMVVMFieldVariant FieldVariant = UE::MVVM::BindingHelper::FindFieldByName(Class, Binding.GetBindingName());
			
			return SNew(SMVVMFieldIcon)
				.Field(FieldVariant);
		}

		static bool Sort(const TSharedPtr<FMVVMAvailableBindingNode>& Node, const TSharedPtr<FMVVMAvailableBindingNode>& OtherNode)
		{
			TSubclassOf<UMVVMViewModelBase> NodeClass = Node->GetViewModelClassProperty();
			TSubclassOf<UMVVMViewModelBase> OtherNodeClass = OtherNode->GetViewModelClassProperty();
			const FString& NodeStr = Node->Binding.GetBindingName().ToString();
			const FString& OtherNodeStr = OtherNode->Binding.GetBindingName().ToString();
			if (NodeClass && OtherNodeClass)
			{
				return NodeStr < OtherNodeStr;
			}

			if (NodeClass)
			{
				return true;
			}

			if (OtherNodeClass)
			{
				return false;
			}

			return NodeStr < OtherNodeStr;
		}
	};

	static TArray<FMVVMAvailableBinding> GetBindingsList(TSubclassOf<UMVVMViewModelBase> Class)
	{
		TArray<FMVVMAvailableBinding> ViewModelAvailableBindingsList = GEngine->GetEngineSubsystem<UMVVMSubsystem>()->GetViewModelAvailableBindings(Class);
		return ViewModelAvailableBindingsList;
	}
} //namespace UE::MVVM::Private

void SMVVMViewModelBindingListWidget::Construct(const FArguments& InArgs)
{
	using UE::MVVM::Private::FMVVMAvailableBindingNode;
	ChildSlot
	[
		SAssignNew(TreeWidget, STreeView<TSharedPtr<FMVVMAvailableBindingNode>>)
		.TreeItemsSource(&TreeSource)
		.OnGetChildren(this, &SMVVMViewModelBindingListWidget::HandleGetChildren)
		.OnGenerateRow(this, &SMVVMViewModelBindingListWidget::HandleGenerateRow)
		.ItemHeight(20.0f)
	];
}

void SMVVMViewModelBindingListWidget::SetViewModel(TSubclassOf<UMVVMViewModelBase> ViewModelClass)
{
	using UE::MVVM::Private::FMVVMAvailableBindingNode;
	TreeSource.Empty();
	if (ViewModelClass)
	{
		TSharedRef<FMVVMAvailableBindingNode> RootNode = MakeShared<FMVVMAvailableBindingNode>(ViewModelClass);
		TArray<FMVVMAvailableBinding> ViewModelAvailableBindingsList = UE::MVVM::Private::GetBindingsList(ViewModelClass);
		for (const FMVVMAvailableBinding& Binding : ViewModelAvailableBindingsList)
		{
			RootNode->ChildNodes.Add(MakeShared<FMVVMAvailableBindingNode>(Binding, ViewModelClass));
		}
		RootNode->ChildNodes.Sort(FMVVMAvailableBindingNode::Sort);
		TreeSource.Add(RootNode);
		TreeWidget->SetItemExpansion(RootNode, true);
	}
	TreeWidget->RequestListRefresh();
}

TSharedRef<ITableRow> SMVVMViewModelBindingListWidget::HandleGenerateRow(TSharedPtr<FMVVMAvailableBindingNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	using UE::MVVM::Private::FMVVMAvailableBindingNode;
	typedef STableRow<TSharedPtr<FMVVMAvailableBindingNode>> RowType;

	TSharedRef<RowType> NewRow = SNew(RowType, OwnerTable);


	NewRow->SetContent(
		SNew(SHorizontalBox)
			.ToolTipText(Item->GetToolTip())
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				Item->GetIcon()
			]
			+ SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Visibility(EVisibility::Visible)
				.Text(Item->GetDisplayName())
			]
	);

	return NewRow;
}

void SMVVMViewModelBindingListWidget::HandleGetChildren(TSharedPtr<FMVVMAvailableBindingNode> InParent, TArray<TSharedPtr<FMVVMAvailableBindingNode>>& OutChildren)
{
	using UE::MVVM::Private::FMVVMAvailableBindingNode;

	if (!InParent->ChildNodes.IsEmpty())
	{
		OutChildren = InParent->ChildNodes;
		return;
	}

	UE::MVVM::FMVVMFieldVariant FieldVariant = UE::MVVM::BindingHelper::FindFieldByName(InParent->Class, InParent->Binding.GetBindingName());

	if (FieldVariant.IsProperty())
	{
		FProperty* Property = FieldVariant.GetProperty();

		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			if (ObjectProperty->PropertyClass->IsChildOf(UMVVMViewModelBase::StaticClass()))
			{
				if (InParent->ChildNodes.IsEmpty())
				{
					TSubclassOf<UMVVMViewModelBase> ChildClass = ObjectProperty->PropertyClass;
					TArray<FMVVMAvailableBinding> ViewModelAvailableBindingsList = UE::MVVM::Private::GetBindingsList(ChildClass);
					for (const FMVVMAvailableBinding& Binding : ViewModelAvailableBindingsList)
					{
						TSharedPtr<FMVVMAvailableBindingNode> ChildNode = MakeShared<FMVVMAvailableBindingNode>(Binding, ChildClass);
						InParent->ChildNodes.Add(ChildNode);
					}
					InParent->ChildNodes.Sort(FMVVMAvailableBindingNode::Sort);
				}
				OutChildren.Append(InParent->ChildNodes);
			}
		}
	}
}

TSharedRef<SWidget> SMVVMViewModelBindingListWidget::GetFieldIcon(const UE::MVVM::FMVVMFieldVariant& FieldVariant)
{
	return SNew(SMVVMFieldIcon)
		.Field(FieldVariant);
}
