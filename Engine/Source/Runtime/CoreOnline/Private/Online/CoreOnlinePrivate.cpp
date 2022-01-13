// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/CoreOnlinePrivate.h"

namespace UE::Online {

FString FOnlineForeignAccountIdRegistry::ToLogString(const FOnlineAccountIdHandle& Handle) const
{
	FString Result;
	if (Handle.IsValid())
	{
		const EOnlineServices HandleType = Handle.GetOnlineServicesType();
		const uint32 HandleIndex = Handle.GetHandle() - 1;
		if (const FRepData* RepDataForServices = OnlineServicesToRepData.Find(HandleType))
		{
			const TArray<TArray<uint8>>& RepDataArray = RepDataForServices->RepDataArray;
			if (ensure(RepDataArray.IsValidIndex(HandleIndex)))
			{
				const TArray<uint8>& RepData = RepDataArray[HandleIndex];
				Result = FString::Printf(TEXT("ForeignId=[Type=%d Handle=%d RepData=[%s]"),
					HandleType,
					HandleIndex,
					*FString::FromHexBlob(RepData.GetData(), RepData.Num()));
			}
		}
	}
	return Result;
}

TArray<uint8> FOnlineForeignAccountIdRegistry::ToReplicationData(const FOnlineAccountIdHandle& Handle) const
{
	TArray<uint8> RepData;
	if (Handle.IsValid())
	{
		const EOnlineServices HandleType = Handle.GetOnlineServicesType();
		const uint32 HandleIndex = Handle.GetHandle() - 1;
		if (const FRepData* RepDataForServices = OnlineServicesToRepData.Find(Handle.GetOnlineServicesType()))
		{
			const TArray<TArray<uint8>>& RepDataArray = RepDataForServices->RepDataArray;
			if (ensure(RepDataArray.IsValidIndex(HandleIndex)))
			{
				RepData = RepDataArray[HandleIndex];
			}
		}
	}
	return RepData;
}

FOnlineAccountIdHandle FOnlineForeignAccountIdRegistry::FromReplicationData(EOnlineServices Services, const TArray<uint8>& RepData)
{
	FOnlineAccountIdHandle Handle;
	if (RepData.Num())
	{
		FRepData& RepDataForServices = OnlineServicesToRepData.FindOrAdd(Services);
		if (FOnlineAccountIdHandle* Found = RepDataForServices.RepDataToHandle.Find(RepData))
		{
			Handle = *Found;
		}
		else
		{
			RepDataForServices.RepDataArray.Add(RepData);
			Handle = FOnlineAccountIdHandle(Services, RepDataForServices.RepDataArray.Num());
			RepDataForServices.RepDataToHandle.Emplace(RepData, Handle);
		}
	}
	return Handle;
}

}