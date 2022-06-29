// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewModelContext.h"

FMVVMBlueprintViewModelContext::FMVVMBlueprintViewModelContext(TSubclassOf<UMVVMViewModelBase> InClass, FName InViewModelName)
	: ViewModelContextId(FGuid::NewGuid())
	, ViewModelClass(InClass)
	, ViewModelName(InViewModelName)
{

}


FText FMVVMBlueprintViewModelContext::GetDisplayName() const
{
	return FText::FromName(ViewModelName);
}
