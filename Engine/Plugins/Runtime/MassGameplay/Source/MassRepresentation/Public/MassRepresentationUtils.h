// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "MassCommonTypes.h"
#include "MassRepresentationTypes.h"

namespace UE::MassRepresentation
{

inline EMassVisibility GetVisibilityFromArchetype(const FMassExecutionContext& Context)
{
	if (Context.DoesArchetypeHaveTag<FMassVisibilityCanBeSeenTag>())
	{
		return EMassVisibility::CanBeSeen;
	}
	if (Context.DoesArchetypeHaveTag<FMassVisibilityCulledByFrustumTag>())
	{
		return EMassVisibility::CulledByFrustum;
	}
	if (Context.DoesArchetypeHaveTag<FMassVisibilityCulledByDistanceTag>())
	{
		return EMassVisibility::CulledByDistance;
	}
	return EMassVisibility::Max;
}

inline const UScriptStruct* GetTagFromVisibility(EMassVisibility Visibility)
{
	switch (Visibility)
	{
		case EMassVisibility::CanBeSeen:
			return FMassVisibilityCanBeSeenTag::StaticStruct();
		case EMassVisibility::CulledByFrustum:
			return FMassVisibilityCulledByFrustumTag::StaticStruct();
		case EMassVisibility::CulledByDistance:
			return FMassVisibilityCulledByDistanceTag::StaticStruct();
		default:
			checkf(false, TEXT("Unsupported visibility Type"));
		case EMassVisibility::Max:
			return nullptr;
	}
}

inline bool IsVisibilityTagSet(const FMassExecutionContext& Context, EMassVisibility Visibility)
{
	switch (Visibility)
	{
		case EMassVisibility::CanBeSeen:
			return Context.DoesArchetypeHaveTag<FMassVisibilityCanBeSeenTag>();
		case EMassVisibility::CulledByFrustum:
			return Context.DoesArchetypeHaveTag<FMassVisibilityCulledByFrustumTag>();
		case EMassVisibility::CulledByDistance:
			return Context.DoesArchetypeHaveTag<FMassVisibilityCulledByDistanceTag>();
		default:
			checkf(false, TEXT("Unsupported visibility Type"));
		case EMassVisibility::Max:
			return false;
	}
}

} // UE::MassRepresentation