// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapConnectionsTypes.h"
#include "MagicLeapConnectionsComponent.generated.h"

/**
	Component that provides access to the Connections API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPCONNECTIONS_API UMagicLeapConnectionsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
		Enables the sending and receiving of invites.
	*/
	UFUNCTION(BlueprintCallable, Category = "Connections | MagicLeap")
	void EnableInvitesAsync();

	/**
		Disables the sending and receiving of invites.
	*/
	UFUNCTION(BlueprintCallable, Category = "Connections | MagicLeap")
	void DisableInvites();

	/**
		Indicates the current status of the invite send/receive servers.
		@return True if sending and receiving invites are enabled, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Connections | MagicLeap")
	bool IsInvitesEnabled() const;

	/**
		Submits a request to start the invite sending process.  Request an invite to be sent for other connections to join a
		multi-user experience.  This call will trigger a connections invite dialog requesting the user to select up to the
		specified number of online users to be invited.  The system will then initiate a push notification to other online devices,
		start a copy of the application requesting the invite and deliver the given payload.  If the requesting application is not
		installed on the receiving device, the system will prompt the user to install via Magic Leap World app.
		@param Args The arguments for the sending invite process.
		@param OutInviteHandle A valid FGuid to the invite request if the function call succeeds.
		@return True if the invite is successfully created, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Connections | MagicLeap")
	bool SendInviteAsync(const FMagicLeapConnectionsInviteArgs& Args, FGuid& OutInviteHandle);

	/**
		Attempts to cancel a previously requested invite sending process.  If invite dialog has not yet been completed by the
		user, this request will dismiss the dialog and cancel the invite sending process.  Otherwise this operation will return an
		error.
		@param InviteRequestHandle The handle to the invite to be cancelled.
		@return True if the invite was cancelled, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Connections | MagicLeap")
	bool CancelInvite(const FGuid& InviteRequestHandle);

private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Connections | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapInviteReceivedDelegateMulti OnInviteReceived;

	UPROPERTY(BlueprintAssignable, Category = "Connections | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapInviteSentDelegateMulti OnInviteSent;
};
