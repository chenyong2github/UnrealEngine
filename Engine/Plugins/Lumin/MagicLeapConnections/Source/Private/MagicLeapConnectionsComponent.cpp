// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapConnectionsComponent.h"
#include "MagicLeapConnectionsPlugin.h"

void UMagicLeapConnectionsComponent::EnableInvitesAsync()
{
	GetMagicLeapConnectionsPlugin().EnableInvitesAsync(OnInviteReceived);
}

void UMagicLeapConnectionsComponent::DisableInvites()
{
	GetMagicLeapConnectionsPlugin().DisableInvites();
}

bool UMagicLeapConnectionsComponent::IsInvitesEnabled() const
{
	return GetMagicLeapConnectionsPlugin().IsInvitesEnabled();
}

bool UMagicLeapConnectionsComponent::SendInviteAsync(const FMagicLeapConnectionsInviteArgs& Args, FGuid& OutInviteHandle)
{
	return GetMagicLeapConnectionsPlugin().SendInviteAsync(Args, OutInviteHandle, OnInviteSent);
}

bool UMagicLeapConnectionsComponent::CancelInvite(const FGuid& InviteRequestHandle)
{
	return GetMagicLeapConnectionsPlugin().CancelInvite(InviteRequestHandle);
}
