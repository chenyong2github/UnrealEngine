// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminLocalizedAppNameWidget.h"
#include "LuminLocalizedAppNameListWidget.h"
#include "LuminLocalePickerWidget.h"
#include "EditorStyleSet.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SLuminLocalizedAppNameWidget"

void SLuminLocalizedAppNameWidget::Construct(const FArguments& Args)
{
	LocalizedAppName = Args._LocalizedAppName.Get();
	ListWidget = Args._ListWidget.Get();

	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBorder)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SNew(SLuminLocalePickerWidget)
					.ToolTipText(LOCTEXT("LocalizedAppNameLocaleTooltip", "Select the country code for this localized app name."))
					.InitiallySelectedLocale(LocalizedAppName.LanguageCode)
					.OnPickLocale(FOnPickLocale::CreateRaw(this, &SLuminLocalizedAppNameWidget::OnPickLocale))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				[
					SNew(SEditableTextBox)
					.ToolTipText(LOCTEXT("LocalizedAppNameTooltip", "Enter the application name for this locale."))
					.Text(FText::FromString(LocalizedAppName.AppName))
					.OnTextChanged(FOnTextChanged::CreateRaw(this, &SLuminLocalizedAppNameWidget::OnEditAppName))
				]
			]
		]

		+ SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Fill)
		.HAlign(EHorizontalAlignment::HAlign_Fill)
		.Padding(2.0f, 0.0f)
		[
			SNew(SBorder)
			.ToolTipText(LOCTEXT("RemoveLocalizedAppName", "Remove this localized app name."))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.VAlign(EVerticalAlignment::VAlign_Center)
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.Padding(2)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ContentPadding(2.0f)
					.ForegroundColor(FSlateColor::UseForeground())
					.IsFocusable(false)
					.OnClicked(this, &SLuminLocalizedAppNameWidget::OnRemove)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("PropertyWindow.Button_EmptyArray"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		]
	];
}

const FLocalizedAppName& SLuminLocalizedAppNameWidget::GetLocalizedAppName() const
{
	return LocalizedAppName;
}

void SLuminLocalizedAppNameWidget::OnPickLocale(const FString& Locale)
{
	LocalizedAppName.LanguageCode = Locale;
	ListWidget->Save();
}

void SLuminLocalizedAppNameWidget::OnEditAppName(const FText& NewAppName)
{
	LocalizedAppName.AppName = NewAppName.ToString();
	ListWidget->Save();
}

FReply SLuminLocalizedAppNameWidget::OnRemove()
{
	ListWidget->Remove(this);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
