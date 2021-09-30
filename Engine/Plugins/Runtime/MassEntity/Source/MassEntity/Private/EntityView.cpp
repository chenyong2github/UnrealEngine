// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntityView.h"
#include "MassEntitySubsystem.h"
#include "ArchetypeData.h"


//////////////////////////////////////////////////////////////////////
// FMassEntityView
FMassEntityView::FMassEntityView(const FArchetypeHandle& ArchetypeHandle, FMassEntityHandle InEntity)
{
	Entity = InEntity;
	check(ArchetypeHandle.IsValid());
	Archetype = ArchetypeHandle.DataPtr.Get();
	EntityHandle = Archetype->MakeEntityHandle(Entity);
}

FMassEntityView::FMassEntityView(const UMassEntitySubsystem& EntitySubsystem, FMassEntityHandle InEntity)
{
	Entity = InEntity;
	const FArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);
	check(ArchetypeHandle.IsValid());
	Archetype = ArchetypeHandle.DataPtr.Get();
	EntityHandle = Archetype->MakeEntityHandle(Entity);
}

void* FMassEntityView::GetComponentPtr(const UScriptStruct& ComponentType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	if (const int32* ComponentIndex = Archetype->GetComponentIndex(&ComponentType))
	{
		// failing the below Find means given entity's archetype is missing given ComponentType
		return Archetype->GetComponentData(*ComponentIndex, EntityHandle);
	}
	return nullptr;
}

void* FMassEntityView::GetComponentPtrChecked(const UScriptStruct& ComponentType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	const int32 ComponentIndex = Archetype->GetComponentIndexChecked(&ComponentType);
	return Archetype->GetComponentData(ComponentIndex, EntityHandle);
}

bool FMassEntityView::HasTag(const UScriptStruct& TagType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	return Archetype->HasTagType(&TagType);
}
