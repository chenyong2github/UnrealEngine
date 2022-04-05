// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"

bool UStateTree::IsReadyToRun() const
{
	// Valid tree must have at least one state and valid instance data.
	return States.Num() > 0 && InstanceDataDefaultValue.IsValid();
}

#if WITH_EDITOR
void UStateTree::ResetCompiled()
{
	States.Reset();
	Transitions.Reset();

	Nodes.Reset();
	Instances.Reset();
	InstanceObjects.Reset();
	ExternalDataDescs.Reset();
	PropertyBindings.Reset();

	NumDataViews = 0;
	ExternalDataBaseIndex = 0;

	InstanceDataDefaultValue.Reset();
}

void UStateTree::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTree, Schema))
		{
			UE::StateTree::Delegates::OnSchemaChanged.Broadcast(*this);
		}
	}
}

void UStateTree::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	static const FName SchemaTag(TEXT("Schema"));

	const FString SchemaClassName = Schema ? Schema->GetClass()->GetName() : TEXT("");
	OutTags.Add(FAssetRegistryTag(SchemaTag, SchemaClassName, FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}
#endif // WITH_EDITOR

void UStateTree::PostLoad()
{
	Super::PostLoad();

	Link();
}

void UStateTree::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	// We need to link and rebind property bindings each time a BP is compiled,
	// because property bindings may get invalid, and instance data potentially needs refreshed.
	if (Record.GetUnderlyingArchive().IsModifyingWeakAndStrongReferences())
	{
		Link();
	}
}


void UStateTree::Link()
{
	FStateTreeLinker Linker;

	ExternalDataBaseIndex = PropertyBindings.GetSourceStructNum();
	Linker.SetExternalDataBaseIndex(ExternalDataBaseIndex);
	
	for (FInstancedStruct& Node : Nodes)
	{
		if (FStateTreeNodeBase* NodePtr = Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			Linker.SetCurrentInstanceDataType(NodePtr->GetInstanceDataType(), NodePtr->DataViewIndex);
			NodePtr->Link(Linker);
		}
	}

	ExternalDataDescs = Linker.GetExternalDataDescs();
	
	NumDataViews = ExternalDataBaseIndex + ExternalDataDescs.Num();

	// Initialize the instance data default value.
	// This data will be used to allocate runtime instance on all StateTree users.
	InstanceDataDefaultValue.Reset();
	if (Instances.Num() > 0)
	{
		InstanceDataDefaultValue.Initialize(*this, Instances, InstanceObjects);
	}

	PropertyBindings.ResolvePaths();
}
