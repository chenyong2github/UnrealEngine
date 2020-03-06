// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminLocalePickerWidget.h"
#include "Widgets/Input/STextComboBox.h"

void SLuminLocalePickerWidget::Construct(const FArguments& Args)
{
	OnPickLocale = Args._OnPickLocale.Get();

	TArray<FString> CultureNames;
	FInternationalization::Get().GetCultureNames(CultureNames);

	for (FString& Locale : CultureNames)
	{
		Locales.Add(MakeShareable(new FString(Locale)));
	}

	int32 CurrLocaleIndex = 0;
	FString InitiallySelectedLocale = Args._InitiallySelectedLocale.Get();
	for (int32 LocaleIndex = 0; LocaleIndex < Locales.Num(); ++LocaleIndex)
	{
		if (*Locales[LocaleIndex].Get() == InitiallySelectedLocale)
		{
			CurrLocaleIndex = LocaleIndex;
			break;
		}
	}

	ChildSlot
	[
		SNew(STextComboBox)
		.OptionsSource(&Locales)
		.InitiallySelectedItem(Locales[CurrLocaleIndex])
		.OnSelectionChanged(this, &SLuminLocalePickerWidget::OnSelectedLocaleChanged)
	];
}

void SLuminLocalePickerWidget::OnSelectedLocaleChanged(TSharedPtr<FString> NewLocale, ESelectInfo::Type SelectInfo)
{
	OnPickLocale.Execute(*NewLocale.Get());
}
