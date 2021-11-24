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

inline FString ToLogString(const FOnlineId& Id)
{
	// TODO: Redact in shipping once we have the id registry version of this
	return FString::Printf(TEXT("%s:%x"), *Id.Type.ToString(), Id.Handle);
}

inline void LexFromString(FOnlineId& Id, const TCHAR* String)
{
	// TODO: should instead just implement ParseOnlineExecParams
}

/* UE::Online */ }
