// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStateTreeSubsystem.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "Engine/Engine.h"


void UMassStateTreeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

FMassStateTreeInstanceHandle UMassStateTreeSubsystem::AllocateInstanceData(const UStateTree* StateTree)
{
	if (StateTree == nullptr)
	{
		return FMassStateTreeInstanceHandle();
	}
	
	int32 Index = 0;
	if (InstanceDataFreelist.Num() > 0)
	{
		Index = InstanceDataFreelist.Pop();
	}
	else
	{
		Index = InstanceDataArray.Num();
		InstanceDataArray.AddDefaulted();
	}

	FMassStateTreeInstanceDataItem& Item = InstanceDataArray[Index];
	
	FStateTreeExecutionContext::InitInstanceData(*this, *StateTree, Item.InstanceData);
	
	return FMassStateTreeInstanceHandle::Make(Index, Item.Generation);
}

void UMassStateTreeSubsystem::FreeInstanceData(const FMassStateTreeInstanceHandle Handle)
{
	if (!IsValidHandle(Handle))
	{
		return;
	}

	FMassStateTreeInstanceDataItem& Item = InstanceDataArray[Handle.GetIndex()];
	Item.InstanceData.Reset();
	Item.Generation++;

	InstanceDataFreelist.Add(Handle.GetIndex());
}
