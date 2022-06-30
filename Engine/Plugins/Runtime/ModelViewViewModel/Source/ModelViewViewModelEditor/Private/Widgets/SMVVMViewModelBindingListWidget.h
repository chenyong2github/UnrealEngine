// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/PropertyViewer/IFieldIterator.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UWidgetBlueprint;
namespace UE::PropertyViewer
{
class SPropertyViewer;
}

namespace UE::MVVM
{

/**
 * 
 */
class FFieldIterator_ViewModel : public UE::PropertyViewer::FFieldIterator_BlueprintVisible
{
public:
	FFieldIterator_ViewModel(const UWidgetBlueprint* WidgetBlueprint);
	virtual TArray<FFieldVariant> GetFields(const UStruct*) const override;

private:
	TWeakObjectPtr<const UWidgetBlueprint> WidgetBlueprint;
};


/** 
 * 
 */
class SViewModelBindingListWidget : public SCompoundWidget
{
public:
	struct FViewModel
	{
		TSubclassOf<UObject> Class;
		FGuid ViewModelId;
		FName Name;
	};
	SLATE_BEGIN_ARGS(SViewModelBindingListWidget) {}
		SLATE_ARGUMENT(FViewModel, ViewModel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* WidgetBlueprint);
	void SetViewModel(UClass* Class, FName Name, FGuid Guid);
	void SetViewModels(TArrayView<const FViewModel> ViewModels);

	void SetRawFilterText(const FText& InFilterText);

private:
	TUniquePtr<FFieldIterator_ViewModel> ViewModelFieldIterator;
	TArray<FViewModel> ViewModels;
	TSharedPtr<UE::PropertyViewer::SPropertyViewer> PropertyViewer;
};

} //namespace UE::MVVM