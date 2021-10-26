// Copyright Epic Games, Inc. All Rights Reserved.

#include "Group.h"

#include "CADKernel/Core/System.h"

const TCHAR* CADKernel::GroupOriginNames[] = {
	TEXT("Unknown"),
	TEXT("CAD Group"),
	TEXT("CAD Layer"),
	TEXT("CAD Color"),
	nullptr
};


CADKernel::EEntity CADKernel::FGroup::GetGroupType() const
{
	if (Entities.Num() == 0) 
	{
		return EEntity::NullEntity;
	}

	return (*Entities.begin())->GetEntityType();
}

void CADKernel::FGroup::ReplaceEntitiesWithMap(const TMap<TSharedPtr<FEntity>, TSharedPtr<FEntity>>& Map)
{
	for (const TPair<TSharedPtr<FEntity>, TSharedPtr<FEntity>>& Pair : Map)
	{
		if (Entities.Contains(Pair.Key)) 
		{
			Entities.Remove(Pair.Key);
			Entities.Add(Pair.Value);
		}
	}
}

void CADKernel::FGroup::RemoveNonTopologicalEntities()
{
	for (const TSharedPtr<FEntity>& Entity : Entities)
	{
		EEntity Type = Entity->GetEntityType();
		if (Type != EEntity::TopologicalFace && Type != EEntity::TopologicalEdge && Type != EEntity::TopologicalVertex) 
		{
			Entities.Remove(Entity);
		}
	}
}

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FGroup::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
	.Add(TEXT("Origin"), GroupOriginNames[(int8) GetOrigin()])
	.Add(TEXT("Entities"), Entities);
}
#endif

void CADKernel::FGroup::SetName(const FString& Name)
{
	GroupName = Name;
}
