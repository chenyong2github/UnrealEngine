// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineIdEOS.h"

#include "OnlineServicesEOSTypes.h"
#include "Online/OnlineAsyncOp.h"

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
	Handle.Type = EOnlineServices::Epic;
	if (EOS_EpicAccountId_IsValid(EpicAccountId) == EOS_TRUE)
	{
		const FReadScopeLock ReadLock(Lock);
		if (const uint32* Found = EpicAccountIdToHandle.Find(EpicAccountId))
		{
			Handle.Handle = *Found;
		}
	}
	return Handle;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Find(const EOS_ProductUserId ProductUserId) const
{
	FOnlineAccountIdHandle Handle;
	Handle.Type = EOnlineServices::Epic;
	if (EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE)
	{
		const FReadScopeLock ReadLock(Lock);
		if (const uint32* Found = ProductUserIdToHandle.Find(ProductUserId))
		{
			Handle.Handle = *Found;
		}
	}
	return Handle;
}

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Create(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId)
{
	FOnlineAccountIdHandle Result;
	Result.Type = EOnlineServices::Epic;
	const bool bEpicAccountIdValid = EOS_EpicAccountId_IsValid(EpicAccountId) == EOS_TRUE;
	const bool bProductUserIdValid = EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE;
	check(bEpicAccountIdValid || bProductUserIdValid);

	if (bEpicAccountIdValid || bProductUserIdValid)
	{
		auto FindExisting = [&]() -> uint32
		{
			uint32 Result = 0;
			if (const uint32* FoundEas = bEpicAccountIdValid ? EpicAccountIdToHandle.Find(EpicAccountId) : nullptr)
			{
				Result = *FoundEas;
			}
			else if (const uint32* FoundProd = bProductUserIdValid ? ProductUserIdToHandle.Find(ProductUserId) : nullptr)
			{
				Result = *FoundProd;
			}

			if (Result != 0)
			{
				check(EpicAccountId == AccountIdData[Result - 1].EpicAccountId);
				check(ProductUserId == AccountIdData[Result - 1].ProductUserId);
			}

			return Result;
		};

		// First take read lock and look for existing elements
		{
			const FReadScopeLock ReadLock(Lock);
			Result.Handle = FindExisting();
		}

		if(!Result.IsValid())
		{
			// Next take write lock, check again for existing elements (in case another thread added it)
			const FWriteScopeLock WriteLock(Lock);
			Result.Handle = FindExisting();
			if (!Result.IsValid())
			{
				// No existing element, so add a new one
				FOnlineAccountIdDataEOS& NewAccountIdData = AccountIdData.Emplace_GetRef();
				NewAccountIdData.EpicAccountId = EpicAccountId;
				NewAccountIdData.ProductUserId = ProductUserId;

				Result.Handle = AccountIdData.Num();

				if (bEpicAccountIdValid)
				{
					EpicAccountIdToHandle.Emplace(EpicAccountId, CopyTemp(Result.Handle));
				}
				if (bProductUserIdValid)
				{
					ProductUserIdToHandle.Emplace(ProductUserId, CopyTemp(Result.Handle));
				}
			}
		}
	}
	
	return Result;
}

FOnlineAccountIdDataEOS FOnlineAccountIdRegistryEOS::GetIdData(const FOnlineAccountIdHandle& Handle) const
{
	if (Handle.Type == EOnlineServices::Epic && Handle.IsValid())
	{
		FReadScopeLock ScopeLock(Lock);
		return AccountIdData[Handle.Handle - 1];
	}
	return FOnlineAccountIdDataEOS();
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
