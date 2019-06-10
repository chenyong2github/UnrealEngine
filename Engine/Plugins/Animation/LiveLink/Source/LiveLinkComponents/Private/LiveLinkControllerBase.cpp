// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LiveLinkControllerBase.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

AActor* ULiveLinkControllerBase::GetOuterActor() const
{
	for (UObject* Outer = GetOuter(); Outer != nullptr; Outer = Outer->GetOuter())
	{
		if (AActor* Actor = Cast<AActor>(Outer))
		{
			return Actor;
		}
		if (UActorComponent* Component = Cast<UActorComponent>(Outer))
		{
			return Component->GetOwner();
		}
	}

	return nullptr;
}


TSubclassOf<ULiveLinkControllerBase> ULiveLinkControllerBase::GetControllerForRole(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	for (TObjectIterator<UClass> Itt; Itt; ++Itt)
	{
		if (Itt->IsChildOf(ULiveLinkControllerBase::StaticClass()) && !Itt->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			if (Itt->GetDefaultObject<ULiveLinkControllerBase>()->IsRoleSupported(RoleToSupport))
			{
				return TSubclassOf<ULiveLinkControllerBase>(*Itt);
			}
		}
	}
	return TSubclassOf<ULiveLinkControllerBase>();
}
