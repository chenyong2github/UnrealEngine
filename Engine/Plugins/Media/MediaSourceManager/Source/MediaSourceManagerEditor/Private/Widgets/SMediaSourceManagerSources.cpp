// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerSources.h"

#include "Inputs/MediaSourceManagerInput.h"
#include "MediaSourceManager.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SMediaSourceManagerChannel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerSources"

void SMediaSourceManagerSources::Construct(const FArguments& InArgs,
	UMediaSourceManager* InMediaSourceManager)
{
	MediaSourceManagerPtr = InMediaSourceManager;

	ChildSlot
		[
			SNew(SScrollBox)

			
			+ SScrollBox::Slot()
			[
				SAssignNew(ChannelsContainer, SVerticalBox)
			]

		];

	RefreshChannels();
}

void SMediaSourceManagerSources::SetManager(UMediaSourceManager* InManager)
{
	MediaSourceManagerPtr = InManager;
	RefreshChannels();
}

void SMediaSourceManagerSources::RefreshChannels()
{
	ChannelsContainer->ClearChildren();
	ChannelWidgets.Reset();
	UMediaSourceManager* MediaSourceManager = MediaSourceManagerPtr.Get();
	if (MediaSourceManager != nullptr)
	{
		int32 NumChannels = MediaSourceManager->Channels.Num();
		ChannelWidgets.SetNum(NumChannels);
		for (int32 Index = 0; Index < NumChannels; ++Index)
		{
			ChannelsContainer->AddSlot()
				.AutoHeight()
				.Padding(2)
				.VAlign(VAlign_Center)
				[
					SNew(SMediaSourceManagerChannel, MediaSourceManager->Channels[Index])
				];
		}
	}
}


#undef LOCTEXT_NAMESPACE
