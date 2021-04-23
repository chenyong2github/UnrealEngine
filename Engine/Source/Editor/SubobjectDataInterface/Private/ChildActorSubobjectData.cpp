// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChildActorSubobjectData.h"

FChildActorSubobjectData::FChildActorSubobjectData(UObject* ContextObject, const FSubobjectDataHandle& ParentHandle, const bool InbIsInheritedSCS)
    : FInheritedSubobjectData(ContextObject, ParentHandle, InbIsInheritedSCS)
{
}

FText FChildActorSubobjectData::GetDisplayName() const
{
	if(const UChildActorComponent* CAC = GetChildActorComponent())
	{
		return CAC->GetClass()->GetDisplayNameText();	
	}
	return FInheritedSubobjectData::GetDisplayName();
}

FText FChildActorSubobjectData::GetActorDisplayText() const
{
	if (const AActor* ChildActor = GetObject<AActor>())
	{
		return ChildActor->GetClass()->GetDisplayNameText();
	}

	return FInheritedSubobjectData::GetActorDisplayText();
}

bool FChildActorSubobjectData::IsChildActor() const
{
	return true;
}

bool FChildActorSubobjectData::CanDelete() const
{
	return false;
}

bool FChildActorSubobjectData::CanReparent() const
{
	// Can't reparent nodes within a child actor subtree
	return false;
}

bool FChildActorSubobjectData::CanDuplicate() const
{
	// Duplication of Child actor components is not allowed.
	return false;
}	