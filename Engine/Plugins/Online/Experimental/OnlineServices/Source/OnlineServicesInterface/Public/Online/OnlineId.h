// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/TypeHash.h"

#include "Online/OnlineServicesTypes.h"

namespace UE::Online {

/** Enum used as template argument to TOnlineIdHandle to make it a compile error to assign between id's of different types */
enum class EOnlineIdType
{
	AccountId,
	SessionId,
	PartyId
};

/**
 * A handle to an id which uniquely identifies a persistent or transient online resource, i.e. account/session/party etc, within a given Online Services provider.
 * At most one id, and therefore one handle, exists for any given resource. The id and handle persist until the OnlineServices module is unloaded.
 * Passed to and returned from OnlineServices APIs.
 */ 
template<EOnlineIdType IdType>
class TOnlineIdHandle
{
public:
	TOnlineIdHandle() : Type(EOnlineServices::Null), Handle(0) {}

	inline bool IsValid() const { return Handle != 0; }

	/* The Online Services provider this id relates to */
	EOnlineServices Type : 8;
	/* An opaque handle to the underlying online id */
	uint32 Handle : 24;
};

using FOnlineAccountIdHandle = TOnlineIdHandle<EOnlineIdType::AccountId>;

template<EOnlineIdType IdType>
inline bool operator==(const TOnlineIdHandle<IdType>& A, const TOnlineIdHandle<IdType>& B)
{
	return A.Type == B.Type && A.Handle == B.Handle;
}

template<EOnlineIdType IdType>
inline bool operator!=(const TOnlineIdHandle<IdType>& A, const TOnlineIdHandle<IdType>& B)
{
	return !(A == B);
}

template<EOnlineIdType IdType>
inline uint32 GetTypeHash(const TOnlineIdHandle<IdType>& Handle)
{
	using ::GetTypeHash;
	return HashCombine(GetTypeHash(Handle.Type), GetTypeHash(Handle.Handle));
}

template<EOnlineIdType IdType>
inline void LexFromString(const TOnlineIdHandle<IdType>& Id, const TCHAR* String)
{
	// TODO: should instead just implement ParseOnlineExecParams
}

/* UE::Online */ }
