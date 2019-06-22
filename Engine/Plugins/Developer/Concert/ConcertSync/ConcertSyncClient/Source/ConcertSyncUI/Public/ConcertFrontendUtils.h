// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertActionDefinition.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "EditorFontGlyphs.h"
#include "ConcertFrontendStyle.h"

#define LOCTEXT_NAMESPACE "ConcertFrontendUtils"

namespace ConcertFrontendUtils
{
	static const FName ButtonIconSyle = TEXT("FontAwesome.10");
	static const float MinDesiredWidthForBtnAndIcon = 29.f;
	static const FName ButtonStyleNames[(int32)EConcertActionType::NUM] = {
		TEXT("FlatButton"),
		TEXT("FlatButton.Primary"),
		TEXT("FlatButton.Info"),
		TEXT("FlatButton.Success"),
		TEXT("FlatButton.Warning"),
		TEXT("FlatButton.Danger"),
	};

	FORCEINLINE bool ShowSessionConnectionUI()
	{
		return !(IS_PROGRAM);
	}

	inline TSharedRef<SWidget> CreateDisplayName(const TAttribute<FText>& InDisplayName)
	{
		return SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle("BoldFont"))
				.Text(InDisplayName)
			];
	}

	inline TSharedRef<SButton> CreateTextButton(const FConcertActionDefinition& InDef)
	{
		const FButtonStyle* ButtonStyle = &FEditorStyle::Get().GetWidgetStyle<FButtonStyle>(ButtonStyleNames[(int32)InDef.Type]);
		check(ButtonStyle);
		const float ButtonContentWidthPadding = 6.f;
		const float PaddingCompensation = (ButtonStyle->NormalPadding.Left + ButtonStyle->NormalPadding.Right + ButtonContentWidthPadding * 2);

		return SNew(SButton)
			.ToolTipText(InDef.ToolTipText)
			.ButtonStyle(ButtonStyle)
			.ForegroundColor(FLinearColor::White)
			.ContentPadding(FMargin(ButtonContentWidthPadding, 2.f))
			.IsEnabled(InDef.IsEnabled)
			.Visibility_Lambda([IsVisible = InDef.IsVisible]() { return IsVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked_Lambda([OnExecute = InDef.OnExecute]() { OnExecute.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(SBox)
				.MinDesiredWidth(MinDesiredWidthForBtnAndIcon - PaddingCompensation)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle(ButtonIconSyle))
					.Text(InDef.Text)
					.Justification(ETextJustify::Center)
				]
			];
	}

	inline TSharedRef<SButton> CreateIconButton(const FConcertActionDefinition& InDef)
	{
		const FButtonStyle* ButtonStyle = &FEditorStyle::Get().GetWidgetStyle<FButtonStyle>(ButtonStyleNames[(int32)InDef.Type]);
		check(ButtonStyle);

		return SNew(SButton)
			.ButtonStyle(ButtonStyle)
			.ForegroundColor(FSlateColor::UseForeground())
			.ToolTipText(InDef.ToolTipText)
			.ContentPadding(FMargin(0, 0))
			.IsEnabled(InDef.IsEnabled)
			.Visibility_Lambda([IsVisible = InDef.IsVisible]() { return IsVisible.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked_Lambda([OnExecute = InDef.OnExecute]() { OnExecute.ExecuteIfBound(); return FReply::Handled(); })
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(TAttribute<const FSlateBrush*>::Create([IconStyleAttr = InDef.IconStyle]() { return FConcertFrontendStyle::Get()->GetBrush(IconStyleAttr.Get()); }))
			];
	}

	inline void AppendButtons(TSharedRef<SHorizontalBox> InHorizBox, TArrayView<const FConcertActionDefinition> InDefs)
	{
		for (const FConcertActionDefinition& Def : InDefs)
		{
			InHorizBox->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(1.0f))
				[
					Def.IconStyle.IsSet() ? CreateIconButton(Def) : CreateTextButton(Def)
				];
		}
	}

	template <typename ItemType, typename PredFactoryType>
	inline void SyncArraysByPredicate(TArray<TSharedPtr<ItemType>>& InOutArray, TArray<TSharedPtr<ItemType>>&& InNewArray, const PredFactoryType& InPredFactory)
	{
		if (InOutArray.Num() == 0)
		{
			// Empty array - can just move
			InOutArray = MoveTempIfPossible(InNewArray);
		}
		else
		{
			// Add or update the existing entries
			for (TSharedPtr<ItemType>& NewItem : InNewArray)
			{
				TSharedPtr<ItemType>* ExistingItemPtr = InOutArray.FindByPredicate(InPredFactory(NewItem));
				if (ExistingItemPtr)
				{
					**ExistingItemPtr = *NewItem;
				}
				else
				{
					InOutArray.Add(NewItem);
				}
			}
			// Remove entries that are no longer needed
			for (auto ExistingItemIt = InOutArray.CreateIterator(); ExistingItemIt; ++ExistingItemIt)
			{
				TSharedPtr<ItemType>* NewItemPtr = InNewArray.FindByPredicate(InPredFactory(*ExistingItemIt));
				if (!NewItemPtr)
				{
					ExistingItemIt.RemoveCurrent();
					continue;
				}
			}
		}
	}

	template <typename ItemType>
	inline TArray<TSharedPtr<ItemType>> DeepCopyArray(const TArray<TSharedPtr<ItemType>>& InArray)
	{
		TArray<TSharedPtr<ItemType>> ArrayCopy;
		{
			ArrayCopy.Reserve(InArray.Num());
			for (const TSharedPtr<ItemType>& Item : InArray)
			{
				ArrayCopy.Add(MakeShared<ItemType>(*Item));
			}
		}
		return ArrayCopy;
	}

	template <typename ItemType>
	inline TArray<TSharedPtr<ItemType>> DeepCopyArrayAndClearSource(TArray<TSharedPtr<ItemType>>& InOutArray)
	{
		TArray<TSharedPtr<ItemType>> ArrayCopy = DeepCopyArray(InOutArray);
		InOutArray.Reset();
		return ArrayCopy;
	}

	/** Returns the image used to render the expandable area title bar with respect to its hover/expand state. */
	inline const FSlateBrush* GetExpandableAreaBorderImage(const SExpandableArea& Area)
	{
		if (Area.IsTitleHovered())
		{
			return Area.IsExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory_Hovered");
		}
		return Area.IsExpanded() ? FEditorStyle::GetBrush("DetailsView.CategoryTop") : FEditorStyle::GetBrush("DetailsView.CollapsedCategory");
	}

	static FText FormatRelativeTime(const FDateTime& EventTime, const FDateTime* CurrTime = nullptr)
	{
		FTimespan TimeSpan = (CurrTime ? *CurrTime : FDateTime::UtcNow()) - EventTime;
		int32 Days = TimeSpan.GetDays();
		int32 Hours = TimeSpan.GetHours();

		if (Days >= 1)
		{
			return Hours > 0 ?
				FText::Format(LOCTEXT("DaysHours", "{0} {0}|plural(one=Day,other=Days), {1} {1}|plural(one=Hour,other=Hours) Ago"), Days, Hours) :
				FText::Format(LOCTEXT("Days", "{0} {0}|plural(one=Day,other=Days) Ago"), Days);
		}

		int32 Minutes = TimeSpan.GetMinutes();
		if (Hours >= 1)
		{
			return Minutes > 0 ?
				FText::Format(LOCTEXT("HoursMins", "{0} {0}|plural(one=Hour,other=Hours), {1} {1}|plural(one=Minute,other=Minutes) Ago"), Hours, Minutes) :
				FText::Format(LOCTEXT("Hours", "{0} {0}|plural(one=Hour,other=Hours) Ago"), Hours);
		}

		int32 Seconds = TimeSpan.GetSeconds();
		if (Minutes >= 1)
		{
			return Seconds > 0 ?
				FText::Format(LOCTEXT("MinsSecs", "{0} {0}|plural(one=Minute,other=Minutes), {1} {1}|plural(one=Second,other=Seconds) Ago"), Minutes, Seconds) :
				FText::Format(LOCTEXT("Mins", "{0} {0}|plural(one=Minute,other=Minutes) Ago"), Minutes, Seconds);
		}

		if (Seconds >= 1)
		{
			return FText::Format(LOCTEXT("Secs", "{0} {0}|plural(one=Second,other=Seconds) Ago"), Seconds);
		}
		return LOCTEXT("Now", "Now");
	}
};

#undef LOCTEXT_NAMESPACE
