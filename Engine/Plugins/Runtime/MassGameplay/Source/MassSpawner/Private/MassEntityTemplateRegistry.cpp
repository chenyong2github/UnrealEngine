// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTemplateRegistry.h"
#include "MassSpawnerTypes.h"
#include "MassEntitySubsystem.h"
#include "Engine/World.h"
#include "VisualLogger/VisualLogger.h"
#include "HAL/IConsoleManager.h"
#include "MassSpawnerSubsystem.h"

namespace FTemplateRegistryHelpers
{
	uint32 CalcHash(const FInstancedStruct& StructInstance)
	{
		const UScriptStruct* Type = StructInstance.GetScriptStruct();
		const uint8* Memory = StructInstance.GetMemory();
		return Type && Memory ? Type->GetStructTypeHash(Memory) : 0;
	}

	void FragmentInstancesToTypes(TArrayView<const FInstancedStruct> FragmentList, TArray<const UScriptStruct*>& OutFragmentTypes)
	{
		for (const FInstancedStruct& Instance : FragmentList)
		{
			// at this point FragmentList is assumed to have no duplicates nor nulls
			OutFragmentTypes.Add(Instance.GetScriptStruct());
		}
	}

	void ResetEntityTemplates(const TArray<FString>& Args, UWorld* InWorld)
	{
		UMassSpawnerSubsystem* SpawnerSystem = UWorld::GetSubsystem<UMassSpawnerSubsystem>(InWorld);
		if (ensure(SpawnerSystem))
		{
			UMassEntityTemplateRegistry& Registry = SpawnerSystem->GetTemplateRegistryInstance();
			Registry.DebugReset();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs EnableCategoryNameCmd(
		TEXT("ai.mass.reset_entity_templates"),
		TEXT("Clears all the runtime information cached by MassEntityTemplateRegistry. Will result in lazily building all entity templates again."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetEntityTemplates)
	);

}

//----------------------------------------------------------------------//
// UMassEntityTemplateRegistry 
//----------------------------------------------------------------------//
TMap<const UScriptStruct*, UMassEntityTemplateRegistry::FStructToTemplateBuilderDelegate> UMassEntityTemplateRegistry::StructBasedBuilders;
TMap<const UClass*, UMassEntityTemplateRegistry::FClassToTemplateBuilderDelegate> UMassEntityTemplateRegistry::ClassBasedBuilders;

void UMassEntityTemplateRegistry::BeginDestroy()
{
	// force release of memory owned by individual templates (especially the hosted InstancedScriptStructs).
	TemplateIDToTemplateMap.Reset();

	Super::BeginDestroy();
}

UWorld* UMassEntityTemplateRegistry::GetWorld() const 
{
	const UObject* Outer = GetOuter();
	return Outer ? Outer->GetWorld() : nullptr;
}

UMassEntityTemplateRegistry::FStructToTemplateBuilderDelegate& UMassEntityTemplateRegistry::FindOrAdd(const UScriptStruct& DataType)
{
	return StructBasedBuilders.FindOrAdd(&DataType);
}

UMassEntityTemplateRegistry::FClassToTemplateBuilderDelegate& UMassEntityTemplateRegistry::FindOrAdd(const UClass& Class)
{
	return ClassBasedBuilders.FindOrAdd(&Class);
}

const FMassEntityTemplate* UMassEntityTemplateRegistry::FindOrBuildStructTemplate(const FInstancedStruct& StructInstance)
{
	// thou shall not call this function on CDO
	check(HasAnyFlags(RF_ClassDefaultObject) == false);

	const UScriptStruct* Type = StructInstance.GetScriptStruct();
	check(Type);
	// 1. Check if we already have the template stored.
	// 2. If not, 
	//	a. build it
	//	b. store it

	const uint32 StructInstanceHash = FTemplateRegistryHelpers::CalcHash(StructInstance);
	const uint32 HashLookup = HashCombine(GetTypeHash(Type->GetFName()), StructInstanceHash);

	FMassEntityTemplateID* TemplateID = LookupTemplateIDMap.Find(HashLookup);

	if (TemplateID != nullptr)
	{
		if (const FMassEntityTemplate* TemplateFound = TemplateIDToTemplateMap.Find(*TemplateID))
		{
			return TemplateFound;
		}
	}

	// this means we don't have an entry for given struct. Let's see if we know how to make one
	FMassEntityTemplate* NewTemplate = nullptr;
	FStructToTemplateBuilderDelegate* Builder = StructBasedBuilders.Find(StructInstance.GetScriptStruct());
	if (Builder)
	{
		if (TemplateID == nullptr)
		{
			// TODO consider removing the need for strings here
			// Use the class name string for the hash here so the hash can be deterministic between client and server
			const uint32 NameStringHash = GetTypeHash(Type->GetName());
			const uint32 Hash = HashCombine(NameStringHash, StructInstanceHash);

			TemplateID = &LookupTemplateIDMap.Add(HashLookup, FMassEntityTemplateID(Hash, EMassEntityTemplateIDType::ScriptStruct));
		}

		NewTemplate = &TemplateIDToTemplateMap.Add(*TemplateID);

		check(NewTemplate);
		NewTemplate->SetTemplateID(*TemplateID);

		BuildTemplateImpl(*Builder, StructInstance, *NewTemplate);
	}
	UE_CVLOG_UELOG(Builder == nullptr, this, LogMassSpawner, Warning, TEXT("Attempting to build a MassAgentTemplate for struct type %s while template builder has not been registered for this type")
		, *GetNameSafe(Type));

	return NewTemplate;
}

bool UMassEntityTemplateRegistry::BuildTemplateImpl(const FStructToTemplateBuilderDelegate& Builder, const FInstancedStruct& StructInstance, FMassEntityTemplate& OutTemplate)
{
	UWorld* World = GetWorld();
	FMassEntityTemplateBuildContext Context(OutTemplate);
	Builder.Execute(World, StructInstance, Context);
	if (ensure(!OutTemplate.IsEmpty())) // need at least one fragment to create an Archetype
	{
		OutTemplate.SetUpProcessors(Context.Handlers, *this);
		InitializeEntityTemplate(OutTemplate);

		UE_VLOG(this, LogMassSpawner, Log, TEXT("Created entity template for %s:\n%s"), *GetNameSafe(StructInstance.GetScriptStruct())
			, *OutTemplate.DebugGetDescription(UWorld::GetSubsystem<UMassEntitySubsystem>(World)));

		return true;
	}
	return false;
}

const FMassEntityTemplate* UMassEntityTemplateRegistry::FindOrBuildInstanceTemplate(const UObject& Instance)
{
	checkf(HasAnyFlags(RF_ClassDefaultObject) == false, TEXT("Thou shall not call %s on UMassEntityTemplateRegistry's CDO"), ANSI_TO_TCHAR(__FUNCTION__));
	check(Instance.GetClass());

	const FMassEntityTemplateID* const TemplateID = LookupTemplateIDMap.Find(GetTypeHash(Instance.GetClass()->GetFName()));

	if (TemplateID != nullptr)
	{
		if (const FMassEntityTemplate* TemplateFound = TemplateIDToTemplateMap.Find(*TemplateID))
		{
			return TemplateFound;
		}
	}

	// TODO this code needs refactoring as its not accounting for instances needing a different hash to classes
	// 2. If not: build it & store it
	return BuildClassTemplate(*Instance.GetClass(), &Instance);
}

const FMassEntityTemplate& UMassEntityTemplateRegistry::FindInstanceTemplateChecked(const UObject& Instance) const
{
	checkf(HasAnyFlags(RF_ClassDefaultObject) == false,  TEXT("Thou shall not call %s on UMassEntityTemplateRegistry's CDO"), ANSI_TO_TCHAR(__FUNCTION__));
	check(Instance.GetClass());

	const FMassEntityTemplateID& TemplateID = LookupTemplateIDMap.FindChecked(GetTypeHash(Instance.GetClass()->GetFName()));

	return TemplateIDToTemplateMap.FindChecked(TemplateID);
}

const FMassEntityTemplate* UMassEntityTemplateRegistry::FindOrBuildClassTemplate(const UClass& Class)
{
	// thou shall not call this function on UMassEntityTemplateRegistry's CDO
	check(HasAnyFlags(RF_ClassDefaultObject) == false);

	// 1. Check if we already have the template stored.
	const FMassEntityTemplateID* const TemplateID = LookupTemplateIDMap.Find(GetTypeHash(Class.GetFName()));

	if (TemplateID != nullptr)
	{
		if (const FMassEntityTemplate* TemplateFound = TemplateIDToTemplateMap.Find(*TemplateID))
		{
			return TemplateFound;
		}
	}

	// 2. If not: build it & store it
	return BuildClassTemplate(Class);
}

const FMassEntityTemplate* UMassEntityTemplateRegistry::BuildClassTemplate(const UClass& Class, const UObject* Instance)
{
	FMassEntityTemplate* EntityTemplate = nullptr;
	// this function will be called when there's been no EntityTemplate created for Class/Instance just yet
	// Let's see if we know how to make one	
	FClassToTemplateBuilderDelegate* Builder = GetBuilderForClass(Class);
	if (Builder)
	{
		TArray<const UMassProcessor*> Initializers;
		
		// TODO consider removing the need for strings here
		// Use the class name string for the hash here, so the hash can be deterministic between client and server
		const uint32 NameStringHash = GetTypeHash(Class.GetName());

		const EMassEntityTemplateIDType TemplateType = (Instance != nullptr && Instance->HasAnyFlags(RF_ClassDefaultObject) == false) ? EMassEntityTemplateIDType::Instance : EMassEntityTemplateIDType::Class;
		const FMassEntityTemplateID TemplateID = LookupTemplateIDMap.Add(GetTypeHash(Class.GetFName()), FMassEntityTemplateID(NameStringHash, TemplateType));

		EntityTemplate = &TemplateIDToTemplateMap.Add(TemplateID);

		check(EntityTemplate);

		EntityTemplate->SetTemplateID(TemplateID);

		FMassEntityTemplateBuildContext BuildContext(*EntityTemplate);
		Builder->Execute(GetWorld(), Class, Instance, BuildContext);

		if (ensureMsgf(!EntityTemplate->IsEmpty(), TEXT("Need at least one fragment to create an Archetype")))
		{
			EntityTemplate->SetUpProcessors(BuildContext.Handlers, *this);
			InitializeEntityTemplate(*EntityTemplate);
		}
	}
	UE_CVLOG_UELOG(Builder == nullptr, this, LogMassSpawner, Warning, TEXT("Attempting to build a MassAgentTemplate for uclass %s (instance %s) while template builder has not been registered for this type")
		, *Class.GetName(), *GetNameSafe(Instance));

	return EntityTemplate;
}

UMassEntityTemplateRegistry::FClassToTemplateBuilderDelegate* UMassEntityTemplateRegistry::GetBuilderForClass(const UClass& Class)
{
	FClassToTemplateBuilderDelegate* Builder = nullptr;
	const UClass* TmpClass = &Class;
	do
	{ 
		Builder = ClassBasedBuilders.Find(TmpClass);
		TmpClass = TmpClass->GetSuperClass();
	} while (Builder == nullptr && TmpClass != nullptr);
	return Builder;
}

void UMassEntityTemplateRegistry::InitializeEntityTemplate(FMassEntityTemplate& OutTemplate) const
{
	// expected to be ensured by the caller
	check(!OutTemplate.IsEmpty());

	UWorld* World = GetWorld();
	// find or create template
	UMassEntitySubsystem* EntitySys = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
	check(EntitySys);

	// Sort anything there is to sort for later comparaison purposes
	OutTemplate.Sort();

	const FArchetypeHandle ArchetypeHandle = EntitySys->CreateArchetype(OutTemplate.GetCompositionDescriptor(), OutTemplate.GetSharedFragmentValues());
	OutTemplate.SetArchetype(ArchetypeHandle);
}

void UMassEntityTemplateRegistry::DebugReset()
{
#if WITH_MASSGAMEPLAY_DEBUG
	LookupTemplateIDMap.Reset();
	TemplateIDToTemplateMap.Reset();
#endif // WITH_MASSGAMEPLAY_DEBUG
}

const FMassEntityTemplate* UMassEntityTemplateRegistry::FindTemplateFromTemplateID(FMassEntityTemplateID TemplateID) const 
{
	return TemplateIDToTemplateMap.Find(TemplateID);
}

FMassEntityTemplate* UMassEntityTemplateRegistry::FindMutableTemplateFromTemplateID(FMassEntityTemplateID TemplateID) 
{
	return TemplateIDToTemplateMap.Find(TemplateID);
}

FMassEntityTemplate& UMassEntityTemplateRegistry::CreateTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID)
{
	checkSlow(!LookupTemplateIDMap.Contains(HashLookup));
	LookupTemplateIDMap.Add(HashLookup, TemplateID);
	checkSlow(!TemplateIDToTemplateMap.Contains(TemplateID));
	FMassEntityTemplate& NewTemplate = TemplateIDToTemplateMap.Add(TemplateID);
	NewTemplate.SetTemplateID(TemplateID);
	return NewTemplate;
}

void UMassEntityTemplateRegistry::DestroyTemplate(const uint32 HashLookup, FMassEntityTemplateID TemplateID)
{
	LookupTemplateIDMap.Remove(HashLookup);
	TemplateIDToTemplateMap.Remove(TemplateID);
}

