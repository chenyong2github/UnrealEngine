// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Delegates/Delegate.h"
#include "Online/CoreOnline.h"
#include "Online/OnlineIdCommon.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"
#include "eos_connect_types.h"

namespace UE::Online {

class FOnlineAccountIdDataEOS
{
public:
	EOS_EpicAccountId EpicAccountId = nullptr;
	EOS_ProductUserId ProductUserId = nullptr;
};

/**
 * Account id registry specifically for EOS id's which are segmented.
 */
class FOnlineAccountIdRegistryEOS : public IOnlineAccountIdRegistry
{
public:
	static FOnlineAccountIdRegistryEOS& Get();

	FOnlineAccountIdHandle Find(const EOS_EpicAccountId EpicAccountId) const;
	FOnlineAccountIdHandle Find(const EOS_ProductUserId ProductUserId) const;
	FOnlineAccountIdHandle Create(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId);

	// Return copies as it is not thread safe to return pointers/references to array elements, in case the array is grown+relocated on another thread.
	FOnlineAccountIdDataEOS GetIdData(const FOnlineAccountIdHandle& Handle) const;

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FOnlineAccountIdHandle& Handle) const override;
	virtual TArray<uint8> ToReplicationData(const FOnlineAccountIdHandle& Handle) const override;
	virtual FOnlineAccountIdHandle FromReplicationData(const TArray<uint8>& ReplicationString) override;
	// End IOnlineAccountIdRegistry

	virtual ~FOnlineAccountIdRegistryEOS() = default;

private:
	mutable FRWLock Lock;

	TArray<FOnlineAccountIdDataEOS> AccountIdData; // Actual container for the info, indexed by the handle

	TMap<EOS_EpicAccountId, FOnlineAccountIdHandle> EpicAccountIdToHandle; // Map of EOS_EpicAccountId to the associated handle.
	TMap<EOS_ProductUserId, FOnlineAccountIdHandle> ProductUserIdToHandle; // Map of EOS_ProductUserId to the associated handle.

};

EOS_EpicAccountId GetEpicAccountId(const FOnlineAccountIdHandle& Handle);
EOS_EpicAccountId GetEpicAccountIdChecked(const FOnlineAccountIdHandle& Handle);
EOS_ProductUserId GetProductUserId(const FOnlineAccountIdHandle& Handle);
EOS_ProductUserId GetProductUserIdChecked(const FOnlineAccountIdHandle& Handle);

FOnlineAccountIdHandle FindAccountId(const EOS_EpicAccountId EpicAccountId);
FOnlineAccountIdHandle FindAccountId(const EOS_ProductUserId EpicAccountId);
FOnlineAccountIdHandle FindAccountIdChecked(const EOS_EpicAccountId EpicAccountId);
FOnlineAccountIdHandle FindAccountIdChecked(const EOS_ProductUserId EpicAccountId);

FOnlineAccountIdHandle CreateAccountId(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId);

template<typename IdType>
inline bool ValidateOnlineId(const TOnlineIdHandle<IdType>& Handle)
{
	return Handle.GetOnlineServicesType() == EOnlineServices::Epic && Handle.IsValid();
}

} /* namespace UE::Online */
