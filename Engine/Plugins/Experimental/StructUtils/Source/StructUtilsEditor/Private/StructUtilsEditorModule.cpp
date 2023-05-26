// Copyright Epic Games, Inc. All Rights Reserved.

#include "StructUtilsEditorModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyBagDetails.h"
#include "PropertyEditorModule.h"
#include "StructUtilsTypes.h"
#include "Engine/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "CoreGlobals.h"
#include "GameFramework/Actor.h"
#include "StructUtilsDelegates.h"
#include "InstancedStruct.h"
#include "InstancedStructContainer.h"
#include "PropertyBag.h"

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

IMPLEMENT_MODULE(FStructUtilsEditorModule, StructUtilsEditor)

void FStructUtilsEditorModule::StartupModule()
{
	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("InstancedStruct", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInstancedStructDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("InstancedPropertyBag", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPropertyBagDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}

void FStructUtilsEditorModule::ShutdownModule()
{
	// Unregister the details customization
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("InstancedStruct");
		PropertyModule.UnregisterCustomPropertyTypeLayout("InstancedPropertyBag");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

namespace UE::StructUtils::Private
{

static bool ContainsInstancedStructProperty(const UClass* Class)
{
	for (TFieldIterator<FStructProperty> It(Class); It; ++It)
	{
		const UScriptStruct* Struct = It->Struct;
		if (Struct == TBaseStructure<FInstancedStruct>::Get())
		{
			return true;
		}
		if (Struct == TBaseStructure<FInstancedStructContainer>::Get())
		{
			return true;
		}
		if (Struct == TBaseStructure<FInstancedPropertyBag>::Get())
		{
			return true;
		}
	}
	return false;
}

static void VisitReferencedObjects(const UUserDefinedStruct* StructToReinstance)
{
	// Helper preference collector, does not collect anything, but makes sure AddStructReferencedObjects() gets called e.g. on instanced struct. 
	class FVisitorReferenceCollector : public FReferenceCollector
	{
	public:
		virtual bool IsIgnoringArchetypeRef() const override { return false; }
		virtual bool IsIgnoringTransient() const override { return false; }

		virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* ReferencingProperty) override
		{
			// Empty
		}
	};

	// Find classes that contain any of the instanced struct types.
	TArray<const UClass*> InstancedStructClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (ContainsInstancedStructProperty(*It))
		{
			InstancedStructClasses.Add(*It);
		}
	}	

	// Find objects that contain any of the instanced struct types.
	TArray<UObject*> SourceObjects;
	for (const UClass* Class : InstancedStructClasses)
	{
		TArray<UObject*> Objects;
		GetObjectsOfClass(Class, Objects);
		SourceObjects.Append(Objects);
	}

	FVisitorReferenceCollector Collector;

	// This sets global variable which read in the AddStructReferencedObjects().
	UE::StructUtils::Private::FStructureToReinstanceScope StructureToReinstanceScope(StructToReinstance);

	for (UObject* Object : SourceObjects)
	{
		// This sets global variable which read in the AddStructReferencedObjects().
		UE::StructUtils::Private::FCurrentReinstanceOuterObjectScope CurrentReinstanceOuterObjectScope(Object);
		
		Collector.AddPropertyReferences(Object->GetClass(), Object);

		// AddPropertyReferences() for objects does not handle ARO, do it manually.
		for (TPropertyValueIterator<FStructProperty> It(Object->GetClass(), Object); It; ++It)
		{
			const UScriptStruct* Struct = It.Key()->Struct;
			void* Instance = const_cast<void*>(It.Value());
			if (Struct && Struct->StructFlags & STRUCT_AddStructReferencedObjects)
			{
				Struct->GetCppStructOps()->AddStructReferencedObjects()(Instance, Collector);
			}
		}
	}
};

}; // UE::StructUtils::Private

void FStructUtilsEditorModule::PreChange(const UUserDefinedStruct* StructToReinstance, FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!StructToReinstance)
	{
		return;
	}

	// Make a duplicate of the existing struct, and point all instances of the struct to point to the duplicate.
	// This is done because the original struct will be changed.
	UUserDefinedStruct* DuplicatedStruct = nullptr;
	{
		const FString ReinstancedName = FString::Printf(TEXT("STRUCT_REINST_%s"), *StructToReinstance->GetName());
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UUserDefinedStruct::StaticClass(), FName(*ReinstancedName));

		TGuardValue<FIsDuplicatingClassForReinstancing, bool> IsDuplicatingClassForReinstancing(GIsDuplicatingClassForReinstancing, true);
		DuplicatedStruct = (UUserDefinedStruct*)StaticDuplicateObject(StructToReinstance, GetTransientPackage(), UniqueName, ~RF_Transactional); 

		DuplicatedStruct->Guid = StructToReinstance->Guid;
		DuplicatedStruct->Bind();
		DuplicatedStruct->StaticLink(true);
		DuplicatedStruct->PrimaryStruct = const_cast<UUserDefinedStruct*>(StructToReinstance);
		DuplicatedStruct->Status = EUserDefinedStructureStatus::UDSS_Duplicate;
		DuplicatedStruct->SetFlags(RF_Transient);
		DuplicatedStruct->AddToRoot();
	}

	UUserDefinedStructEditorData* DuplicatedEditorData = CastChecked<UUserDefinedStructEditorData>(DuplicatedStruct->EditorData);
	DuplicatedEditorData->RecreateDefaultInstance();

	UE::StructUtils::Private::VisitReferencedObjects(DuplicatedStruct);
	
	DuplicatedStruct->RemoveFromRoot();
}

void FStructUtilsEditorModule::PostChange(const UUserDefinedStruct* StructToReinstance, FStructureEditorUtils::EStructureEditorChangeInfo Info)
{
	if (!StructToReinstance)
	{
		return;
	}

	UE::StructUtils::Private::VisitReferencedObjects(StructToReinstance);

	if (UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.IsBound())
	{
		UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Broadcast(*StructToReinstance);
	}
}

#undef LOCTEXT_NAMESPACE
