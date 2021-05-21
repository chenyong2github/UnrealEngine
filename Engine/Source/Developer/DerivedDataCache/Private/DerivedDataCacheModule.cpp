// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheModule.h"

#include "DerivedDataBuild.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCachePrivate.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"

namespace UE::DerivedData::Private
{

static FDerivedDataCacheInterface* GDerivedDataCacheInstance;
static IBuild* GDerivedDataBuildInstance;

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
		FScopeLock Lock(&CreateLock);
		if (!GDerivedDataCacheInstance)
		{
			GDerivedDataCacheInstance = CreateCache();
			check(GDerivedDataCacheInstance);
		}
		return &GDerivedDataCacheInstance;
	}

	IBuild* const* CreateOrGetBuild() final
	{
		FScopeLock Lock(&CreateLock);
		if (!GDerivedDataBuildInstance)
		{
			GDerivedDataBuildInstance = CreateBuild(**CreateOrGetCache());
			check(GDerivedDataBuildInstance);
		}
		return &GDerivedDataBuildInstance;
	}

	void ShutdownModule() final
	{
		delete GDerivedDataBuildInstance;
		GDerivedDataBuildInstance = nullptr;
		delete GDerivedDataCacheInstance;
		GDerivedDataCacheInstance = nullptr;
	}

private:
	FCriticalSection CreateLock;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

} // UE::DerivedData::Private

IMPLEMENT_MODULE(UE::DerivedData::Private::FDerivedDataCacheModule, DerivedDataCache);
