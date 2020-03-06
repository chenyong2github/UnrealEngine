// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Widgets/SCompoundWidget.h"
#include "LuminRuntimeSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SListView.h"

FUNC_DECLARE_DELEGATE(FOnPickLocale, void, const FString&);

class SLuminLocalePickerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLuminLocalePickerWidget)
		{
		}
		SLATE_ATTRIBUTE(FString, InitiallySelectedLocale)
		SLATE_ATTRIBUTE(FOnPickLocale, OnPickLocale)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	void OnSelectedLocaleChanged(TSharedPtr<FString> NewLocale, ESelectInfo::Type SelectInfo);

private:
	TSharedPtr< SListView< TSharedPtr<FString> > > ListViewWidget;
	TArray<TSharedPtr<FString>> Locales;
	FOnPickLocale OnPickLocale;
};
