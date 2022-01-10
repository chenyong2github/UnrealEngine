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

void UMassObserverRegistry::RegisterFragmentInitializer(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> FragmentInitializerClass)
{
	check(FragmentInitializerClass);
	FragmentInitializersMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(FragmentInitializerClass);
}

void UMassObserverRegistry::RegisterFragmentDeinitializer(const UScriptStruct& FragmentType, TSubclassOf<UMassProcessor> FragmentDeinitializerClass)
{
	check(FragmentDeinitializerClass);
	FragmentDeinitializersMap.FindOrAdd(&FragmentType).ClassCollection.AddUnique(FragmentDeinitializerClass);
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
