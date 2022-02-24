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
void UStateTree::ResetBaked()
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
	
	PropertyBindings.ResolvePaths();

	Link();
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
		InstanceDataDefaultValue.Initialize(Instances);
	}
}

int32 UStateTree::GetInstanceDataSize() const
{
	if (!InstanceDataDefaultValue.IsValid() || !InstanceDataDefaultValue.GetLayout().IsValid())
	{
		return 0;
	}

	// TODO: this should count all allocated data (arrays, objects).
	return InstanceDataDefaultValue.GetLayout()->GetLayoutInstanceSize();
}
