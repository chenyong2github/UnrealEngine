// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntityView.h"
#include "MassEntitySubsystem.h"
#include "ArchetypeData.h"


//////////////////////////////////////////////////////////////////////
// FEntityView
FEntityView::FEntityView(const FArchetypeHandle& ArchetypeHandle, FLWEntity InEntity)
{
	Entity = InEntity;
	check(ArchetypeHandle.IsValid());
	Archetype = ArchetypeHandle.DataPtr.Get();
	EntityHandle = Archetype->MakeEntityHandle(Entity);
}

FEntityView::FEntityView(const UMassEntitySubsystem& EntitySubsystem, FLWEntity InEntity)
{
	Entity = InEntity;
	const FArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);
	check(ArchetypeHandle.IsValid());
	Archetype = ArchetypeHandle.DataPtr.Get();
	EntityHandle = Archetype->MakeEntityHandle(Entity);
}

void* FEntityView::GetComponentPtr(const UScriptStruct& ComponentType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	if (const int32* ComponentIndex = Archetype->GetComponentIndex(&ComponentType))
	{
		// failing the below Find means given entity's archetype is missing given ComponentType
		return Archetype->GetComponentData(*ComponentIndex, EntityHandle);
	}
	return nullptr;
}

void* FEntityView::GetComponentPtrChecked(const UScriptStruct& ComponentType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	const int32 ComponentIndex = Archetype->GetComponentIndexChecked(&ComponentType);
	return Archetype->GetComponentData(ComponentIndex, EntityHandle);
}

bool FEntityView::HasTag(const UScriptStruct& TagType) const
{
	checkSlow(Archetype && EntityHandle.IsValid());
	return Archetype->HasTagType(&TagType);
}
