// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeLinker.h"
#include "StateTreeNodeBase.h"

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

	Schema = nullptr;
	Parameters.Reset();
	Nodes.Reset();
	Instances.Reset();
	InstanceObjects.Reset();
	ExternalDataDescs.Reset();
	PropertyBindings.Reset();

	NumDataViews = 0;
	ExternalDataBaseIndex = 0;

	InstanceDataDefaultValue.Reset();
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

	if (!Link())
	{
		UE_LOG(LogStateTree, Error, TEXT("%s failed to link. Asset will not be usable at runtime."), *GetName());	
	}
}

void UStateTree::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	// We need to link and rebind property bindings each time a BP is compiled,
	// because property bindings may get invalid, and instance data potentially needs refreshed.
	if (Record.GetUnderlyingArchive().IsModifyingWeakAndStrongReferences())
	{
		if (!Link())
		{
			UE_LOG(LogStateTree, Error, TEXT("%s failed to link. Asset will not be usable at runtime."), *GetName());	
		}
	}
}

bool UStateTree::Link()
{
	// Initialize the instance data default value.
	// This data will be used to allocate runtime instance on all StateTree users.
	InstanceDataDefaultValue.Reset();
	
	ExternalDataDescs.Reset();
	NumDataViews = 0;
	ExternalDataBaseIndex = 0;

	// Resolves property paths used by bindings a store property pointers
	if (!PropertyBindings.ResolvePaths())
	{
		return false;
	}

	// Resolves nodes references to other StateTree data
	FStateTreeLinker Linker(Schema);
	Linker.SetExternalDataBaseIndex(PropertyBindings.GetSourceStructNum());
	
	for (FInstancedStruct& Node : Nodes)
	{
		if (FStateTreeNodeBase* NodePtr = Node.GetMutablePtr<FStateTreeNodeBase>())
		{
			Linker.SetCurrentInstanceDataType(NodePtr->GetInstanceDataType(), NodePtr->DataViewIndex);
			const bool bLinkSucceeded = NodePtr->Link(Linker);
			if (!bLinkSucceeded || Linker.GetStatus() == EStateTreeLinkerStatus::Failed)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: node '%s' failed to resolve its references."), *GetName(), *NodePtr->StaticStruct()->GetName());
				return false;
			}
		}
	}

	// Link succeeded, setup tree to be ready to run
	ExternalDataBaseIndex = PropertyBindings.GetSourceStructNum();
	ExternalDataDescs = Linker.GetExternalDataDescs();
	NumDataViews = ExternalDataBaseIndex + ExternalDataDescs.Num();

	if (Instances.Num() > 0)
	{
		InstanceDataDefaultValue.Initialize(*this, Instances, InstanceObjects);
	}

	return true;
}
