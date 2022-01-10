// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverProcessor.h"
#include "MassObserverRegistry.h"

//----------------------------------------------------------------------//
// UMassObserverProcessor
//----------------------------------------------------------------------//
UMassObserverProcessor::UMassObserverProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
#if WITH_EDITORONLY_DATA
	bCanShowUpInSettings = false;
#endif // WITH_EDITORONLY_DATA
}

void UMassObserverProcessor::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) && GetClass()->HasAnyClassFlags(CLASS_Abstract) == false && FragmentType != nullptr)
	{
		Register();
	}
}

//----------------------------------------------------------------------//
//  UMassFragmentInitializer
//----------------------------------------------------------------------//
void UMassFragmentInitializer::Register()
{
	check(FragmentType);
	UMassObserverRegistry::GetMutable().RegisterFragmentInitializer(*FragmentType, GetClass());
}

//----------------------------------------------------------------------//
//  UMassFragmentDeinitializer
//----------------------------------------------------------------------//
void UMassFragmentDeinitializer::Register()
{
	check(FragmentType);
	UMassObserverRegistry::GetMutable().RegisterFragmentDeinitializer(*FragmentType, GetClass());
}
