// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTree.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "CoreMinimal.h"
#include "StateTreeDelegates.h"

UStateTree::UStateTree(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UStateTree::IsValidStateTree() const
{
	// Valid tree must have at least one state.
	return States.Num() > 0;
}


#if WITH_EDITOR
void UStateTree::ResetBaked()
{
	States.Reset();
	Conditions.Reset();
	Transitions.Reset();

	RuntimeStorageItems.Reset();
	RuntimeStorageStruct = nullptr;
	RuntimeStorageOffsets.Reset();
	RuntimeStorageDefaultValue.Reset();
	ExternalItems.Reset();
	PropertyBindings.Reset();

	NumLinkedItems = 0;
	ExternalItemBaseIndex = 0;
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
	InitRuntimeStorage();
#else
	// Item offsets still need to be calculated in non editor target since the struct sizes might be different.
	checkf(RuntimeStorageOffsets.Num() == 0, TEXT("RuntimeStorageOffsets is transient and should only be computed once."));

	for (TFieldIterator<FProperty> PropertyIt(RuntimeStorageStruct); PropertyIt; ++PropertyIt)
	{
		FStructProperty* StructProperty = CastField<FStructProperty>(*PropertyIt);
		checkf(StructProperty, TEXT("RuntimeStorageStruct is expected to only contain Struct properties"));
		RuntimeStorageOffsets.Emplace(StructProperty->Struct, StructProperty->GetOffset_ForInternal());
	}
#endif // WITH_EDITOR
}

void UStateTree::BeginDestroy()
{
	Super::BeginDestroy();
	
	// Destroy the runtime storage before the UScriptStruct it uses gets removed.
	RuntimeStorageDefaultValue.Reset();
}

void UStateTree::ResolvePropertyPaths()
{
	// TODO: find better hook when to call this. Currently this gets called from StateTreeComponent, it should be called once PostLoad() or when entering SIE/PIE in editor.
	PropertyBindings.ResolvePaths();
}

void UStateTree::Link()
{
	FStateTreeLinker Linker;

	ExternalItemBaseIndex = PropertyBindings.GetSourceStructNum();
	Linker.SetItemBaseIndex(ExternalItemBaseIndex);
	
	for (FInstancedStruct& RuntimeItem : RuntimeStorageItems)
	{
		if (FStateTreeEvaluatorBase* Eval = RuntimeItem.GetMutablePtr<FStateTreeEvaluatorBase>())
		{
			Eval->Link(Linker);
		}
		else if (FStateTreeTaskBase* Task = RuntimeItem.GetMutablePtr<FStateTreeTaskBase>())
		{
			Task->Link(Linker);
		}
	}

	ExternalItems = Linker.GetItemDescs();
	
	NumLinkedItems = ExternalItemBaseIndex + ExternalItems.Num();
}

void UStateTree::InitRuntimeStorage()
{
	RuntimeStorageStruct = nullptr;
	RuntimeStorageOffsets.Reset();
	RuntimeStorageDefaultValue.Reset();

	// Check that the items are valid before trying to create the type.
	// The structs can become invalid i.e. because of missing type.
	bool bValid = true;
	for (const FInstancedStruct& Item : RuntimeStorageItems)
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

	const FString StructName = GetName() + TEXT("_RuntimeStorage");

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
	NewProperties.SetNumZeroed(RuntimeStorageItems.Num());
	for (int32 ItemIndex = RuntimeStorageItems.Num() - 1; ItemIndex >= 0; ItemIndex--)
	{
		const FInstancedStruct& ItemPtr = RuntimeStorageItems[ItemIndex];
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
	TArray<FStateTreeRuntimeStorageItemOffset> NewItemOffsets;
	for (FStructProperty* StructProperty : NewProperties)
	{
		NewItemOffsets.Emplace(StructProperty->Struct, StructProperty->GetOffset_ForInternal());
	}
	check(RuntimeStorageItems.Num() == NewItemOffsets.Num());

	// Instantiate default value
	RuntimeStorageDefaultValue.InitializeAs(NewStruct);
	for (int i = 0; i < NewItemOffsets.Num(); i++)
	{
		const FStateTreeRuntimeStorageItemOffset& Item = NewItemOffsets[i];
		uint8* Dest = RuntimeStorageDefaultValue.GetMutableMemory() + Item.Offset;
		Item.Struct->CopyScriptStruct(Dest, RuntimeStorageItems[i].GetMemory());
	}

	RuntimeStorageOffsets = NewItemOffsets;
	RuntimeStorageStruct = NewStruct;
}
