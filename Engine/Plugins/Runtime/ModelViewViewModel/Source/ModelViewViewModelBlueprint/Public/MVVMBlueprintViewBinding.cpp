// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "WidgetBlueprintExtension.h"


FString FMVVMBlueprintViewBinding::GetNameString(const UMVVMBlueprintView* View) const
{
	const FMVVMBlueprintViewModelContext* ViewModel = View->FindViewModel(ViewModelPath.GetViewModelId());
	TStringBuilder<512> BindingName;
	if (ViewModel != nullptr)
	{
		BindingName << ViewModel->GetViewModelName();
	}
	else
	{
		BindingName << TEXT("<none>");
	}
	BindingName << TEXT(".");
	BindingName << ViewModelPath.GetBasePropertyPath();

	if (BindingType == EMVVMBindingMode::TwoWay)
	{
		BindingName << TEXT(" <-> ");
	}
	else if (UE::MVVM::IsForwardBinding(BindingType))
	{
		BindingName << TEXT(" -> ");
	}
	else if (UE::MVVM::IsBackwardBinding(BindingType))
	{
		BindingName << TEXT(" <- ");
	}
	else
	{
		BindingName << TEXT(" ??? "); // shouldn't happen
	}

	if (View->GetOuterUMVVMWidgetBlueprintExtension_View()->GetWidgetBlueprint()->GetFName() == WidgetPath.GetWidgetName())
	{
		BindingName << WidgetPath.GetBasePropertyPath();
	}
	else
	{
		if (WidgetPath.GetWidgetName().IsNone())
		{
			BindingName << TEXT("<none>");
		}
		else
		{
			BindingName << WidgetPath.GetWidgetName();
		}
		BindingName << TEXT(".");
		BindingName << WidgetPath.GetBasePropertyPath();
	}

	return BindingName.ToString();
}
