// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

enum class ECheckBoxState : uint8;
class IPropertyHandle;
class UWidget;
class UWidgetBlueprint;
struct FOnGenerateGlobalRowExtensionArgs;
struct FPropertyRowExtensionButton;

class FMVVMPropertyAccess;

class FMVVMBindPropertiesDetailView
{
public:
	static bool IsSupported(const FOnGenerateGlobalRowExtensionArgs& InArgs);
	void CreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions);

private:
	struct FBinding
	{
		TArray<TWeakObjectPtr<UWidget>> Widgets;
		TWeakObjectPtr<UWidgetBlueprint> WidgetBlueprint;
		TWeakPtr<IPropertyHandle> PropertyHandle;
	};

	bool GetBindingInfo(const TSharedPtr<IPropertyHandle>& Handle, FBinding& Out);
	//TSharedRef<SWidget> HandleExecutePropertyExposed(FBinding);
	ECheckBoxState GetPropertyExposedCheckState(FBinding) const;

private:
	TSharedPtr<FMVVMPropertyAccess> Poppup;
};