// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/ScreenReaderUser.h"
#include "Announcement/ScreenReaderAnnouncementChannel.h"
#include "TextToSpeech.h"

FScreenReaderUser::FScreenReaderUser(int32 InUserId)
	: UserId(InUserId)
	, bActive(false)
{
	// @TODOAccessibility: For now, just default it to the platform TTS. Should give a way to allow users to use custom TTS though 
	AnnouncementChannel = MakeUnique<FScreenReaderAnnouncementChannel>(ITextToSpeechModule::Get().GetPlatformFactory()->Create());
}

FScreenReaderUser::~FScreenReaderUser()
{
	Deactivate();
}

void FScreenReaderUser::Activate()
{
	if (!IsActive())
	{
		bActive = true;
		AnnouncementChannel->Activate();
	}
}

void FScreenReaderUser::Deactivate()
{
	if (IsActive())
	{
		bActive = false;
		AnnouncementChannel->Deactivate();
	}
}

FScreenReaderReply FScreenReaderUser::RequestSpeak(FScreenReaderAnnouncement InAnnouncement)
{
	if (IsActive())
	{
		return AnnouncementChannel->RequestSpeak(MoveTemp(InAnnouncement));
	}
	return FScreenReaderReply::Unhandled();
}

FScreenReaderReply FScreenReaderUser::StopSpeaking()
{
	if (IsActive())
	{
		return AnnouncementChannel->StopSpeaking();
	}
	return FScreenReaderReply::Unhandled();
}

bool FScreenReaderUser::IsSpeaking() const
{
	if (IsActive())
	{
		return AnnouncementChannel->IsSpeaking();
	}
	return false;
}

FScreenReaderReply FScreenReaderUser::RequestSpeakWidget(const TSharedRef<IAccessibleWidget>& InWidget)
{
	if (IsActive())
	{
		return AnnouncementChannel->RequestSpeakWidget(InWidget);
	}
	return FScreenReaderReply::Unhandled();
}