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
