// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeLinker.h"
#include "StateTreeNodeBase.h"

const FGuid FStateTreeCustomVersion::GUID(0x28E21331, 0x501F4723, 0x8110FA64, 0xEA10DA1E);
FCustomVersionRegistration GRegisterStateTreeCustomVersion(FStateTreeCustomVersion::GUID, FStateTreeCustomVersion::LatestVersion, TEXT("StateTreeAsset"));

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
	NamedExternalDataDescs.Reset();
	PropertyBindings.Reset();

	NumDataViews = 0;
	ExternalDataBaseIndex = 0;

	InstanceDataDefaultValue.Reset();
}

void UStateTree::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	const FString SchemaClassName = Schema ? Schema->GetClass()->GetName() : TEXT("");
	OutTags.Add(FAssetRegistryTag(UE::StateTree::SchemaTag, SchemaClassName, FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}
#endif // WITH_EDITOR

void UStateTree::PostLoad()
{
	Super::PostLoad();

	const int32 CurrentVersion = GetLinkerCustomVersion(FStateTreeCustomVersion::GUID);

	if (CurrentVersion < FStateTreeCustomVersion::SharedInstanceData)
	{
		ResetLinked();
		UE_LOG(LogStateTree, Error, TEXT("%s: StateTree compiled data in older format. Please recompile the StateTree asset."), *GetName());
		return;
	}
	
	if (!Link())
	{
		UE_LOG(LogStateTree, Error, TEXT("%s failed to link. Asset will not be usable at runtime."), *GetName());	
	}
}

void UStateTree::Serialize(FStructuredArchiveRecord Record)
{
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FStateTreeCustomVersion::GUID);
	
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

void UStateTree::ResetLinked()
{
	InstanceDataDefaultValue.Reset();
	SharedInstanceData.Reset();
	
	ExternalDataDescs.Reset();
	NumDataViews = 0;
	ExternalDataBaseIndex = 0;
}

bool UStateTree::Link()
{
	// Initialize the instance data default value.
	// This data will be used to allocate runtime instance on all StateTree users.
	ResetLinked();
	
	// Update property bag structs before resolving binding.
	TArrayView<FStateTreeBindableStructDesc> SourceStructs = PropertyBindings.GetSourceStructs();
	TArrayView<FStateTreePropCopyBatch> CopyBatches = PropertyBindings.GetCopyBatches();

	if (DefaultParametersDataViewIndex != INDEX_NONE)
	{
		SourceStructs[DefaultParametersDataViewIndex].Struct = Parameters.GetPropertyBagStruct();
	}
	
	for (const FCompactStateTreeState& State : States)
	{
		if (State.Type == EStateTreeStateType::Subtree)
		{
			if (State.ParameterInstanceIndex == MAX_uint16
				|| State.ParameterDataViewIndex == MAX_uint16)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: Data for state '%s' is malformed. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString());
				return false;
			}

			// Subtree is a bind source, update bag struct.
			const FCompactStateTreeParameters& Params = Instances[State.ParameterInstanceIndex].GetMutable<FCompactStateTreeParameters>();
			FStateTreeBindableStructDesc& Desc = SourceStructs[State.ParameterDataViewIndex];
			Desc.Struct = Params.Parameters.GetPropertyBagStruct();
		}
		else if (State.Type == EStateTreeStateType::Linked && State.LinkedState.IsValid())
		{
			const FCompactStateTreeState& LinkedState = States[State.LinkedState.Index];

			if (State.ParameterInstanceIndex == MAX_uint16
				|| LinkedState.ParameterInstanceIndex == MAX_uint16)
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: Data for state '%s' is malformed. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString());
				return false;
			}

			const FCompactStateTreeParameters& Params = Instances[State.ParameterInstanceIndex].GetMutable<FCompactStateTreeParameters>();

			// Check that the bag in linked state matches.
			const FCompactStateTreeParameters& LinkedStateParams = Instances[LinkedState.ParameterInstanceIndex].GetMutable<FCompactStateTreeParameters>();

			if (LinkedStateParams.Parameters.GetPropertyBagStruct() != Params.Parameters.GetPropertyBagStruct())
			{
				UE_LOG(LogStateTree, Error, TEXT("%s: The parameters on state '%s' does not match the linked state parameters in state '%s'. Please recompile the StateTree asset."), *GetName(), *State.Name.ToString(), *LinkedState.Name.ToString());
				return false;
			}

			FStateTreePropCopyBatch& Batch = CopyBatches[Params.BindingsBatch.Index];
			Batch.TargetStruct.Struct = Params.Parameters.GetPropertyBagStruct();
		}
	}
	
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

	if (Instances.Num() > 0 || InstanceObjects.Num() > 0)
	{
		InstanceDataDefaultValue.Initialize(*this, Instances, InstanceObjects);
	}

	if (SharedInstances.Num())
	{
		SharedInstanceData.Initialize(*this, SharedInstances, SharedInstanceObjects);
	}

	
	return true;
}
