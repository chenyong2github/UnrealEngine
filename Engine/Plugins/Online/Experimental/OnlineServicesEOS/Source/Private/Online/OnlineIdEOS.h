// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/OnlineIdEOSGS.h"

class FAccountIdReplicationTest;

namespace UE::Online {

class FOnlineAccountIdDataEOS
{
public:
	EOS_EpicAccountId EpicAccountId = nullptr;
	EOS_ProductUserId ProductUserId = nullptr;
};

/**
 * Account id registry specifically for EOS id's which are an EOS_EpicAccountId/EOS_ProductUserId pair.
 */
class FOnlineAccountIdRegistryEOS : public IOnlineAccountIdRegistryEOSGS
{
public:
	virtual ~FOnlineAccountIdRegistryEOS() = default;

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FOnlineAccountIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineAccountIdHandle& Handle) const override;
	virtual FOnlineAccountIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	// Begin IOnlineAccountRegistryEOSGS
	virtual FOnlineAccountIdHandle FindAccountId(const EOS_ProductUserId ProductUserId) const override;
	virtual EOS_ProductUserId GetProductUserId(const FOnlineAccountIdHandle& Handle) const override;
	// End IOnlineAccountRegistryEOSGS

	FOnlineAccountIdHandle FindAccountId(const EOS_EpicAccountId EpicAccountId) const;

	// Return copies as it is not thread safe to return pointers/references to array elements, in case the array is grown+relocated on another thread.
	FOnlineAccountIdDataEOS GetAccountIdData(const FOnlineAccountIdHandle& Handle) const;

	static FOnlineAccountIdRegistryEOS& Get();

private:
	friend class FAuthEOS;
	friend class FOnlineServicesEOSModule;
	friend FAccountIdReplicationTest;
	FOnlineAccountIdHandle FindOrAddAccountId(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId);

	mutable FRWLock Lock;

	TArray<FOnlineAccountIdDataEOS> AccountIdData; // Actual container for the info, indexed by the handle

	TMap<EOS_EpicAccountId, FOnlineAccountIdHandle> EpicAccountIdToHandle; // Map of EOS_EpicAccountId to the associated handle.
	TMap<EOS_ProductUserId, FOnlineAccountIdHandle> ProductUserIdToHandle; // Map of EOS_ProductUserId to the associated handle.
};

EOS_EpicAccountId GetEpicAccountId(const FOnlineAccountIdHandle& Handle);
EOS_EpicAccountId GetEpicAccountIdChecked(const FOnlineAccountIdHandle& Handle);
FOnlineAccountIdHandle FindAccountId(const EOS_EpicAccountId EpicAccountId);
FOnlineAccountIdHandle FindAccountIdChecked(const EOS_EpicAccountId EpicAccountId);

} /* namespace UE::Online */
