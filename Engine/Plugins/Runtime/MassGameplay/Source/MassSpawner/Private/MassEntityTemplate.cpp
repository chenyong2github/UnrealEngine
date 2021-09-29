// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTemplate.h"
#include "VisualLogger/VisualLoggerTypes.h"

DEFINE_ENUM_TO_STRING(EMassEntityTemplateIDType);

//----------------------------------------------------------------------//
//  FMassEntityTemplateID
//----------------------------------------------------------------------//
FString FMassEntityTemplateID::ToString() const
{
	return FString::Printf(TEXT("[%s %d]"), *EnumToString(Type), Hash);
}

//----------------------------------------------------------------------//
//  FMassEntityTemplate
//----------------------------------------------------------------------//
void FMassEntityTemplate::SetArchetype(const FArchetypeHandle& InArchetype)
{
	check(InArchetype.IsValid());
	Archetype = InArchetype;
}

void FMassEntityTemplate::SetUpProcessors(const FMassObjectHandlers& ObjectHandlers, UObject& PipelineOwner)
{
	// @todo do we want to copy the Processors or just use the CDOs?
	InitializationPipeline.InitializeFromArray(ObjectHandlers.Initializers, PipelineOwner);
	DeinitializationPipeline.InitializeFromArray(ObjectHandlers.Deinitializers, PipelineOwner);
}

FString FMassEntityTemplate::DebugGetDescription(UMassEntitySubsystem* EntitySubsystem) const
{ 
	FStringOutputDevice Ar;
#if WITH_MASS_DEBUG
	Ar.SetAutoEmitLineTerminator(true);

	Ar += TEXT("Fragments:\n");
	FragmentCollection.DebugOutputDescription(Ar);

	Ar += TEXT("\nProcessors:\n");
	InitializationPipeline.DebugOutputDescription(Ar);

	if (EntitySubsystem)
	{
		Ar += TEXT("Archetype detais:\n");
		Ar += DebugGetArchetypeDescription(*EntitySubsystem);
	}
#endif // WITH_MASS_DEBUG
	return MoveTemp(Ar);
}

FString FMassEntityTemplate::DebugGetArchetypeDescription(UMassEntitySubsystem& EntitySubsystem) const
{
	FStringOutputDevice OutDescription;
#if WITH_MASS_DEBUG
	EntitySubsystem.DebugGetStringDesc(Archetype, OutDescription);
#endif // WITH_MASS_DEBUG
	return MoveTemp(OutDescription);
}
