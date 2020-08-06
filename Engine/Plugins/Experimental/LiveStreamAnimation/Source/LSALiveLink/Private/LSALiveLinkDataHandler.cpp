// Copyright Epic Games, Inc.All Rights Reserverd.

#include "LSALiveLinkDataHandler.h"
#include "LSALiveLinkSourceOptions.h"
#include "LSALiveLinkStreamingHelper.h"
#include "LSALiveLinkLog.h"
#include "LSALiveLinkSkelMeshSource.h"

#include "LiveStreamAnimationRole.h"
#include "LiveStreamAnimationHandle.h"

void ULSALiveLinkDataHandler::OnStartup()
{
	LiveLinkStreamingHelper = MakeShared<FLSALiveLinkStreamingHelper>(*this);
}

void ULSALiveLinkDataHandler::OnShutdown()
{

	LiveLinkStreamingHelper.Reset();
}

void ULSALiveLinkDataHandler::OnPacketReceived(const TArrayView<const uint8> ReceivedPacket)
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return;
	}

	Helper->OnPacketReceived(ReceivedPacket);
}

void ULSALiveLinkDataHandler::OnAnimationRoleChanged(const ELiveStreamAnimationRole NewRole)
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return;
	}

	Helper->OnAnimationRoleChanged(NewRole);
}

void ULSALiveLinkDataHandler::GetJoinInProgressPackets(TArray<TArray<uint8>>& OutPackets)
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return;
	}

	Helper->GetJoinInProgressPackets(OutPackets);
}

bool ULSALiveLinkDataHandler::StartTrackingLiveLinkSubject(
	const FName LiveLinkSubject,
	const FLiveStreamAnimationHandleWrapper RegisteredName,
	const FLSALiveLinkSourceOptions Options,
	const FLiveStreamAnimationHandleWrapper TranslationProfile)
{
	return StartTrackingLiveLinkSubject(
		LiveLinkSubject,
		FLiveStreamAnimationHandle(RegisteredName),
		Options,
		FLiveStreamAnimationHandle(TranslationProfile)
		);
}

bool ULSALiveLinkDataHandler::StartTrackingLiveLinkSubject(
	const FName LiveLinkSubject,
	const FLiveStreamAnimationHandle RegisteredName,
	const FLSALiveLinkSourceOptions Options,
	const FLiveStreamAnimationHandle TranslationProfile)
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return false;
	}

	const ELiveStreamAnimationRole CurrentRole = GetRole();
	if (ELiveStreamAnimationRole::Tracker != CurrentRole)
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("ULSALiveLinkDataHandler::StartTrackingLiveLinkSubject: Invalid role. %d"),
			static_cast<int32>(CurrentRole));

		return false;
	}

	return Helper->StartTrackingLiveLinkSubject(
		LiveLinkSubject,
		RegisteredName,
		Options,
		TranslationProfile);
}

void ULSALiveLinkDataHandler::StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandleWrapper RegisteredName)
{
	StopTrackingLiveLinkSubject(FLiveStreamAnimationHandle(RegisteredName));
}

void ULSALiveLinkDataHandler::StopTrackingLiveLinkSubject(const FLiveStreamAnimationHandle RegisteredName)
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return;
	}

	const ELiveStreamAnimationRole CurrentRole = GetRole();
	if (ELiveStreamAnimationRole::Tracker != CurrentRole)
	{
		UE_LOG(LogLSALiveLink, Warning, TEXT("ULiveStreamAnimationSubsystem::StartTrackingLiveLinkSubject: Invalid role. %d"),
			static_cast<int32>(CurrentRole));

		return;
	}

	Helper->StopTrackingLiveLinkSubject(FLiveStreamAnimationHandle(RegisteredName));
}

TSharedPtr<const class FLSALiveLinkSkelMeshSource> ULSALiveLinkDataHandler::GetOrCreateLiveLinkSkelMeshSource()
{
	FLSALiveLinkStreamingHelper* Helper = LiveLinkStreamingHelper.Get();
	if (!Helper)
	{
		return nullptr;
	}

	return Helper->GetOrCreateLiveLinkSkelMeshSource();
}
