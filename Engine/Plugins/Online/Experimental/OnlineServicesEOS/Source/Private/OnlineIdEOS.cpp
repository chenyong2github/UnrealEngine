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

FOnlineAccountIdHandle FOnlineAccountIdRegistryEOS::Create(const EOS_EpicAccountId EpicAccountId, const EOS_ProductUserId ProductUserId)
{
	FOnlineAccountIdHandle Result;
	const bool bEpicAccountIdValid = EOS_EpicAccountId_IsValid(EpicAccountId) == EOS_TRUE;
	const bool bProductUserIdValid = EOS_ProductUserId_IsValid(ProductUserId) == EOS_TRUE;
	check(bEpicAccountIdValid || bProductUserIdValid);

	if (bEpicAccountIdValid || bProductUserIdValid)
	{
		auto FindExisting = [&]()
		{
			FOnlineAccountIdHandle Result;
			if (const FOnlineAccountIdHandle* FoundEas = bEpicAccountIdValid ? EpicAccountIdToHandle.Find(EpicAccountId) : nullptr)
			{
				Result = *FoundEas;
			}
			else if (const FOnlineAccountIdHandle* FoundProd = bProductUserIdValid ? ProductUserIdToHandle.Find(ProductUserId) : nullptr)
			{
				Result = *FoundProd;
			}

			if (Result.IsValid())
			{
				check(EpicAccountId == AccountIdData[Result.GetHandle() - 1].EpicAccountId);
				check(ProductUserId == AccountIdData[Result.GetHandle() - 1].ProductUserId);
			}

			return Result;
		};

		// First take read lock and look for existing elements
		{
			const FReadScopeLock ReadLock(Lock);
			Result = FindExisting();
		}

		if(!Result.IsValid())
		{
			// Next take write lock, check again for existing elements (in case another thread added it)
			const FWriteScopeLock WriteLock(Lock);
			Result = FindExisting();
			if (!Result.IsValid())
			{
				// No existing element, so add a new one
				FOnlineAccountIdDataEOS& NewAccountIdData = AccountIdData.Emplace_GetRef();
				NewAccountIdData.EpicAccountId = EpicAccountId;
				NewAccountIdData.ProductUserId = ProductUserId;

				Result = FOnlineAccountIdHandle(EOnlineServices::Epic, AccountIdData.Num());

				if (bEpicAccountIdValid)
				{
					EpicAccountIdToHandle.Emplace(EpicAccountId, Result);
				}
				if (bProductUserIdValid)
				{
					ProductUserIdToHandle.Emplace(ProductUserId, Result);
				}
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
