// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverRegistry.h"

//----------------------------------------------------------------------//
// UMassObserverRegistry
//----------------------------------------------------------------------//
UMassObserverRegistry::UMassObserverRegistry()
{
	// there can be only one!
	check(HasAnyFlags(RF_ClassDefaultObject));
}

void UMassObserverRegistry::RegisterFragmentAddedObserver(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	FragmentInitializersMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(ObserverClass);
}

void UMassObserverRegistry::RegisterFragmentRemovedObserver(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	FragmentDeinitializersMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(ObserverClass);
}

void UMassObserverRegistry::RegisterTagAddedObserver(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	TagAddedObserversMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(ObserverClass);
}

void UMassObserverRegistry::RegisterTagRemovedObserver(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> ObserverClass)
{
	check(ObserverClass);
	TagRemovedObserversMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(ObserverClass);
}

TConstArrayView<TSubclassOf<UMassProcessor>> UMassObserverRegistry::GetFragmentInitializers(const UScriptStruct& FragmentType) const
{
	const FMassProcessorClassCollection* InitializersCollection = FragmentInitializersMap.Find(&FragmentType);
	return InitializersCollection ? InitializersCollection->ClassCollection : TConstArrayView<TSubclassOf<UMassProcessor>>();
}

TConstArrayView<TSubclassOf<UMassProcessor>> UMassObserverRegistry::GetFragmentDeinitializers(const UScriptStruct& FragmentType) const
{
	const FMassProcessorClassCollection* DeinitializersCollection = FragmentDeinitializersMap.Find(&FragmentType);
	return DeinitializersCollection ? DeinitializersCollection->ClassCollection : TConstArrayView<TSubclassOf<UMassProcessor>>();
}
