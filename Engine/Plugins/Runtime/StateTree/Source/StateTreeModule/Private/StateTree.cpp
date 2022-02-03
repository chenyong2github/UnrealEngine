// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeDelegates.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

UStateTree::UStateTree(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

#if WITH_EDITOR
	FEditorDelegates::PreBeginPIE.AddUObject(this, &UStateTree::OnPIEStarted);
#endif
}

UStateTree::~UStateTree()
{
#if WITH_EDITOR
	FEditorDelegates::PreBeginPIE.RemoveAll(this);
#endif
}

bool UStateTree::IsValidStateTree() const
{
	// Valid tree must have at least one state.
	return States.Num() > 0;
}

#if WITH_EDITOR

void UStateTree::OnPIEStarted(const bool bIsSimulating)
{
	PropertyBindings.ResolvePaths();
	Link();
	InitInstanceStorageType();
}

void UStateTree::ResetBaked()
{
	States.Reset();
	Transitions.Reset();

	Items.Reset();
	Instances.Reset();
	InstanceObjects.Reset();
	InstanceStorageStruct = nullptr;
	InstanceStorageOffsets.Reset();
	InstanceStorageDefaultValue.Reset();
	ExternalDataDescs.Reset();
	PropertyBindings.Reset();

	NumDataViews = 0;
	ExternalDataBaseIndex = 0;
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
	
#if WITH_EDITOR
	InitInstanceStorageType();
#else
	// Item offsets still need to be calculated in non editor target since the struct sizes might be different.
	checkf(InstanceStorageOffsets.Num() == 0, TEXT("RuntimeStorageOffsets is transient and should only be computed once."));

	for (TFieldIterator<FProperty> PropertyIt(InstanceStorageStruct); PropertyIt; ++PropertyIt)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(*PropertyIt);
		checkf(StructProperty, TEXT("InstanceStorageStruct is expected to only contain Struct properties"));
		InstanceStorageOffsets.Emplace(StructProperty->Struct, StructProperty->GetOffset_ForInternal());
	}
#endif // WITH_EDITOR
}

void UStateTree::BeginDestroy()
{
	Super::BeginDestroy();
	
	// Destroy the runtime storage before the UScriptStruct it uses gets removed.
	InstanceStorageDefaultValue.Reset();
}

void UStateTree::ResolvePropertyPaths()
{
	// TODO: find better hook when to call this. Currently this gets called from StateTreeComponent, it should be called once PostLoad() or when entering SIE/PIE in editor.
	PropertyBindings.ResolvePaths();
}

void UStateTree::Link()
{
	FStateTreeLinker Linker;

	ExternalDataBaseIndex = PropertyBindings.GetSourceStructNum();
	Linker.SetExternalDataBaseIndex(ExternalDataBaseIndex);
	
	for (FInstancedStruct& Item : Items)
	{
		if (FStateTreeNodeBase* Node = Item.GetMutablePtr<FStateTreeNodeBase>())
		{
			Linker.SetCurrentInstanceDataType(Node->GetInstanceDataType(), Node->DataViewIndex);
			Node->Link(Linker);
		}
	}

	ExternalDataDescs = Linker.GetExternalDataDescs();
	
	NumDataViews = ExternalDataBaseIndex + ExternalDataDescs.Num();
}

void UStateTree::InitInstanceStorageType()
{
	InstanceStorageStruct = nullptr;
	InstanceStorageOffsets.Reset();
	InstanceStorageDefaultValue.Reset();

	// Check that the items are valid before trying to create the type.
	// The structs can become invalid i.e. because of missing type.
	bool bValid = true;
	for (const FInstancedStruct& Item : Instances)
	{
		if (!Item.IsValid())
		{
			bValid = false;
			break;
		}
	}
	if (!bValid)
	{
		return;
	}

	const FString StructName = GetName() + TEXT("_InstanceStorage");

	// Remove existing struct of same name.
	UScriptStruct* OldStruct = FindObject<UScriptStruct>(this, *StructName);
	if (OldStruct)
	{
		const FString OldStructName = MakeUniqueObjectName(OldStruct->GetOuter(), OldStruct->GetClass(), *FString::Printf(TEXT("%s_TRASH"), *OldStruct->GetName())).ToString();
		OldStruct->SetFlags(RF_NewerVersionExists);
		OldStruct->ClearFlags(RF_Public | RF_Standalone);
		OldStruct->SetStructTrashed(/*bIsTrash*/true);
		OldStruct->Rename(*OldStructName, nullptr, REN_DontCreateRedirectors|REN_ForceNoResetLoaders);
	}

	UScriptStruct* NewStruct = NewObject<UScriptStruct>(this, *StructName, RF_Public);

	if (Schema)
	{
		NewStruct->SetSuperStruct(Schema->GetStorageSuperStruct());
	}

	// Append all evaluators and tasks.
	// Since properties are stored in linked list, add in reverse order so that we retain the correct order in output.
	TArray<FStructProperty*> NewProperties;
	NewProperties.SetNumZeroed(Instances.Num());
	for (int32 ItemIndex = Instances.Num() - 1; ItemIndex >= 0; ItemIndex--)
	{
		const FInstancedStruct& ItemPtr = Instances[ItemIndex];
		UScriptStruct* ItemStruct = const_cast<UScriptStruct*>(ItemPtr.GetScriptStruct());
		FName PropName(FString::Printf(TEXT("%s%d"), *ItemStruct->GetName(), NewProperties.Num()));
		FStructProperty* NewStructProperty = new FStructProperty(NewStruct, PropName, RF_Public);
		NewStructProperty->Struct = ItemStruct;

		NewStruct->AddCppProperty(NewStructProperty);

		NewProperties[ItemIndex] = NewStructProperty;
	}

	// Finalize the struct
	NewStruct->Bind();
	NewStruct->StaticLink(/*bRelinkExistingProperties*/true);

	// Store item offsets for fast struct view creation at runtime.
	TArray<FStateTreeInstanceStorageOffset> NewOffsets;
	for (FStructProperty* StructProperty : NewProperties)
	{
		NewOffsets.Emplace(StructProperty->Struct, StructProperty->GetOffset_ForInternal());
	}
	check(Instances.Num() == NewOffsets.Num());

	// Instantiate default value
	InstanceStorageDefaultValue.InitializeAs(NewStruct);
	for (int i = 0; i < NewOffsets.Num(); i++)
	{
		const FStateTreeInstanceStorageOffset& Item = NewOffsets[i];
		uint8* Dest = InstanceStorageDefaultValue.GetMutableMemory() + Item.Offset;
		Item.Struct->CopyScriptStruct(Dest, Instances[i].GetMemory());
	}

	InstanceStorageOffsets = NewOffsets;
	InstanceStorageStruct = NewStruct;
}
