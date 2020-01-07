// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosSolverSettings.h"
#include "Chaos/ChaosSolverActor.h"
#include "ChaosSolversModule.h"

UChaosSolverSettings::UChaosSolverSettings()
{
}

UClass* UChaosSolverSettings::GetSolverActorClass() const
{
	UClass* const SolverActorClass = DefaultChaosSolverActorClass.IsValid() ? LoadObject<UClass>(NULL, *DefaultChaosSolverActorClass.ToString()) : nullptr;
	return (SolverActorClass != nullptr) ? SolverActorClass : AChaosSolverActor::StaticClass();
}

void UChaosSolverSettings::UpdateProperty(FProperty* InProperty)
{
	UpdateAllProperties();
}

void UChaosSolverSettings::UpdateAllProperties()
{
}

// internal
void UChaosSolverSettings::RegisterSolverActorProvider()
{
 	FChaosSolversModule* const ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
 	check(ChaosModule);
	ChaosModule->RegisterSolverActorClassProvider(this);
}

#if WITH_EDITOR

void UChaosSolverSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateProperty(PropertyChangedEvent.Property);
}
#endif

void UChaosSolverSettings::PostInitProperties()
{
	Super::PostInitProperties();

	RegisterSolverActorProvider();

	UpdateAllProperties();
}

void UChaosSolverSettings::PostReloadConfig(class FProperty* PropertyThatWasLoaded)
{
	Super::PostReloadConfig(PropertyThatWasLoaded);

	UpdateProperty(PropertyThatWasLoaded);
}
