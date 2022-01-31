// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineIdEOS.h"

#include "Online/OnlineAsyncOp.h"
#include "String/HexToBytes.h"
#include "String/BytesToHex.h"
#include "Online/OnlineServicesEOSTypes.h"

#include "eos_connect_types.h"
#include "eos_connect.h"

namespace UE::Online {

FOnlineAccountIdRegistryEOS& FOnlineAccountIdRegistryEOS::Get()
{
	static FOnlineAccountIdRegistryEOS Instance;
	return Instance;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Find(const EOS_EpicAccountId EpicAccountId) const
{
	FOnlineAccountIdHandle Handle;
	if (EOS_EpicAccountId_IsValid(EpicAccountId) == EOS_TRUE)
	{
		const FReadScopeLock ReadLock(Lock);
		if (const FOnlineAccountIdHandle* Found = EpicAccountIdToHandle.Find(EpicAccountId))
		{
			Handle = *Found;
		}
	}
	return Handle;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Find(const EOS_ProductUserId ProductUserId) const
{
	FOnlineAccountIdHandle Handle;
	if (EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE)
	{
		const FReadScopeLock ReadLock(Lock);
		if (const FOnlineAccountIdHandle* Found = ProductUserIdToHandle.Find(ProductUserId))
		{
			Handle = *Found;
		}
	}
	return Handle;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Create(const EOS_EpicAccountId InEpicAccountId, const EOS_ProductUserId InProductUserId)
{
	FOnlineAccountIdHandle Result;
	const bool bInEpicAccountIdValid = EOS_EpicAccountId_IsValid(InEpicAccountId) == EOS_TRUE;
	const bool bInProductUserIdValid = EOS_ProductUserId_IsValid(InProductUserId) == EOS_TRUE;
	if (ensure(bInEpicAccountIdValid || bInProductUserIdValid))
	{
		bool bUpdateEpicAccountId = false;
		bool bUpdateProductUserId = false;

		auto FindExisting = [this, InEpicAccountId, InProductUserId, bInEpicAccountIdValid, bInProductUserIdValid, &bUpdateEpicAccountId, &bUpdateProductUserId]()
		{
			FOnlineAccountIdHandle Result;
			if (const FOnlineAccountIdHandle* FoundEas = bInEpicAccountIdValid ? EpicAccountIdToHandle.Find(InEpicAccountId) : nullptr)
			{
				Result = *FoundEas;
			}
			else if (const FOnlineAccountIdHandle* FoundProd = bInProductUserIdValid ? ProductUserIdToHandle.Find(InProductUserId) : nullptr)
			{
				Result = *FoundProd;
			}

			if (Result.IsValid())
			{
				const EOS_EpicAccountId FoundEpicAccountId = AccountIdData[Result.GetHandle() - 1].EpicAccountId;
				const EOS_ProductUserId FoundProductUserId = AccountIdData[Result.GetHandle() - 1].ProductUserId;

				// Check that the found EAS/EOS ids are either unset, or match the input. If a valid input is passed for a currently unset field, this is an update,
				// which we will track here and complete later under a write lock.
				check(!FoundEpicAccountId || InEpicAccountId == FoundEpicAccountId);
				check(!FoundProductUserId || InProductUserId == FoundProductUserId);
				bUpdateEpicAccountId = !FoundEpicAccountId && bInEpicAccountIdValid;
				bUpdateProductUserId = !FoundProductUserId && bInProductUserIdValid;
			}

			return Result;
		};

		{
			// First take read lock and look for existing elements
			const FReadScopeLock ReadLock(Lock);
			Result = FindExisting();
		}

		if(!Result.IsValid())
		{
			// Double-checked locking. If we didn't find an element, we take the write lock, and look again, in case another thread raced with us and added one.
			const FWriteScopeLock WriteLock(Lock);
			Result = FindExisting();

			if (!Result.IsValid())
			{
				// We still didn't find one, so now we can add one.
				FOnlineAccountIdDataEOS& NewAccountIdData = AccountIdData.Emplace_GetRef();
				NewAccountIdData.EpicAccountId = InEpicAccountId;
				NewAccountIdData.ProductUserId = InProductUserId;

				Result = FOnlineAccountIdHandle(EOnlineServices::Epic, AccountIdData.Num());

				if (bInEpicAccountIdValid)
				{
					EpicAccountIdToHandle.Emplace(InEpicAccountId, Result);
				}
				if (bInProductUserIdValid)
				{
					ProductUserIdToHandle.Emplace(InProductUserId, Result);
				}
			}
		}

		check(Result.IsValid());
		if (bUpdateEpicAccountId || bUpdateProductUserId)
		{
			// Finally, update any previously unset fields for which we now have a valid value.
			const FWriteScopeLock WriteLock(Lock);
			FOnlineAccountIdDataEOS& AccountIdDataToUpdate = AccountIdData[Result.GetHandle() -1];
			if (bUpdateEpicAccountId)
			{
				AccountIdDataToUpdate.EpicAccountId = InEpicAccountId;
				EpicAccountIdToHandle.Emplace(InEpicAccountId, Result);
			}
			if (bUpdateProductUserId)
			{
				AccountIdDataToUpdate.ProductUserId = InProductUserId;
				ProductUserIdToHandle.Emplace(InProductUserId, Result);
			}
		}
	}
	
	return Result;
}

FOnlineAccountIdDataEOS FOnlineAccountIdRegistryEOS::GetIdData(const FOnlineAccountIdHandle& Handle) const
{
	if (ValidateOnlineId(Handle))
	{
		FReadScopeLock ScopeLock(Lock);
		return AccountIdData[Handle.GetHandle() - 1];
	}
	return FOnlineAccountIdDataEOS();
}

FString FOnlineAccountIdRegistryEOS::ToLogString(const FOnlineAccountIdHandle& Handle) const
{
	FString Result;
	if (ValidateOnlineId(Handle))
	{
		const FOnlineAccountIdDataEOS& IdData = GetIdData(Handle);
		Result = FString::Printf(TEXT("EAS=[%s] EOS=[%s]"), *LexToString(IdData.EpicAccountId), *LexToString(IdData.ProductUserId));
	}
	else
	{
		check(!Handle.IsValid()); // Check we haven't been passed a valid handle for a different EOnlineServices.
		Result = TEXT("Invalid");
	}
	return Result;
}

enum class EEOSAccountIdElements : uint8
{	
	None = 0,
	EAS = 1 << 0,
	EOS = 1 << 1
};
ENUM_CLASS_FLAGS(EEOSAccountIdElements);
const uint8 OnlineIdEOSUtf8BufferLength = 32;
const uint8 OnlineIdEOSHexBufferLength = 16;

TArray<uint8> FOnlineAccountIdRegistryEOS::ToReplicationData(const FOnlineAccountIdHandle& Handle) const
{
	TArray<uint8> ReplicationData;
	if (ValidateOnlineId(Handle))
	{
		const FOnlineAccountIdDataEOS& IdData = GetIdData(Handle);
		EEOSAccountIdElements Elements = EEOSAccountIdElements::None;

		char EasBuffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = { 0 };
		if (EOS_EpicAccountId_IsValid(IdData.EpicAccountId))
		{
			int32 EasBufferLength = sizeof(EasBuffer);
			const EOS_EResult EosResult = EOS_EpicAccountId_ToString(IdData.EpicAccountId, EasBuffer, &EasBufferLength);
			if (ensure(EosResult == EOS_EResult::EOS_Success))
			{
				check(EasBufferLength-1 == OnlineIdEOSUtf8BufferLength);
				Elements |= EEOSAccountIdElements::EAS;
			}
		}

		char EosBuffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = { 0 };
		if (EOS_ProductUserId_IsValid(IdData.ProductUserId))
		{
			int32 EosBufferLength = sizeof(EosBuffer);
			const EOS_EResult EosResult = EOS_ProductUserId_ToString(IdData.ProductUserId, EosBuffer, &EosBufferLength);
			if (ensure(EosResult == EOS_EResult::EOS_Success))
			{
				check(EosBufferLength-1 == OnlineIdEOSUtf8BufferLength);
				Elements |= EEOSAccountIdElements::EOS;
			}
		}

		const int EasHexBufferLength = EnumHasAnyFlags(Elements, EEOSAccountIdElements::EAS) ? OnlineIdEOSHexBufferLength : 0;
		const int EosHexBufferLength = EnumHasAnyFlags(Elements, EEOSAccountIdElements::EOS) ? OnlineIdEOSHexBufferLength : 0;
		ReplicationData.SetNumUninitialized(1 + EasHexBufferLength + EosHexBufferLength);
		ReplicationData[0] = uint8(Elements);
		if (EasHexBufferLength > 0)
		{
			UE::String::HexToBytes(FUtf8StringView(EasBuffer, OnlineIdEOSUtf8BufferLength), ReplicationData.GetData() + 1);
		}
		if (EosHexBufferLength > 0)
		{
			UE::String::HexToBytes(FUtf8StringView(EosBuffer, OnlineIdEOSUtf8BufferLength), ReplicationData.GetData() + 1 + EasHexBufferLength);
		}
	}
	else
	{
		check(!Handle.IsValid()); // Check we haven't been passed a valid handle for a different EOnlineServices.
	}
	return ReplicationData;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::FromReplicationData(const TArray<uint8>& ReplicationData)
{
	const EEOSAccountIdElements Elements = EEOSAccountIdElements(ReplicationData[0]);
	const int EasHexBufferLength = EnumHasAnyFlags(Elements, EEOSAccountIdElements::EAS) ? OnlineIdEOSHexBufferLength : 0;
	const int EosHexBufferLength = EnumHasAnyFlags(Elements, EEOSAccountIdElements::EOS) ? OnlineIdEOSHexBufferLength : 0;

	EOS_EpicAccountId EpicAccountId = nullptr;
	if (EasHexBufferLength > 0)
	{
		char EasBuffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = { 0 };
		UE::String::BytesToHex(TConstArrayView<uint8>(ReplicationData.GetData() + 1, EasHexBufferLength), EasBuffer);
		EpicAccountId = EOS_EpicAccountId_FromString(EasBuffer);
	}

	EOS_ProductUserId ProductUserId = nullptr;
	if (EosHexBufferLength > 0)
	{
		char EosBuffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = { 0 };
		UE::String::BytesToHex(TConstArrayView<uint8>(ReplicationData.GetData() + 1 + EasHexBufferLength, EosHexBufferLength), EosBuffer);
		ProductUserId = EOS_ProductUserId_FromString(EosBuffer);
	}
	
	return Create(EpicAccountId, ProductUserId);
}

EOS_EpicAccountId GetEpicAccountId(const FOnlineAccountIdHandle& Handle)
{
	return FOnlineAccountIdRegistryEOS::Get().GetIdData(Handle).EpicAccountId;
}

EOS_EpicAccountId GetEpicAccountIdChecked(const FOnlineAccountIdHandle& Handle)
{
	EOS_EpicAccountId EpicAccountId = GetEpicAccountId(Handle);
	check(EOS_EpicAccountId_IsValid(EpicAccountId) == EOS_TRUE);
	return EpicAccountId;
}

EOS_ProductUserId GetProductUserId(const FOnlineAccountIdHandle& Handle)
{
	return FOnlineAccountIdRegistryEOS::Get().GetIdData(Handle).ProductUserId;
}

EOS_ProductUserId GetProductUserIdChecked(const FOnlineAccountIdHandle& Handle)
{
	EOS_ProductUserId ProductUserId = GetProductUserId(Handle);
	check(EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE);
	return ProductUserId;
}

FOnlineAccountIdHandle FindAccountId(const EOS_EpicAccountId EpicAccountId)
{
	return FOnlineAccountIdRegistryEOS::Get().Find(EpicAccountId);
}

FOnlineAccountIdHandle FindAccountId(const EOS_ProductUserId ProductUserId)
{
	return FOnlineAccountIdRegistryEOS::Get().Find(ProductUserId);
}

FOnlineAccountIdHandle FindAccountIdChecked(const EOS_EpicAccountId EpicAccountId)
{
	FOnlineAccountIdHandle Result = FindAccountId(EpicAccountId);
	check(Result.IsValid());
	return Result;
}

FOnlineAccountIdHandle FindAccountIdChecked(const EOS_ProductUserId ProductUserId)
{
	FOnlineAccountIdHandle Result = FindAccountId(ProductUserId);
	check(Result.IsValid());
	return Result;
}

FOnlineAccountIdHandle CreateAccountId(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId)
{
	return FOnlineAccountIdRegistryEOS::Get().Create(EpicAccountId, ProductUserId);
}

/* UE::Online */ }
