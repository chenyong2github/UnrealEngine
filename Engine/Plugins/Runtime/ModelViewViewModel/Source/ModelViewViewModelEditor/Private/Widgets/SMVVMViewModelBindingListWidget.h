// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"
#include "Misc/IFilter.h"
#include "Misc/TextFilter.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Templates/SubclassOf.h"
#include "Types/MVVMFieldVariant.h"
#include "Widgets/SCompoundWidget.h"

struct FMVVMAvailableBinding;
class FUICommandList;
class ITableRow;
template<typename T>
class STreeView;
class STableViewBase;
class SInlineEditableTextBlock;
class UMVVMViewModelBase;


namespace UE::MVVM
{
namespace Private
{
class FAvailableBindingNode;
}//namespace Private


class SViewModelBindingListWidget : public SCompoundWidget
{
	friend Private::FAvailableBindingNode;
public:
	struct FViewModel
	{
		TSubclassOf<UObject> Class;
		FGuid ViewModelId;
		FName Name;
	};

	DECLARE_DELEGATE_RetVal_FourParams(bool, FOnViewModelRenamed, const FViewModel&, const FText&, bool, FText&);
	DECLARE_DELEGATE_OneParam(FOnViewModelDeleted, const FViewModel&);
	DECLARE_DELEGATE_RetVal_OneParam(FViewModel, FOnViewModelDuplicated, const FViewModel&);

	SLATE_BEGIN_ARGS(SViewModelBindingListWidget) {}
		SLATE_ARGUMENT(TArrayView<const FViewModel>, ViewModels)
		SLATE_EVENT(FOnViewModelRenamed, OnRenamed)
		SLATE_EVENT(FOnViewModelDeleted, OnDeleted)
		SLATE_EVENT(FOnViewModelDuplicated, OnDuplicated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetViewModel(TSubclassOf<UObject> ViewModelClass, FName ViewModelName);
	void SetViewModels(TArrayView<const FViewModel> ViewModels);
	FText SetRawFilterText(const FText& InFilterText);

private:
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	FText GetRawFilterText() const;

	void HandleGetFilterStrings(TSharedPtr<Private::FAvailableBindingNode> Item, TArray<FString>& OutStrings);
	TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<Private::FAvailableBindingNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildren(TSharedPtr<Private::FAvailableBindingNode> InParent, TArray<TSharedPtr<Private::FAvailableBindingNode>>& OutChildren);

	TSharedPtr<SWidget> HandleContextMenuOpening();
	void HandleDuplicateViewModel();
	bool HandleCanDuplicateViewModel();
	void HandleDeleteViewModel();
	bool HandleCanDeleteViewModel();
	void HandleRenameViewModel();
	bool HandleCanRenameViewModel();

	void CreateCommandList();

	TSharedPtr<STreeView<TSharedPtr<Private::FAvailableBindingNode>>> TreeWidget;
	TArray<TSharedPtr<Private::FAvailableBindingNode>> TreeSource;
	TArray<TSharedPtr<Private::FAvailableBindingNode>> FilteredTreeSource;

	TSharedPtr<FUICommandList> CommandList;
	typedef TTextFilter<TSharedPtr<Private::FAvailableBindingNode>> ViewModelTextFilter;
	TSharedPtr<ViewModelTextFilter> SearchFilter;
	typedef TreeFilterHandler<TSharedPtr<Private::FAvailableBindingNode>> ViewModelTreeFilter;
	TSharedPtr<ViewModelTreeFilter> FilterHandler;

	FOnViewModelRenamed OnRenamed;
	FOnViewModelDeleted OnDeleted;
	FOnViewModelDuplicated OnDuplicated;
};

} //namespace UE::MVVM