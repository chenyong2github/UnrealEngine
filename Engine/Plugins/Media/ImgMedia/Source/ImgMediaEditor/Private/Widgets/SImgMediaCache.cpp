// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SImgMediaCache.h"
#include "DetailLayoutBuilder.h"
#include "IImgMediaModule.h"
#include "ImgMediaGlobalCache.h"
#include "SlateOptMacros.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ImgMediaCache"

SImgMediaCache::~SImgMediaCache()
{
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SImgMediaCache::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
			.Padding(0, 20, 0, 0)
			.AutoHeight()
		
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
		[
			SNew(STextBlock)
				.Text(LOCTEXT("GlobalCache", "Global Cache"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
			
		+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 5, 0, 0)
			[
				SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked(this, &SImgMediaCache::OnClearGlobalCacheClicked)
					.Text(LOCTEXT("ClearGlobalCache", "Clear Global Cache"))
					.ToolTipText(LOCTEXT("ClearGlobalCacheButtonToolTip", "Clear the global cache."))
			]
		
	];
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SImgMediaCache::OnClearGlobalCacheClicked()
{
	// Clear the cache.
	FImgMediaGlobalCache* GlobalCache = IImgMediaModule::GetGlobalCache();
	if (GlobalCache != nullptr)
	{
		GlobalCache->EmptyCache();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
