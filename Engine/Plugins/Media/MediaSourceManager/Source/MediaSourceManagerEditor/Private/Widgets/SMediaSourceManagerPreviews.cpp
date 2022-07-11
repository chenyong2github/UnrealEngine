// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreviews.h"

#include "MediaSourceManager.h"
#include "MediaSourceManagerEditorModule.h"
#include "MediaSourceManagerSettings.h"
#include "MediaTexture.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SMediaPlayerEditorViewer.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerPreviews"

void SMediaSourceManagerPreviews::Construct(const FArguments& InArg)
{
	ChildSlot
		[
			SAssignNew(ChannelsContainer, SHorizontalBox)
		];

	// Get style.
	IMediaSourceManagerEditorModule* Module = FModuleManager::GetModulePtr
		<IMediaSourceManagerEditorModule>("MediaSourceManagerEditor");
	if (Module != nullptr)
	{
		Style = Module->GetStyle();
	}

	SetManager();
	GetMutableDefault<UMediaSourceManagerSettings>()->OnManagerChanged.AddRaw(this,
		&SMediaSourceManagerPreviews::SetManager);
}

SMediaSourceManagerPreviews::~SMediaSourceManagerPreviews()
{
	UMediaSourceManagerSettings* Settings = GetMutableDefault<UMediaSourceManagerSettings>();
	if (Settings != nullptr)
	{
		Settings->OnManagerChanged.RemoveAll(this);
	}
}

void SMediaSourceManagerPreviews::SetManager()
{
	MediaSourceManagerPtr = (GetDefault<UMediaSourceManagerSettings>()->GetManager());
	RefreshChannels();
}

void SMediaSourceManagerPreviews::RefreshChannels()
{
	ChannelsContainer->ClearChildren();
	UMediaSourceManager* MediaSourceManager = MediaSourceManagerPtr.Get();
	if ((MediaSourceManager != nullptr) && (Style.IsValid()))
	{
		TSharedRef<ISlateStyle> StyleRef = Style.ToSharedRef();

		// Loop over each channel.
		int32 NumChannels = MediaSourceManager->Channels.Num();
		for (int32 Index = 0; Index < NumChannels; ++Index)
		{
			UMediaSourceManagerChannel* Channel = MediaSourceManager->Channels[Index];
			if (Channel != nullptr)
			{
				UMediaPlayer* MediaPlayer = Channel->GetMediaPlayer();
				UMediaTexture* MediaTexture = Cast<UMediaTexture>(Channel->OutTexture);

				// Add preview widget.
				ChannelsContainer->AddSlot()
					.Padding(2)
					.HAlign(HAlign_Center)
					[
						SNew(SMediaPlayerEditorViewer, *MediaPlayer, MediaTexture, StyleRef, false)
							.bShowUrl(false)
					];

				Channel->Play();
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
