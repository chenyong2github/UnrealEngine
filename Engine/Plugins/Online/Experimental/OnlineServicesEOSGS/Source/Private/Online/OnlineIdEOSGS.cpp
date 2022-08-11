// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineIdEOSGS.h"

#include "Online/OnlineAsyncOp.h"
#include "String/HexToBytes.h"
#include "String/BytesToHex.h"
#include "Online/OnlineServicesEOSGSTypes.h"

namespace UE::Online {

const uint8 OnlineIdEOSUtf8BufferLength = 32;
const uint8 OnlineIdEOSHexBufferLength = 16;

FOnlineAccountIdRegistryEOSGS::FOnlineAccountIdRegistryEOSGS()
	: Registry(EOnlineServices::Epic)
{

}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOSGS::FindAccountId(const EOS_ProductUserId ProductUserId) const
{
	return Registry.FindHandle(ProductUserId);
}

EOS_ProductUserId FOnlineAccountIdRegistryEOSGS::GetProductUserId(const FOnlineAccountIdHandle& Handle) const
{
	return Registry.FindIdValue(Handle);
}

FString FOnlineAccountIdRegistryEOSGS::ToLogString(const FOnlineAccountIdHandle& Handle) const
{
	if (Registry.ValidateOnlineId(Handle))
	{
		EOS_ProductUserId ProductUserId = Registry.FindIdValue(Handle);
		return LexToString(ProductUserId);
	}
	return FString();
}

TArray<uint8> FOnlineAccountIdRegistryEOSGS::ToReplicationData(const FOnlineAccountIdHandle& Handle) const
{
	TArray<uint8> ReplicationData;
	if (Registry.ValidateOnlineId(Handle))
	{
		EOS_ProductUserId ProductUserId = Registry.FindIdValue(Handle);
		if (ensure(EOS_ProductUserId_IsValid(ProductUserId)))
		{
			char EosBuffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
			int32 EosBufferLength = sizeof(EosBuffer);
			const EOS_EResult EosResult = EOS_ProductUserId_ToString(ProductUserId, EosBuffer, &EosBufferLength);
			if (ensure(EosResult == EOS_EResult::EOS_Success))
			{
				check(EosBufferLength - 1 == OnlineIdEOSUtf8BufferLength);
				ReplicationData.SetNumUninitialized(OnlineIdEOSHexBufferLength);
				UE::String::HexToBytes(FUtf8StringView(EosBuffer, OnlineIdEOSUtf8BufferLength), ReplicationData.GetData());
			}
		}
	}
	return ReplicationData;
}
FOnlineAccountIdHandle FOnlineAccountIdRegistryEOSGS::FromReplicationData(const TArray<uint8>& ReplicationData)
{
	if (ReplicationData.Num() == OnlineIdEOSHexBufferLength)
	{
		char EosBuffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {};
		UE::String::BytesToHex(TConstArrayView<uint8>(ReplicationData.GetData(), OnlineIdEOSHexBufferLength), EosBuffer);
		EOS_ProductUserId ProductUserId = EOS_ProductUserId_FromString(EosBuffer);
		return FindOrAddAccountId(ProductUserId);
	}
	return Registry.GetInvalidHandle();
}

IOnlineAccountIdRegistryEOSGS& FOnlineAccountIdRegistryEOSGS::GetRegistered()
{
	IOnlineAccountIdRegistry* Registry = FOnlineIdRegistryRegistry::Get().GetAccountIdRegistry(EOnlineServices::Epic);
	check(Registry);
	return *static_cast<IOnlineAccountIdRegistryEOSGS*>(Registry);
}

FOnlineAccountIdRegistryEOSGS& FOnlineAccountIdRegistryEOSGS::Get()
{
	static FOnlineAccountIdRegistryEOSGS Instance;
	return Instance;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOSGS::FindOrAddAccountId(const EOS_ProductUserId ProductUserId)
{
	if (ensure(EOS_ProductUserId_IsValid(ProductUserId)))
	{
		return Registry.FindOrAddHandle(ProductUserId);
	}
	return Registry.GetInvalidHandle();
}

EOS_ProductUserId GetProductUserId(const FOnlineAccountIdHandle& Handle)
{
	return FOnlineAccountIdRegistryEOSGS::GetRegistered().GetProductUserId(Handle);
}

EOS_ProductUserId GetProductUserIdChecked(const FOnlineAccountIdHandle& Handle)
{
	EOS_ProductUserId ProductUserId = GetProductUserId(Handle);
	check(EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE);
	return ProductUserId;
}

FOnlineAccountIdHandle FindAccountId(const EOS_ProductUserId ProductUserId)
{
	return FOnlineAccountIdRegistryEOSGS::GetRegistered().FindAccountId(ProductUserId);
}

FOnlineAccountIdHandle FindAccountIdChecked(const EOS_ProductUserId ProductUserId)
{
	FOnlineAccountIdHandle Result = FindAccountId(ProductUserId);
	check(Result.IsValid());
	return Result;
}

/* UE::Online */ }
