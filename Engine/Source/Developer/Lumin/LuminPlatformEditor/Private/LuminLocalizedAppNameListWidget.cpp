// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminLocalizedAppNameListWidget.h"
#include "LuminLocalizedAppNameWidget.h"
#include "SourceControlHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "SLuminLocalizedAppNameListWidget"

void SLuminLocalizedAppNameListWidget::Construct(const FArguments& Args)
{
	DetailLayoutBuilder = Args._DetailLayoutBuilder.Get();

	TArray<FString> CultureNames;
	FInternationalization::Get().GetCultureNames(CultureNames);
	DefaultCultureName = CultureNames[0];

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("AddLocalizedIconAsset", "Add a localized app name."))
				.ContentPadding(2.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				.OnClicked(this, &SLuminLocalizedAppNameListWidget::Add)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_AddToArray"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ToolTipText(LOCTEXT("RemoveAllLocalizedIconAsset", "Remove all localized app names."))
				.ContentPadding(2.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				.OnClicked(this, &SLuminLocalizedAppNameListWidget::RemoveAll)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("PropertyWindow.Button_EmptyArray"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(ListViewWidget, SListView<TSharedPtr<SLuminLocalizedAppNameWidget>>)
				.ItemHeight(24)
				.ListItemsSource(&Items)
				.OnGenerateRow(this, &SLuminLocalizedAppNameListWidget::OnGenerateRowForList)
			]
		]
	];

	Load();
}

FReply SLuminLocalizedAppNameListWidget::Add()
{
	FLocalizedAppName LocalizedAppName;
	LocalizedAppName.LanguageCode = DefaultCultureName;
	Items.Add(SNew(SLuminLocalizedAppNameWidget).LocalizedAppName(LocalizedAppName).ListWidget(this));
	ListViewWidget->RequestListRefresh();
	Save();
	return FReply::Handled();
}

FReply SLuminLocalizedAppNameListWidget::Remove(SLuminLocalizedAppNameWidget* InWidget)
{
	bool bRemoved = false;

	for (auto Widget : Items)
	{
		if (Widget.Get() == InWidget)
		{
			bRemoved = true;
			Items.Remove(Widget);
			break;
		}
	}

	if (bRemoved)
	{
		Save();
	}

	return FReply::Handled();
}

FReply SLuminLocalizedAppNameListWidget::RemoveAll()
{
	Items.Empty();
	ListViewWidget->RequestListRefresh();
	Save();
	return FReply::Handled();
}

void SLuminLocalizedAppNameListWidget::Save()
{
	checkf(LocalizedAppNamesProp.IsValid(), TEXT("Could not resolve LocalizedAppNamesProp!"));
	FString SaveString;

	if (Items.Num() > 0)
	{
		SaveString.Append(TEXT("("));
		for (int32 IconWidgetIndex = 0; IconWidgetIndex < Items.Num(); ++IconWidgetIndex)
		{
			FString LocalizedAppNameString;
			// don't save incomplete entries
			if (ItemToString(Items[IconWidgetIndex].Get()->GetLocalizedAppName(), LocalizedAppNameString))
			{
				SaveString += IconWidgetIndex < (Items.Num() - 1) ? FString::Printf(TEXT("%s,"), *LocalizedAppNameString) : *LocalizedAppNameString;
			}
		}

		SaveString.Append(TEXT(")"));
	}

	TArray<FString> Values;
	Values.Add(SaveString);
	LocalizedAppNamesProp->SetPerObjectValues(Values);
}

void SLuminLocalizedAppNameListWidget::Load()
{
	LocalizedAppNamesProp = DetailLayoutBuilder->GetProperty("LocalizedAppNames");
	FString LocalizedAppNamesString;
	FPropertyAccess::Result Result = LocalizedAppNamesProp->GetValueAsFormattedString(LocalizedAppNamesString);
	if (Result == FPropertyAccess::Result::Success && LocalizedAppNamesString.Len() > 0)
	{
		TArray<FString> LocalizedAppNameObjectStrings;
		// remove the first open bracket
		LocalizedAppNamesString.RemoveAt(0);
		LocalizedAppNamesString.ParseIntoArray(LocalizedAppNameObjectStrings, TEXT("("));

		for (auto LocalizedAppNameObjectString : LocalizedAppNameObjectStrings)
		{
			FLocalizedAppName LocalizedAppName;
			if (StringToItem(LocalizedAppNameObjectString, LocalizedAppName))
			{
				TSharedPtr<SLuminLocalizedAppNameWidget> LocalizedAppNameWidget = SNew(SLuminLocalizedAppNameWidget)
					.LocalizedAppName(LocalizedAppName)
					.ListWidget(this);

				Items.Add(LocalizedAppNameWidget);
			}
		}

		ListViewWidget->RequestListRefresh();
	}
}

bool SLuminLocalizedAppNameListWidget::ItemToString(const FLocalizedAppName& LocalizedAppName, FString& OutString)
{
	if (LocalizedAppName.LanguageCode.IsEmpty() || LocalizedAppName.AppName.IsEmpty())
	{
		return false;
	}

	OutString = TEXT("(");

	if (LocalizedAppName.LanguageCode.Len() > 0)
	{
		OutString.Append(FString::Printf(TEXT("LanguageCode=\"%s\","), *LocalizedAppName.LanguageCode));
	}

	if (LocalizedAppName.AppName.Len() > 0)
	{
		OutString.Append(FString::Printf(TEXT("AppName=\"%s\""), *LocalizedAppName.AppName));
	}

	OutString.Append(TEXT(")"));
	return true;
}

bool SLuminLocalizedAppNameListWidget::StringToItem(const FString& String, FLocalizedAppName& OutLocalizedAppName)
{
	TArray<FString> Fields;
	if (String.ParseIntoArray(Fields, TEXT("\"")) == 5)
	{
		OutLocalizedAppName.LanguageCode = Fields[1];
		OutLocalizedAppName.AppName = Fields[3];
		return true;
	}
	
	return false;
}

TSharedRef<ITableRow> SLuminLocalizedAppNameListWidget::OnGenerateRowForList(TSharedPtr< SLuminLocalizedAppNameWidget> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(STableRow< TSharedPtr<SLuminLocalizedAppNameWidget> >, OwnerTable)
		.Padding(2.0f)
		.IsEnabled(true)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				Item.ToSharedRef()
			]
		];
}

#undef LOCTEXT_NAMESPACE
