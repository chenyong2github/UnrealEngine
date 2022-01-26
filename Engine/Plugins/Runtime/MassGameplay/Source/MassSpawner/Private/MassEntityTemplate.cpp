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
void FMassEntityTemplate::SetArchetype(const FMassArchetypeHandle& InArchetype)
{
	check(InArchetype.IsValid());
	Archetype = InArchetype;
}

FString FMassEntityTemplate::DebugGetDescription(UMassEntitySubsystem* EntitySubsystem) const
{ 
	FStringOutputDevice Ar;
#if WITH_MASSGAMEPLAY_DEBUG
	Ar.SetAutoEmitLineTerminator(true);

	if (EntitySubsystem)
	{
		Ar += TEXT("Archetype details:\n");
		Ar += DebugGetArchetypeDescription(*EntitySubsystem);
	}
	else
	{
		Ar += TEXT("Composition:\n");
		Composition.DebugOutputDescription(Ar);
	}

#endif // WITH_MASSGAMEPLAY_DEBUG
	return MoveTemp(Ar);
}

FString FMassEntityTemplate::DebugGetArchetypeDescription(UMassEntitySubsystem& EntitySubsystem) const
{
	FStringOutputDevice OutDescription;
#if WITH_MASSGAMEPLAY_DEBUG
	EntitySubsystem.DebugGetStringDesc(Archetype, OutDescription);
#endif // WITH_MASSGAMEPLAY_DEBUG
	return MoveTemp(OutDescription);
}
