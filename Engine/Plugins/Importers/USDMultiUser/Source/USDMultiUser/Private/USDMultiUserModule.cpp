// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

#include "ConcertSyncSettings.h"

#include "USDLog.h"
#include "USDStageActor.h"
#include "USDTransactor.h"

namespace UE
{
	namespace UsdMultiUser
	{
		namespace Private
		{
			void EnableTransactorFilters()
			{
				UConcertSyncConfig* SyncConfig = GetMutableDefault<UConcertSyncConfig>();
				if ( !SyncConfig )
				{
					return;
				}

				FSoftClassPath StageActorClass = AUsdStageActor::StaticClass();
				FSoftClassPath TransactorClass = UUsdTransactor::StaticClass();

				for ( const FTransactionClassFilter& Filter : SyncConfig->IncludeObjectClassFilters )
				{
					if ( Filter.ObjectOuterClass == StageActorClass )
					{
						if ( Filter.ObjectClasses.Contains( TransactorClass ) )
						{
							UE_LOG( LogUsd, Log, TEXT( "Found existing ConcertSync object class filters for UUsdTransactor" ) );
							return;
						}
					}
				}

				FTransactionClassFilter NewFilter;
				NewFilter.ObjectOuterClass = StageActorClass;
				NewFilter.ObjectClasses = { TransactorClass };

				SyncConfig->IncludeObjectClassFilters.Add( NewFilter );

				UE_LOG( LogUsd, Log, TEXT( "Added ConcertSync object class filters for UUsdTransactor" ) );
			}

			void DisableTransactorFilters()
			{
				UConcertSyncConfig* SyncConfig = GetMutableDefault<UConcertSyncConfig>();
				if ( !SyncConfig )
				{
					return;
				}

				FSoftClassPath StageActorClass = AUsdStageActor::StaticClass();
				FSoftClassPath TransactorClass = UUsdTransactor::StaticClass();

				for ( TArray<FTransactionClassFilter>::TIterator It( SyncConfig->IncludeObjectClassFilters ); It; ++It )
				{
					if ( It->ObjectOuterClass == StageActorClass )
					{
						It->ObjectClasses.Remove( TransactorClass );

						UE_LOG( LogUsd, Log, TEXT( "Removed ConcertSync object class filters for UUsdTransactor" ) );
					}

					// Don't assume it's fully our filter
					if ( It->ObjectClasses.Num() == 0 )
					{
						It.RemoveCurrent();
					}
				}
			}
		}
	}
}

/**
 * Module that adds multi user synchronization to the USD Multi User plugin.
 */
class FUsdMultiUserModule : public IModuleInterface
{
private:
	virtual void StartupModule() override
	{
		UE::UsdMultiUser::Private::EnableTransactorFilters();
	}

	virtual void ShutdownModule() override
	{
		// If we're shutting down the classes will already be gone anyway
		if ( !IsEngineExitRequested() )
		{
			UE::UsdMultiUser::Private::DisableTransactorFilters();
		}
	}
};


IMPLEMENT_MODULE(FUsdMultiUserModule, USDMultiUser);
