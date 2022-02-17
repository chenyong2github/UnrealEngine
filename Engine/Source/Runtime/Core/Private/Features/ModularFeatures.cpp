// Copyright Epic Games, Inc. All Rights Reserved.

#include "Features/ModularFeatures.h"
#include "Misc/ScopeLock.h"

IModularFeatures& IModularFeatures::Get()
{
	// Singleton instance
	static FModularFeatures ModularFeatures;
	return ModularFeatures;
}

void FModularFeatures::LockModularFeatureList()
{
	ModularFeaturesMapCriticalSection.Lock();
	bModularFeatureListLocked = true;
}

void FModularFeatures::UnlockModularFeatureList()
{
	ModularFeaturesMapCriticalSection.Unlock();
	bModularFeatureListLocked = false;
}

int32 FModularFeatures::GetModularFeatureImplementationCount( const FName Type )
{
	ensureMsgf(IsInGameThread() || bModularFeatureListLocked, TEXT("IModularFeature counting is not thread-safe unless wrapped with LockModularFeatureList/UnlockModularFeatureList"));

	return ModularFeaturesMap.Num( Type );
}

IModularFeature* FModularFeatures::GetModularFeatureImplementation( const FName Type, const int32 Index )
{
	ensureMsgf(IsInGameThread() || bModularFeatureListLocked, TEXT("IModularFeature fetching is not thread-safe unless wrapped with LockModularFeatureList/UnlockModularFeatureList"));

	IModularFeature* ModularFeature = nullptr;

	int32 CurrentIndex = 0;
	for( TMultiMap< FName, class IModularFeature* >::TConstKeyIterator It( ModularFeaturesMap, Type ); It; ++It )
	{
		if( Index == CurrentIndex )
		{
			ModularFeature = It.Value();
			break;
		}

		++CurrentIndex;
	}

	check( ModularFeature != nullptr );
	return ModularFeature;
}


void FModularFeatures::RegisterModularFeature( const FName Type, IModularFeature* ModularFeature )
{
	FScopeLock ScopeLock(&ModularFeaturesMapCriticalSection);

	ModularFeaturesMap.AddUnique( Type, ModularFeature );
	ModularFeatureRegisteredEvent.Broadcast( Type, ModularFeature );
}


void FModularFeatures::UnregisterModularFeature( const FName Type, IModularFeature* ModularFeature )
{
	FScopeLock ScopeLock(&ModularFeaturesMapCriticalSection);

	ModularFeaturesMap.RemoveSingle( Type, ModularFeature );
	ModularFeatureUnregisteredEvent.Broadcast( Type, ModularFeature );
}

IModularFeatures::FOnModularFeatureRegistered& FModularFeatures::OnModularFeatureRegistered()
{
	return ModularFeatureRegisteredEvent;
}

IModularFeatures::FOnModularFeatureUnregistered& FModularFeatures::OnModularFeatureUnregistered()
{
	return ModularFeatureUnregisteredEvent;
}
