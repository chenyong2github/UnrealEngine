// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/CookProfilerProvider.h"
#include "CookProfilerProviderPrivate.h"
#include "AnalysisServicePrivate.h"

namespace TraceServices
{
thread_local FProviderLock* GThreadCurrentCookProviderLock;
thread_local int32 GThreadCurrentReadCookProviderLockCount;
thread_local int32 GThreadCurrentWriteCookProviderLockCount;

void FCookProfilerProvider::BeginEdit() const
{
	Lock.BeginWrite(GThreadCurrentCookProviderLock, GThreadCurrentReadCookProviderLockCount, GThreadCurrentWriteCookProviderLockCount);
}

void FCookProfilerProvider::EndEdit() const
{
	Lock.EndWrite(GThreadCurrentCookProviderLock, GThreadCurrentWriteCookProviderLockCount);
}

void FCookProfilerProvider::EditAccessCheck() const
{
	Lock.WriteAccessCheck(GThreadCurrentReadCookProviderLockCount);
}

void FCookProfilerProvider::BeginRead() const
{
	Lock.BeginRead(GThreadCurrentCookProviderLock, GThreadCurrentReadCookProviderLockCount, GThreadCurrentWriteCookProviderLockCount);
}

void FCookProfilerProvider::EndRead() const
{
	Lock.EndRead(GThreadCurrentCookProviderLock, GThreadCurrentReadCookProviderLockCount);
}

void FCookProfilerProvider::ReadAccessCheck() const
{
	Lock.ReadAccessCheck(GThreadCurrentCookProviderLock, GThreadCurrentReadCookProviderLockCount, GThreadCurrentWriteCookProviderLockCount);
}

FCookProfilerProvider::FCookProfilerProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
	
}

void FCookProfilerProvider::AddPackage(uint64 Id, TStringView<TCHAR> Name)
{
	PackageIdToIndexMap.Add(Id, Packages.Num());
	Packages.Emplace(Id, Session.StoreString(Name));
}

void FCookProfilerProvider::EnumeratePackages(double StartTime, double EndTime, EnumeratePackagesCallback Callback) const
{
	for (const FPackageData& Package : Packages)
	{
		if (Callback(Package) == false)
		{
			break;
		}
	}
}

uint32 FCookProfilerProvider::GetNumPackages() const
{
	return (uint32) Packages.Num();
}

FPackageData* FCookProfilerProvider::EditPackage(uint64 Id)
{
	uint32* Index = PackageIdToIndexMap.Find(Id);

	if (Index && *Index < Packages.Num())
	{
		FPackageData& Package = Packages[*Index];
		return &Package;
	}

	return nullptr;
}

} // namespace TraceServices
