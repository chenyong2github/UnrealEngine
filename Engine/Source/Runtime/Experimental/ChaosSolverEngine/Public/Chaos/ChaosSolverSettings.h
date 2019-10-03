// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "ChaosSolversModule.h"
#include "ChaosSolverSettings.generated.h"

/** 
 * Settings class for the Chaos Solver
 */

UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "Chaos Solver"))
class CHAOSSOLVERENGINE_API UChaosSolverSettings : public UDeveloperSettings, public IChaosSolverActorClassProvider
{
	GENERATED_BODY()

public:

	UChaosSolverSettings();

	// IChaosSolverActorClassProvider interface
	virtual UClass* GetSolverActorClass() const;

	/** The class to use when auto-creating a default chaos solver actor */
	UPROPERTY(config, noclear, EditAnywhere, Category = GameInstance, meta = (MetaClass = "ChaosSolverActor"))
	FSoftClassPath DefaultChaosSolverActorClass;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PostInitProperties() override;
	virtual void PostReloadConfig(class UProperty* PropertyThatWasLoaded) override;

private: 

	// Chaos can't read the properties here as it doesn't depend on engine, the
	// following functions push changes to the chaos module as properties change
	void UpdateProperty(UProperty* InProperty);
	void UpdateAllProperties();
	void RegisterSolverActorProvider();
	//////////////////////////////////////////////////////////////////////////
};
