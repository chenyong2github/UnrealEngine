// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVMPropertyPath.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SMVVMCachedViewBindingPropertyPath.h"
#include "Widgets/SMVVMCachedViewBindingConversionFunction.h"
#include "Widgets/SMVVMFieldSelectorMenu.h"

namespace UE::MVVM { class SCachedViewBindingPropertyPath; }

class SComboButton;

namespace UE::MVVM
{

class SFieldSelector : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(FFieldSelectionContext, FOnGetSelectionContext);

	SLATE_BEGIN_ARGS(SFieldSelector)
		: _TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		{
		}
		SLATE_STYLE_ARGUMENT(FTextBlockStyle, TextStyle)
		SLATE_ARGUMENT_DEFAULT(bool, ShowContext) = true;
		SLATE_EVENT(FOnGetPropertyPath, OnGetPropertyPath)
		SLATE_EVENT(FOnGetConversionFunction, OnGetConversionFunction)
		SLATE_EVENT(FOnFieldSelectionChanged, OnFieldSelectionChanged)
		SLATE_EVENT(FOnGetSelectionContext, OnGetSelectionContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UWidgetBlueprint* InWidgetBlueprint);

private:
	int32 GetCurrentDisplayIndex() const;

	TSharedRef<SWidget> CreateSourcePanel();
	TSharedRef<SWidget> HandleGetMenuContent();

	void HandleFieldSelectionChanged(FMVVMBlueprintPropertyPath, const UFunction*);
	void HandleMenuClosed();

private:
	TSharedPtr<SCachedViewBindingPropertyPath> PropertyPathWidget;
	TSharedPtr<SComboButton> ComboButton;

	TWeakObjectPtr<const UWidgetBlueprint> WidgetBlueprint;
	const FTextBlockStyle* TextStyle = nullptr;
	FOnGetPropertyPath OnGetPropertyPath;
	FOnGetConversionFunction OnGetConversionFunction;
	FOnGetSelectionContext OnGetSelectionContext;
	FOnFieldSelectionChanged OnFieldSelectionChanged;
}; 

} // namespace UE::MVVM
