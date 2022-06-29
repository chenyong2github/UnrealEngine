// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewModelContext.h"

#include "FieldNotification/IFieldValueChanged.h"

FMVVMBlueprintViewModelContext::FMVVMBlueprintViewModelContext(const UClass* InClass, FName InViewModelName)
{
	if (InClass && InClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
	{
		ViewModelContextId = FGuid::NewGuid();
		NotifyFieldValueClass = const_cast<UClass*>(InClass);
		ViewModelName = InViewModelName;
	}
}


FText FMVVMBlueprintViewModelContext::GetDisplayName() const
{
	return FText::FromName(ViewModelName);
}
