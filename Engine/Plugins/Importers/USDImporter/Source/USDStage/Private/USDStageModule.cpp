// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDStageModule.h"

#include "USDMemory.h"
#include "USDStageActor.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"


class FUsdStageModule : public IUsdStageModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}

	virtual AUsdStageActor& GetUsdStageActor( UWorld* World ) override
	{
		if ( AUsdStageActor* UsdStageActor = FindUsdStageActor( World ) )
		{
			return *UsdStageActor;
		}
		else
		{
			return *( World->SpawnActor< AUsdStageActor >() );
		}
	}

	virtual AUsdStageActor* FindUsdStageActor( UWorld* World ) override
	{
		for ( FActorIterator ActorIterator( World ); ActorIterator; ++ActorIterator )
		{
			if ( AUsdStageActor* UsdStageActor = Cast< AUsdStageActor >( *ActorIterator ) )
			{
				return UsdStageActor;
			}
		}

		return nullptr;
	}
};

IMPLEMENT_MODULE_USD( FUsdStageModule, USDStage );
