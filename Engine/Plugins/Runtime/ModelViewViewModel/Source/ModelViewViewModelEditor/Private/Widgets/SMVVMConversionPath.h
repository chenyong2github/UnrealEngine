// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

struct FMVVMBlueprintViewBinding;
class SMenuAnchor;
class UWidgetBlueprint;

class SMVVMConversionPath : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnFunctionChanged, const FString&);

	SLATE_BEGIN_ARGS(SMVVMConversionPath)
		{
		}
		SLATE_ATTRIBUTE(TArray<FMVVMBlueprintViewBinding*>, Bindings) 
		SLATE_EVENT(FOnFunctionChanged, OnFunctionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint, bool bInIsGetter);

private:
	EVisibility IsFunctionVisible() const;
	FText GetFunctionToolTip() const;
	FSlateColor GetFunctionColor() const;
	void SetConversionFunction(const UFunction* Function);
	TSharedRef<SWidget> GetFunctionMenuContent();
	FReply OnButtonClicked() const;
	FString GetFunctionPath() const;

private:
	TAttribute<TArray<FMVVMBlueprintViewBinding*>> Bindings;
	FOnFunctionChanged OnFunctionChanged;
	TSharedPtr<SMenuAnchor> Anchor;
	const UWidgetBlueprint* WidgetBlueprint = nullptr;
	bool bIsGetter = false;
};