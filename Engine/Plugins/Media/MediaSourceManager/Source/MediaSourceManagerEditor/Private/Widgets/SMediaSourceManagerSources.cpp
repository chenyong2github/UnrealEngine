// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerSources.h"
#include "MediaSourceManager.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerSources"

void SMediaSourceManagerSources::Construct(const FArguments& InArgs,
	UMediaSourceManager& InMediaSourceManager)
{
	MediaSourceManager = &InMediaSourceManager;

	ChildSlot
		[
			SNew(STextBlock)
				.Text(LOCTEXT("Sources", "Sources"))

		];
}

#undef LOCTEXT_NAMESPACE
