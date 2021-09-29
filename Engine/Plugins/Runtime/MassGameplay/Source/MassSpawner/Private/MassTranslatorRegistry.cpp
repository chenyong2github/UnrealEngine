// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTranslatorRegistry.h"
#include "MassCommonTypes.h"
#include "MassTranslator.h"

//----------------------------------------------------------------------//
// UMassTranslatorRegistry
//----------------------------------------------------------------------//
UMassTranslatorRegistry::UMassTranslatorRegistry()
{
	// there can be only one!
	check(HasAnyFlags(RF_ClassDefaultObject));
}

void UMassTranslatorRegistry::RegisterFragmentInitializer(const UScriptStruct& FragmentType, TSubclassOf<UMassFragmentInitializer> FragmentInitializerClass)
{
	check(FragmentInitializerClass);
	FragmentInitializersMap.FindOrAdd(&FragmentType) = FragmentInitializerClass;
}

void UMassTranslatorRegistry::RegisterFragmentDestructor(const UScriptStruct& FragmentType, TSubclassOf<UMassFragmentDestructor> FragmentDestructorClass)
{
	check(FragmentDestructorClass);
	FragmentDestructorsMap.FindOrAdd(&FragmentType) = FragmentDestructorClass;
}

const UMassFragmentInitializer* UMassTranslatorRegistry::GetFragmentInitializer(const UScriptStruct& FragmentType) const
{
	const TSubclassOf<UMassFragmentInitializer>* InitializerClass = FragmentInitializersMap.Find(&FragmentType);
	return (InitializerClass && *InitializerClass) ? GetMutableDefault<UMassFragmentInitializer>(*InitializerClass) : nullptr;
	
}
const UMassFragmentDestructor* UMassTranslatorRegistry::GetFragmentDestructor(const UScriptStruct& FragmentType) const
{
	const TSubclassOf<UMassFragmentDestructor>* DestructorClass = FragmentDestructorsMap.Find(&FragmentType);
	return (DestructorClass && *DestructorClass) ? GetMutableDefault<UMassFragmentDestructor>(*DestructorClass) : nullptr;
}
