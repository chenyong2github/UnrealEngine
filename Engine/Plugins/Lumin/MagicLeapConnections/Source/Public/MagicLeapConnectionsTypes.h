// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapConnectionsTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapConnectionsResult : uint8
{
	/** This MLHandle is not recognized. */
	InvalidHandle,
	/** Indicates number of invitees is invalid. */
	InvalidInviteeCount,
	/** Indicates invite request has been found and the system is attempting to cancel the process. */
	CancellationPending,
	/** Request failed due to system being in an illegal state,
      e.g. user has not successfully logged-in. */
	IllegalState,
	/** \see MLConnectionsRegisterForInvite failed because the system had an error
      with network connectivity, or the servers could not be reached. */
	NetworkFailure,
	/** \see MLConnectionsRegisterForInvite failed because the application is
      already registered to handle invite requests. */
	AlreadyRegistered,
};

UENUM(BlueprintType)
enum class EMagicLeapConnectionsInviteStatus : uint8
{
	/** Indicates the request to start the sending process is being submitted to the system. */
	SubmittingRequest,
	/** Indicates sending process has successfully initiated and invite dialog is being displayed to the user. */
	Pending,
	/** Indicates invite dialog has been completed by the user the invite was successfully sent.
      Invite request resources ready to be freed \see MLContactsReleaseRequestResources. */
	Dispatched,
	/** Indicates that the user has completed the invite dialog but the system was unable to send the invite. */
	DispatchFailed,
	/** Indicates sending process was cancelled either by user, system or \see MLConnectionsCancelInvite. */
	Cancelled,
	/** Unable to determine invite status, because provided handle is invalid. */
	InvalidHandle,
};

UENUM(BlueprintType)
enum class EMagicLeapConnectionsInviteeFilter : uint8
{
	/** Show Magic Leap connections who are online and followed by current user. */
	Following,
	/** Show Magic Leap connections who are online and follow current user. */
	Followers,
	/** Show Magic Leap connections who are online and are mutual followers for current user. */
	Mutual,
};

/** Stores arguments for the sending invite process */
USTRUCT(BlueprintType)
struct FMagicLeapConnectionsInviteArgs
{
	GENERATED_USTRUCT_BODY()

	/** Version of this structure. */
	int32 Version;

	/** Max number of connections to be invited.  Min limit is 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections|MagicLeap")
	int32 InviteeCount;

	/**
		Text prompt to be displayed to the user with invitee selection dialog.  Caller should allocate memory for this.
		Encoding should be in UTF8.  This will be copied internally.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections|MagicLeap")
	FString InviteUserPrompt;

	/**
		Payload message to be delivered to remote copy of the application with invite in serialized text form.  Caller should
		allocate memory for this.  Encoding should be in UTF8.  This will be copied internally.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections|MagicLeap")
	FString InvitePayload;

	/** Type of filter applied by default to ML connections list in invitee selection dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connections|MagicLeap")
	EMagicLeapConnectionsInviteeFilter DefaultInviteeFilter;
};

/** Delegate used to notify the application when a receive invite operation has completed. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapInviteReceivedDelegate, bool, bUserAccepted, FString, Payload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapInviteReceivedDelegateMulti, bool, bUserAccepted, FString, Payload);

/** Delegate used to notify the application when a send invite operation has completed. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapInviteSentDelegate, EMagicLeapConnectionsInviteStatus, InviteStatus, FGuid, InviteHandle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapInviteSentDelegateMulti, EMagicLeapConnectionsInviteStatus, InviteStatus, FGuid, InviteHandle);
