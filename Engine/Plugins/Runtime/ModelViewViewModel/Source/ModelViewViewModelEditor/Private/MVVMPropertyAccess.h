// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IPropertyHandle;
class SWidget;
class UMVVMViewModelBase;
class UWidgetBlueprint;

/**
 *
 */
//class FMVVMPropertyAccess
//{
//public:
//	FMVVMPropertyAccess(UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<IPropertyHandle>& PropertyHandle);
//
//public:
//	TSharedPtr<SWidget> MakeViewModelBindingMenu() const;
//	static TSharedRef<SWidget> MakeFunctionWidget(UFunction* Function);
//	static TSharedRef<SWidget> MakePropertyWidget(FProperty* Property);
//	static bool IsBindableFunction(UFunction* Function);
//	static bool IsBindableProperty(FProperty* Property);
//
//private:
//	TSharedRef<SWidget> MakeViewModelSubMenuWidget(TSubclassOf<UMVVMViewModelBase> ViewModelDefinition) const;
//	void FillViewModelSubMenu(FMenuBuilder& MenuBuilder, TSubclassOf<UMVVMViewModelBase> ViewModelDefinition) const;
//	void HandleAddBinding(FGuid OutputId) const;
//
//private:
//	UWidgetBlueprint* WidgetBlueprint;
//	const TSharedPtr<IPropertyHandle>& PropertyHandle;
//};
