// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapConnectionsFunctionLibrary.h"
#include "MagicLeapConnectionsPlugin.h"

void UMagicLeapConnectionsFunctionLibrary::EnableInvitesAsync(const FMagicLeapInviteReceivedDelegate& InInviteReceivedDelegate)
{
	FMagicLeapInviteReceivedDelegateMulti InviteReceivedDelegate;
	InviteReceivedDelegate.Add(InInviteReceivedDelegate);
	GetMagicLeapConnectionsPlugin().EnableInvitesAsync(InviteReceivedDelegate);
}

void UMagicLeapConnectionsFunctionLibrary::DisableInvites()
{
	GetMagicLeapConnectionsPlugin().DisableInvites();
}

bool UMagicLeapConnectionsFunctionLibrary::IsInvitesEnabled()
{
	return GetMagicLeapConnectionsPlugin().IsInvitesEnabled();
}

bool UMagicLeapConnectionsFunctionLibrary::SendInviteAsync(const FMagicLeapConnectionsInviteArgs& Args, FGuid& OutInviteHandle, const FMagicLeapInviteSentDelegate& InInviteSentDelegate)
{
	FMagicLeapInviteSentDelegateMulti InviteSentDelegate;
	InviteSentDelegate.Add(InInviteSentDelegate);
	return GetMagicLeapConnectionsPlugin().SendInviteAsync(Args, OutInviteHandle, InviteSentDelegate);
}

bool UMagicLeapConnectionsFunctionLibrary::CancelInvite(const FGuid& InviteRequestHandle)
{
	return GetMagicLeapConnectionsPlugin().CancelInvite(InviteRequestHandle);
}
