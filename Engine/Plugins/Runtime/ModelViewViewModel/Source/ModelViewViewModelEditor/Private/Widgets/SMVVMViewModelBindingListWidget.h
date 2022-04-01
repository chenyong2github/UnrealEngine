// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SCompoundWidget.h"

struct FMVVMAvailableBinding;

template<typename T>
class STreeView;
class ITableRow;
class STableViewBase;
class UMVVMViewModelBase;
namespace UE::MVVM
{
struct FMVVMFieldVariant;
namespace Private
{
	class FMVVMAvailableBindingNode;
}
}


class SMVVMViewModelBindingListWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMVVMViewModelBindingListWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetViewModel(TSubclassOf<UMVVMViewModelBase> ViewModelClass);

private:
	typedef UE::MVVM::Private::FMVVMAvailableBindingNode FMVVMAvailableBindingNode;

	TSharedRef<ITableRow> HandleGenerateRow(TSharedPtr<FMVVMAvailableBindingNode> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildren(TSharedPtr<FMVVMAvailableBindingNode> InParent, TArray<TSharedPtr<FMVVMAvailableBindingNode>>& OutChildren);

	TSharedRef<SWidget> GetFieldIcon(const UE::MVVM::FMVVMFieldVariant& FieldVariant);

	TSharedPtr<STreeView<TSharedPtr<FMVVMAvailableBindingNode>>> TreeWidget;
	TArray<TSharedPtr<FMVVMAvailableBindingNode>> TreeSource;

	TSubclassOf<UMVVMViewModelBase> CurrentViewModel;
};