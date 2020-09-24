// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapDispatchTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapDispatchResult : uint8
{
	Ok,
	CannotStartApp,
	InvalidPacket,
	NoAppFound,
	AppPickerDialogFailure,
	AllocFailed,
	InvalidParam,
	UnspecifiedFailure,
	NotImplemented
};

/**
	Delegate used to notify the initiating blueprint of the response from an OAuth request.
	@param Response Contains the response url from authorization service as well as calling context.
*/
DECLARE_DELEGATE_OneParam(FMagicLeapOAuthSchemaHandlerStatic, const FString&);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapOAuthSchemaHandler, const FString&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapOAuthSchemaHandlerMulti, const FString&, Response);
