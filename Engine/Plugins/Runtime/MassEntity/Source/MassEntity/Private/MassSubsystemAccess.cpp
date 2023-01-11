// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSubsystemAccess.h"


namespace UE::Mass::Private
{
	template<typename T>
	TSubclassOf<T> ConvertToSubsystemClass(TSubclassOf<USubsystem> SubsystemClass)
	{
		return *(reinterpret_cast<TSubclassOf<T>*>(&SubsystemClass));
	}
}

//////////////////////////////////////////////////////////////////////////
// FMassSubsystemAccess

FMassSubsystemAccess::FMassSubsystemAccess(UWorld* InWorld)
	: World(InWorld)
{
	Subsystems.AddZeroed(FMassExternalSubsystemBitSet::GetMaxNum());
}

USubsystem* FMassSubsystemAccess::FetchSubsystemInstance(UWorld* World, TSubclassOf<USubsystem> SubsystemClass)
{
	check(SubsystemClass);
	if (SubsystemClass->IsChildOf<UWorldSubsystem>())
	{
		check(World);
		return World->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UWorldSubsystem>(SubsystemClass));
	}
	if (SubsystemClass->IsChildOf<UEngineSubsystem>())
	{
		return GEngine->GetEngineSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UEngineSubsystem>(SubsystemClass));
	}
	if (SubsystemClass->IsChildOf<UGameInstanceSubsystem>())
	{
		check(World && World->GetGameInstance());
		return World->GetGameInstance()->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UGameInstanceSubsystem>(SubsystemClass));
	}
	if (SubsystemClass->IsChildOf<ULocalPlayerSubsystem>())
	{
		check(World);
		return World->GetFirstLocalPlayerFromController()->GetSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<ULocalPlayerSubsystem>(SubsystemClass));
	}
#if WITH_EDITOR
	if (SubsystemClass->IsChildOf<UEditorSubsystem>())
	{
		return GEditor->GetEditorSubsystemBase(UE::Mass::Private::ConvertToSubsystemClass<UEditorSubsystem>(SubsystemClass));
	}
#endif // WITH_EDITOR
	return nullptr;
}

bool FMassSubsystemAccess::CacheSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	if (SubsystemRequirements.IsEmpty())
	{
		return true;
	}

	bool bResult = true;

	for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredConstSubsystems().GetIndexIterator(); It && bResult; ++It)
	{
		bResult = bResult && CacheSubsystem(*It);
	}

	for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredMutableSubsystems().GetIndexIterator(); It && bResult; ++It)
	{
		bResult = bResult && CacheSubsystem(*It);
	}

	if (bResult)
	{
		ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
		MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
	}

	return bResult;
}

bool FMassSubsystemAccess::CacheSubsystem(const uint32 SystemIndex)
{
	if (UNLIKELY(Subsystems.IsValidIndex(SystemIndex) == false))
	{
		Subsystems.AddZeroed(Subsystems.Num() - SystemIndex + 1);
	}

	if (Subsystems[SystemIndex])
	{
		return true;
	}

	const UClass* SubsystemClass = FMassExternalSubsystemBitSet::GetTypeAtIndex(SystemIndex);
	checkSlow(SubsystemClass);

	TSubclassOf<USubsystem> SubsystemSubclass(const_cast<UClass*>(SubsystemClass));
	checkSlow(*SubsystemSubclass);

	if (SubsystemSubclass)
	{
		USubsystem* SystemInstance = FMassSubsystemAccess::FetchSubsystemInstance(World.Get(), SubsystemSubclass);
		Subsystems[SystemIndex] = SystemInstance;
		return SystemInstance != nullptr;
	}

	return false;
}

void FMassSubsystemAccess::SetSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
	MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
}
