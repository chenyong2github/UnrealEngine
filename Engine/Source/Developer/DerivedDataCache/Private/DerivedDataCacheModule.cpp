// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheModule.h"

#include "CoreGlobals.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCachePrivate.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

namespace UE::DerivedData::Private
{

static FDerivedDataCacheInterface* GDerivedDataLegacyCache;
static ICache* GDerivedDataCache;
static IBuild* GDerivedDataBuild;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

class FDerivedDataCacheModule final : public IDerivedDataCacheModule
{
public:
	FDerivedDataCacheInterface& GetDDC() final
	{
		return **CreateOrGetCache();
	}

	FDerivedDataCacheInterface* const* CreateOrGetCache() final
	{
		CreateCacheOnce();
		check(GDerivedDataLegacyCache);
		return &GDerivedDataLegacyCache;
	}

	void CreateCacheOnce()
	{
		FScopeLock Lock(&CreateLock);
		if (!GDerivedDataCache)
		{
			GDerivedDataCache = CreateCache(&GDerivedDataLegacyCache);
			check(GDerivedDataCache);
		}
	}

	void CreateBuildOnce()
	{
		CreateCacheOnce();
		FScopeLock Lock(&CreateLock);
		if (!GDerivedDataBuild)
		{
			GDerivedDataBuild = CreateBuild(*GDerivedDataCache);
			check(GDerivedDataBuild);
		}
	}

	void ShutdownModule() final
	{
		delete GDerivedDataBuild;
		GDerivedDataBuild = nullptr;
		delete GDerivedDataCache;
		GDerivedDataLegacyCache = nullptr;
		GDerivedDataCache = nullptr;
	}

private:
	FCriticalSection CreateLock;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

static FDerivedDataCacheModule* GetModule()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		check(IsInGameThread());
		if (IDerivedDataCacheModule* Module = FModuleManager::LoadModulePtr<IDerivedDataCacheModule>("DerivedDataCache"))
		{
			return static_cast<FDerivedDataCacheModule*>(Module);
		}
	}
	return nullptr;
}

} // UE::DerivedData::Private

namespace UE::DerivedData
{

ICache& GetCache()
{
	if (ICache* Cache = Private::GDerivedDataCache)
	{
		return *Cache;
	}
	if (Private::FDerivedDataCacheModule* Module = Private::GetModule())
	{
		Module->CreateCacheOnce();
	}
	ICache* Cache = Private::GDerivedDataCache;
	check(Cache);
	return *Cache;
}

IBuild& GetBuild()
{
	if (IBuild* Build = Private::GDerivedDataBuild)
	{
		return *Build;
	}
	if (Private::FDerivedDataCacheModule* Module = Private::GetModule())
	{
		Module->CreateBuildOnce();
	}
	IBuild* Build = Private::GDerivedDataBuild;
	check(Build);
	return *Build;
}

} // UE::DerivedData

IMPLEMENT_MODULE(UE::DerivedData::Private::FDerivedDataCacheModule, DerivedDataCache);
