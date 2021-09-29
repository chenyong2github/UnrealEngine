// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassTranslator.h"
#include "MassCommonTypes.h"
#include "MassTranslatorRegistry.h"

//----------------------------------------------------------------------//
//  UMassTranslator
//----------------------------------------------------------------------//
UMassTranslator::UMassTranslator()
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
}

void UMassTranslator::AddRequiredTagsToQuery(FLWComponentQuery& EntityQuery)
{
	EntityQuery.AddTagRequirements<ELWComponentPresence::All>(RequiredTags);
}

//----------------------------------------------------------------------//
// UMassFragmentBuilder
//----------------------------------------------------------------------//
UMassFragmentBuilder::UMassFragmentBuilder()
{
	bAutoRegisterWithProcessingPhases = false;
#if WITH_EDITORONLY_DATA
	bCanShowUpInSettings = false;
#endif // WITH_EDITORONLY_DATA
}

void UMassFragmentBuilder::PostInitProperties()
{
	Super::PostInitProperties();

	if (GetClass()->HasAnyClassFlags(CLASS_Abstract) == false && FragmentType != nullptr)
	{
		Register();
	}
}

//----------------------------------------------------------------------//
//  UMassFragmentInitializer
//----------------------------------------------------------------------//
void UMassFragmentInitializer::Register()
{
	if (FragmentType)
	{
		UMassTranslatorRegistry::GetMutable().RegisterFragmentInitializer(*FragmentType, GetClass());
	}
}

//----------------------------------------------------------------------//
//  UMassFragmentDestructor
//----------------------------------------------------------------------//
void UMassFragmentDestructor::Register()
{
	if (FragmentType)
	{
		UMassTranslatorRegistry::GetMutable().RegisterFragmentDestructor(*FragmentType, GetClass());
	}
}