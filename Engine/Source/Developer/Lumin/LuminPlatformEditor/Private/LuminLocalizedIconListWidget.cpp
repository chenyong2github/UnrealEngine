// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminLocalizedIconListWidget.h"
#include "LuminLocalizedIconWidget.h"
#include "SourceControlHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "SLuminLocalizedIconListWidget"

void SLuminLocalizedIconListWidget::Construct(const FArguments& Args)
{
	DetailLayoutBuilder = Args._DetailLayoutBuilder.Get();
	GameLuminPath = Args._GameLuminPath.Get();

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
				.ToolTipText(LOCTEXT("AddLocalizedIconAsset", "Add a localized icon asset."))
				.ContentPadding(2.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				.OnClicked(this, &SLuminLocalizedIconListWidget::Add)
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
				.ToolTipText(LOCTEXT("RemoveAllLocalizedIconAsset", "Remove all localized icon assets."))
				.ContentPadding(2.0f)
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				.OnClicked(this, &SLuminLocalizedIconListWidget::RemoveAll)
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
				SAssignNew(ListViewWidget, SListView<TSharedPtr<SLuminLocalizedIconWidget>>)
				.ItemHeight(24)
				.ListItemsSource(&Items)
				.OnGenerateRow(this, &SLuminLocalizedIconListWidget::OnGenerateRowForList)
			]
		]
	];

	LoadIconData();
}

FReply SLuminLocalizedIconListWidget::Add()
{
	Items.Add(SNew(SLuminLocalizedIconWidget).ListWidget(this));
	ListViewWidget->RequestListRefresh();
	SaveIconData();
	return FReply::Handled();
}

FReply SLuminLocalizedIconListWidget::RemoveAll()
{
	Items.Empty();
	ListViewWidget->RequestListRefresh();
	SaveIconData();
	return FReply::Handled();
}

void SLuminLocalizedIconListWidget::RemoveIconWidget(SLuminLocalizedIconWidget* InIconWidget)
{
	bool bRemoved = false;

	for (auto IconWidget : Items)
	{
		if (IconWidget.Get() == InIconWidget)
		{
			bRemoved = true;
			Items.Remove(IconWidget);
			break;
		}
	}

	if (bRemoved)
	{
		SaveIconData();
	}
}

void SLuminLocalizedIconListWidget::SaveIconData()
{
	checkf(LocalizedIconInfosProp.IsValid(), TEXT("Could not resolve LocalizedIconInfos!"));
	FString SaveString = TEXT("(IconData=");

	if (Items.Num() > 0)
	{
		SaveString.Append(TEXT("("));
		for (int32 IconWidgetIndex = 0; IconWidgetIndex < Items.Num(); ++IconWidgetIndex)
		{
			FString IconInfoString = LocalizedIconInfoToString(Items[IconWidgetIndex].Get()->GetLocalizedIconInfo());
			SaveString += IconWidgetIndex < (Items.Num() - 1) ? FString::Printf(TEXT("%s,"), *IconInfoString) : *IconInfoString;
		}

		SaveString.Append(TEXT(")"));
	}

	SaveString.Append(TEXT(")"));
	TArray<FString> Values;
	Values.Add(SaveString);
	LocalizedIconInfosProp->SetPerObjectValues(Values);
}

void SLuminLocalizedIconListWidget::LoadIconData()
{
	LocalizedIconInfosProp = DetailLayoutBuilder->GetProperty("LocalizedIconInfos");
	TArray<void*> RawData;
	LocalizedIconInfosProp->AccessRawData(RawData);
	FLocalizedIconInfos* LocalizedIconInfos = reinterpret_cast<FLocalizedIconInfos*>(RawData[0]);

	if (LocalizedIconInfos != nullptr)
	{
		const TArray<FLocalizedIconInfo>& Icons = LocalizedIconInfos->IconData;
		for (auto IconInfo : Icons)
		{
			TSharedPtr<SLuminLocalizedIconWidget> IconWidget = SNew(SLuminLocalizedIconWidget)
				.GameLuminPath(GameLuminPath)
				.LocalizedIconInfo(IconInfo)
				.ListWidget(this);

			Items.Add(IconWidget);
		}

		ListViewWidget->RequestListRefresh();
	}
}

FString SLuminLocalizedIconListWidget::LocalizedIconInfoToString(const FLocalizedIconInfo& LocalizedIconInfo)
{
	FString String = TEXT("(");
	if (LocalizedIconInfo.LanguageCode.Len() > 0)
	{
		String.Append(FString::Printf(TEXT("LanguageCode=\"%s\""), *LocalizedIconInfo.LanguageCode));
	}

	if (LocalizedIconInfo.IconModelPath.Path.Len() > 0)
	{
		if (LocalizedIconInfo.LanguageCode.Len() > 0)
		{
			String.Append(TEXT(","));
		}
		String.Append(FString::Printf(TEXT("IconModelPath=(Path=\"%s\")"), *LocalizedIconInfo.IconModelPath.Path));
	}

	if (LocalizedIconInfo.IconPortalPath.Path.Len() > 0)
	{
		if (LocalizedIconInfo.IconModelPath.Path.Len() > 0)
		{
			String.Append(TEXT(","));
		}
		String.Append(FString::Printf(TEXT("IconPortalPath=(Path=\"%s\")"), *LocalizedIconInfo.IconPortalPath.Path));
	}

	String.Append(TEXT(")"));
	return String;
}

TSharedRef<ITableRow> SLuminLocalizedIconListWidget::OnGenerateRowForList(TSharedPtr<SLuminLocalizedIconWidget> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return
		SNew(STableRow< TSharedPtr<SLuminLocalizedIconWidget> >, OwnerTable)
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
