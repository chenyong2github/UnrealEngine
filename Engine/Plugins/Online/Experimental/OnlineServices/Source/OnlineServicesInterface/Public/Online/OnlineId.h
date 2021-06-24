// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/TypeHash.h"
#include "UObject/NameTypes.h"

/**
 * This file is a placeholder, will be implemented along with the id registry in the future
 */

namespace UE::Online {

using FOnlineIdHandle = int32;
struct FOnlineId
{
	FName Type;
	FOnlineIdHandle Handle;
};

using FAccountId = FOnlineId;

inline bool operator==(const FOnlineId& A, const FOnlineId& B)
{
	return A.Type == B.Type && A.Handle == B.Handle;
}

inline bool operator!=(const FOnlineId& A, const FOnlineId& B)
{
	return !(A == B);
}

inline uint32 GetTypeHash(const FOnlineId& AccountId)
{
	using ::GetTypeHash;
	return HashCombine(GetTypeHash(AccountId.Type), GetTypeHash(AccountId.Handle));
}

/* UE::Online */ }
