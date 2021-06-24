// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * This file is a placeholder, FOnlineError will be implemented in the future
 */

namespace UE::Online {

class FOnlineError
{
public:
	FOnlineError() = default;
	FOnlineError(FString&& InError) : Error(MoveTemp(InError)) {}
	bool operator==(const FOnlineError& Other) const { return Error == Other.Error; }

	FString Error;
};

namespace Errors
{
	inline FOnlineError Cancelled() { return FOnlineError(); }
	inline FOnlineError Unimplemented() { return FOnlineError(); }
}

/* UE::Online */ }
