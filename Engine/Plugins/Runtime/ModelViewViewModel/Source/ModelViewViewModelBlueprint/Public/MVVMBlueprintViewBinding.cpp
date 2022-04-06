// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintView.h"

FString FMVVMBlueprintViewBinding::GetNameString(const UMVVMBlueprintView* View) const
{
	const FMVVMBlueprintViewModelContext* ViewModel = View->FindViewModel(ViewModelPath.ContextId);
	FString BindingName = (ViewModel != nullptr ? ViewModel->GetViewModelName().ToString() : TEXT("<none>")) + TEXT(".") + ViewModelPath.GetBindingName().ToString();

	BindingName += BindingType == EMVVMBindingMode::TwoWay ? TEXT(" <-> ") :
		UE::MVVM::IsForwardBinding(BindingType) ? TEXT(" -> ") :
		UE::MVVM::IsBackwardBinding(BindingType) ? TEXT(" <- ") :
		TEXT("???"); // shouldn't happen

	BindingName += WidgetPath.WidgetName.ToString() + TEXT(".") + WidgetPath.GetBindingName().ToString();

	return BindingName;
}