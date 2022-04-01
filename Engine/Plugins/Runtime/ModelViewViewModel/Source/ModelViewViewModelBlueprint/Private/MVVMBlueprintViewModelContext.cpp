// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewModelContext.h"

FMVVMBlueprintViewModelContext::FMVVMBlueprintViewModelContext(TSubclassOf<UMVVMViewModelBase> InClass, FGuid InGuid) :
	ViewModelContextId(InGuid),
	ViewModelClass(InClass)
{

}

FText FMVVMBlueprintViewModelContext::GetDisplayName() const
{
	if (!OverrideDisplayName.IsEmpty())
	{
		return OverrideDisplayName;
	}
	if (ViewModelClass.Get())
	{
		return ViewModelClass->GetDisplayNameText();
	}
	return FText::GetEmpty();
}
