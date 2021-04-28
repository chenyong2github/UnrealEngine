// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubobjectDataSubsystem.h"
#include "UnrealEngine.h"				// GEngine for subsystems
#include "Editor/EditorEngine.h"		// FActorLabelUtilities
#include "ComponentAssetBroker.h"		// FComponentAssetBrokerage
#include "ChildActorSubobjectData.h"
#include "InheritedSubobjectData.h"
#include "BaseSubobjectDataFactory.h"
#include "ChildSubobjectDataFactory.h"
#include "InheritedSubobjectDataFactory.h"

#include "BlueprintEditorSettings.h"	// bHideConstructionScriptComponentsInDetailsView
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"	// IsClassAllowed
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ClassViewerFilter.h"
#include "AssetRegistryModule.h"
#include "ScopedTransaction.h"

#include "GameProjectGenerationModule.h"	// Adding new component classes
#include "GameProjectUtils.h"
#include "SourceCodeNavigation.h"
#include "AddToProjectConfig.h"

#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"

#include "Kismet2/CompilerResultsLog.h"		// Adding compiler errors to nodes that have their variables deleted
#include "K2Node_ComponentBoundEvent.h"

#include "Engine/SCS_Node.h"		// #TODO_BH  We need to remove this when the actual subobject refactor happens

#define LOCTEXT_NAMESPACE "SubobjectDataInterface"

DEFINE_LOG_CATEGORY_STATIC(LogSubobjectSubsystem, Log, All);

//////////////////////////////////////////////
// USubobjectDataSubsystem

void USubobjectDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FactoryManager = new FSubobjectFactoryManager();
	check(FactoryManager);

	FactoryManager->RegisterFactory(MakeShared<FBaseSubobjectDataFactory>());
	FactoryManager->RegisterFactory(MakeShared<FChildSubobjectDataFactory>());
	FactoryManager->RegisterFactory(MakeShared<FInheritedSubobjectDataFactory>());
}

void USubobjectDataSubsystem::Deinitialize()
{
	if(ensure(FactoryManager))
	{
		delete FactoryManager;
		FactoryManager = nullptr;
	}
}

USubobjectDataSubsystem* USubobjectDataSubsystem::Get()
{
	return GEngine->GetEngineSubsystem<USubobjectDataSubsystem>();
}

void USubobjectDataSubsystem::K2_GatherSubobjectDataForBlueprint(UBlueprint* Context, TArray<FSubobjectDataHandle>& OutArray)
{
	// Return the current CDO that was last generated for the class
	if (Context != nullptr && Context->GeneratedClass != nullptr)
	{
		GatherSubobjectData(Context->GeneratedClass->GetDefaultObject(), OutArray);
	}
}

void USubobjectDataSubsystem::K2_GatherSubobjectDataForInstance(AActor* Context, TArray<FSubobjectDataHandle>& OutArray)
{
	return GatherSubobjectData(Context, OutArray);
}

void USubobjectDataSubsystem::GatherSubobjectData(UObject* Context, TArray<FSubobjectDataHandle>& OutArray)
{
	if (!Context)
	{
		UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Could not gather subobject data, there was no context given!"));
		return;
	}

	const AActor* ActorContext = Cast<AActor>(Context);
	if(!ActorContext)
	{
		UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Could not gather subobject data, the given context was not an actor!"));
		return;
	}
	
	OutArray.Reset();

	FSubobjectDataHandle RootActorHandle = CreateSubobjectData(Context);
	OutArray.Add(RootActorHandle);
	const FSubobjectData* RootActorDataPtr = RootActorHandle.GetData();
	check(RootActorDataPtr);

	// Is the root an actor? 
	const bool bIsInstanced = RootActorDataPtr->IsInstancedActor();

	// If the actor is not instanced, then we are dealing with a BP class
	if (!bIsInstanced)
	{
		// get all the components
		TArray<UActorComponent*> Components;
		ActorContext->GetComponents(Components);

		USceneComponent* RootComponent = ActorContext->GetRootComponent();
		FSubobjectDataHandle RootComponentHandle;
		if (RootComponent != nullptr)
		{
			Components.Remove(RootComponent);
			RootComponentHandle = FactoryCreateSubobjectDataWithParent(RootComponent, RootActorHandle);
			OutArray.Add(RootComponentHandle);
		}

		// The components array will be populated with any natively added components
		// from the constructor/ObjectInitalizer
		// These components will all be attached to the root component if it exists
		for (UActorComponent* Component : Components)
		{
			FSubobjectDataHandle NewComponentHandle = FactoryCreateSubobjectDataWithParent(
					/* Context = */ Component,
					/* ParentHandle = */ RootComponentHandle.IsValid() ? RootComponentHandle : RootActorHandle
			);
			ensureMsgf(NewComponentHandle.IsValid(), TEXT("Gathering of native components failed!"));
			OutArray.Add(NewComponentHandle);
		}

		// If it's a Blueprint-generated class, also get the inheritance stack
		TArray<UBlueprint*> ParentBPStack;
		UBlueprint::GetBlueprintHierarchyFromClass(ActorContext->GetClass(), ParentBPStack);

		// Add the full SCS tree node hierarchy (including SCS nodes inherited from parent blueprints)
		for (int32 StackIndex = ParentBPStack.Num() - 1; StackIndex >= 0; --StackIndex)
		{
			if (ParentBPStack[StackIndex]->SimpleConstructionScript != nullptr)
			{
				const TArray<USCS_Node*>& SCS_RootNodes = ParentBPStack[StackIndex]->SimpleConstructionScript->GetRootNodes();
				for (int32 NodeIndex = 0; NodeIndex < SCS_RootNodes.Num(); ++NodeIndex)
				{
					USCS_Node* SCS_Node = SCS_RootNodes[NodeIndex];
					check(SCS_Node != nullptr);

					// Create a new subobject whose parent 
					FSubobjectDataHandle NewHandle;
					FSubobjectData* NewData = nullptr;
					// If this SCS node has a parent component, then add it to that
					if (SCS_Node->ParentComponentOrVariableName != NAME_None)
					{
						USceneComponent* ParentComponent = SCS_Node->GetParentComponentTemplate(ParentBPStack[0]);
						if (ParentComponent != nullptr)
						{
							// The parent component will already be in the out array if it is valid, so search for it
							FSubobjectDataHandle ParentHandle;
							for(const FSubobjectDataHandle& CurHandle : OutArray)
							{
								if(TSharedPtr<FSubobjectData> DataPtr = CurHandle.GetSharedDataPtr())
								{
									if (DataPtr->GetComponentTemplate() == ParentComponent)
									{
										ParentHandle = CurHandle;
										break;
									}
								}
							}
							
							if (ensure(ParentHandle.IsValid()))
							{
								NewHandle = FactoryCreateInheritedBpSubobject(SCS_Node, RootComponentHandle.IsValid() ? RootComponentHandle : RootActorHandle, /* bIsInherited = */ StackIndex > 0, OutArray);
							}
						}
					}
					// Otherwise add a node parents to the root actor
					else
					{
						NewHandle = FactoryCreateInheritedBpSubobject(SCS_Node, RootComponentHandle.IsValid() ? RootComponentHandle : RootActorHandle, /* bIsInherited = */ StackIndex > 0, OutArray);
					}
					NewData = NewHandle.GetData();
					
					// Only necessary to do the following for inherited nodes (StackIndex > 0).
					if (NewData && StackIndex > 0)
					{
						// This call creates ICH override templates for the current Blueprint. Without this, the parent node
						// search above can fail when attempting to match an inherited node in the tree via component template.
						NewData->GetObjectForBlueprint(ParentBPStack[0]);
						for (FSubobjectDataHandle ChildHandle : NewData->GetChildrenHandles())
						{
							FSubobjectData* ChildData = ChildHandle.GetData();
							if (ensure(ChildData != nullptr))
							{
								ChildData->GetObjectForBlueprint(ParentBPStack[0]);
							}
						}
					}
				}
			}
		}
	}
	// Otherwise, this is an actor instance in a level
	else
	{
		// Get the full set of instanced components
		TSet<UActorComponent*> ComponentsToAdd(ActorContext->GetComponents());

		const bool bHideConstructionScriptComponentsInDetailsView = GetDefault<UBlueprintEditorSettings>()->bHideConstructionScriptComponentsInDetailsView;
		auto ShouldAddInstancedActorComponent = [bHideConstructionScriptComponentsInDetailsView](UActorComponent* ActorComp, USceneComponent* ParentSceneComp)
		{
			// Exclude nested DSOs attached to BP-constructed instances, which are not mutable.
			return (ActorComp != nullptr
				&& (!ActorComp->IsVisualizationComponent())
				&& (ActorComp->CreationMethod != EComponentCreationMethod::UserConstructionScript || !bHideConstructionScriptComponentsInDetailsView)
				&& (ParentSceneComp == nullptr || !ParentSceneComp->IsCreatedByConstructionScript() || !ActorComp->HasAnyFlags(RF_DefaultSubObject)))
				&& (ActorComp->CreationMethod != EComponentCreationMethod::Native || FComponentEditorUtils::GetPropertyForEditableNativeComponent(ActorComp));
		};

		// Filter the components by their visibility
		for (TSet<UActorComponent*>::TIterator It(ComponentsToAdd.CreateIterator()); It; ++It)
		{
			UActorComponent* ActorComp = *It;
			USceneComponent* SceneComp = Cast<USceneComponent>(ActorComp);
			USceneComponent* ParentSceneComp = SceneComp != nullptr ? SceneComp->GetAttachParent() : nullptr;
			if (!ShouldAddInstancedActorComponent(ActorComp, ParentSceneComp))
			{
				It.RemoveCurrent();
			}
		}

		TFunction<void(USceneComponent*, FSubobjectDataHandle)> AddInstancedComponentsRecursive = [&](USceneComponent* Component, FSubobjectDataHandle ParentHandle)
		{
			if (Component != nullptr)
			{
				for (USceneComponent* ChildComponent : Component->GetAttachChildren())
				{
					if (ComponentsToAdd.Contains(ChildComponent) && ChildComponent->GetOwner() == Component->GetOwner())
					{
						ComponentsToAdd.Remove(ChildComponent);
						FSubobjectDataHandle NewParentHandle = FactoryCreateSubobjectDataWithParent(ChildComponent, ParentHandle);
						OutArray.Add(NewParentHandle);
						
						AddInstancedComponentsRecursive(ChildComponent, NewParentHandle);
					}
				}
			}
		};

		USceneComponent* RootComponent = ActorContext->GetRootComponent();

		// Add the root component first
		if (RootComponent != nullptr)
		{
			// We want this to be first every time, so remove it from the set of components that will be added later
			ComponentsToAdd.Remove(RootComponent);

			// Add the root component first
			FSubobjectDataHandle RootCompHandle = FactoryCreateSubobjectDataWithParent(RootComponent, RootActorHandle);
			OutArray.Add(RootCompHandle);

			// Recursively add
			AddInstancedComponentsRecursive(RootComponent, RootCompHandle);
		}

		// Sort components by type (always put scene components first in the tree)
		ComponentsToAdd.Sort([](const UActorComponent& A, const UActorComponent& /* B */)
		{
			return A.IsA<USceneComponent>();
		});

		// Now add any remaining instanced owned components not already added above. This will first add any
		// unattached scene components followed by any instanced non-scene components owned by the Actor instance.
		for (UActorComponent* ActorComp : ComponentsToAdd)
		{
			// Create new subobject data with the original data as their parent.
			OutArray.Add(FactoryCreateSubobjectDataWithParent(ActorComp, RootActorHandle));
		}
	}
}

void USubobjectDataSubsystem::FindAllSubobjectData(FSubobjectData* InData, TSet<FSubobjectData*>& OutVisited) const
{
	if(!InData || OutVisited.Contains(InData))
	{
		return;
	}
	
	OutVisited.Add(InData);
		
	for (FSubobjectDataHandle ChildHandle : InData->GetChildrenHandles())
	{
		FindAllSubobjectData(ChildHandle.GetData(), OutVisited);
	}
}

bool USubobjectDataSubsystem::K2_FindSubobjectDataFromHandle(const FSubobjectDataHandle& Handle, FSubobjectData& OutData) const
{
	const FSubobjectData* Data = Handle.GetData();
	if (Data)
	{
		OutData = *Data;
		return true;
	}
	else
	{
		return false;
	}
}

FSubobjectDataHandle USubobjectDataSubsystem::FindHandleForObject(const FSubobjectDataHandle& Context, const UObject* ObjectToFind, UBlueprint* BPContext /* = nullptr */) const
{
	if(Context.IsValid())
	{
		if(const UActorComponent* ComponentToFind = Cast<UActorComponent>(ObjectToFind))
		{
			// If the given component instance is not already an archetype object
			if(BPContext && !ComponentToFind->IsTemplate())
			{
				// Get the component owner's class object
				check(ComponentToFind->GetOwner() != nullptr);
				UClass* OwnerClass = ComponentToFind->GetOwner()->GetClass();

				// If the given component is one that's created during Blueprint construction
				if (ComponentToFind->IsCreatedByConstructionScript())
				{
					// Check the entire Class hierarchy for the node
					TArray<UBlueprintGeneratedClass*> ParentBPStack;
					UBlueprint::GetBlueprintHierarchyFromClass(OwnerClass, ParentBPStack);

					for(int32 StackIndex = ParentBPStack.Num() - 1; StackIndex >= 0; --StackIndex)
					{
						USimpleConstructionScript* ParentSCS = ParentBPStack[StackIndex] ? ParentBPStack[StackIndex]->SimpleConstructionScript.Get() : nullptr;
						if (ParentSCS)
						{
							// Attempt to locate an SCS node with a variable name that matches the name of the given component
							for (USCS_Node* SCS_Node : ParentSCS->GetAllNodes())
							{
								check(SCS_Node != nullptr);
								if (SCS_Node->GetVariableName() == ComponentToFind->GetFName())
								{
									// We found a match; redirect to the component archetype instance that may be associated with a tree node
									ObjectToFind = SCS_Node->ComponentTemplate;
									break;
								}
							}
						}
					}
				}
				else
				{
					// Get the class default object
					const AActor* CDO = Cast<AActor>(OwnerClass->GetDefaultObject());
					if (CDO)
					{
						// Iterate over the Components array and attempt to find a component with a matching name
						for (UActorComponent* ComponentTemplate : CDO->GetComponents())
						{
							if (ComponentTemplate && ComponentTemplate->GetFName() == ComponentToFind->GetFName())
							{
								// We found a match; redirect to the component archetype instance that may be associated with a tree node
								ObjectToFind = ComponentTemplate;
								break;
							}
						}
					}
				}
			}
		}

		TSet<FSubobjectData*> OutData;
		FindAllSubobjectData(Context.GetSharedDataPtr().Get(), OutData);

		for(FSubobjectData* CurData : OutData)
		{
			if(CurData->GetObject() == ObjectToFind)
			{
				return CurData->GetHandle();
			}
		}
	}
	
	return FSubobjectDataHandle::InvalidHandle;
}

class FComponentClassParentFilter : public IClassViewerFilter
{
public:
	FComponentClassParentFilter(const TSubclassOf<UActorComponent>& InComponentClass) : ComponentClass(InComponentClass) {}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InClass->IsChildOf(ComponentClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(ComponentClass);
	}

	TSubclassOf<UActorComponent> ComponentClass;
};

typedef FComponentClassParentFilter FNativeComponentClassParentFilter;

class FBlueprintComponentClassParentFilter : public FComponentClassParentFilter
{
public:
	FBlueprintComponentClassParentFilter(const TSubclassOf<UActorComponent>& InComponentClass) : FComponentClassParentFilter(InComponentClass) {}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return FComponentClassParentFilter::IsClassAllowed(InInitOptions, InClass, InFilterFuncs) && FKismetEditorUtilities::CanCreateBlueprintOfClass(InClass);
	}
};

UClass* USubobjectDataSubsystem::CreateNewCPPComponent(TSubclassOf<UActorComponent> ComponentClass, const FString& NewClassPath, const FString& NewClassName)
{
	UClass* NewClass = nullptr;

	if (ComponentClass && !NewClassName.IsEmpty() && !NewClassPath.IsEmpty())
	{
		FString HeaderFilePath;
		FString CppFilePath;
		FText FailReason;

		TSharedPtr<FModuleContextInfo> SelectedModuleInfo = MakeShareable(new FModuleContextInfo());
		FNewClassInfo NewClassInfo(ComponentClass);

		// Attempt to add new source files to the project
		const TSet<FString>& DisallowedHeaderNames = FSourceCodeNavigation::GetSourceFileDatabase().GetDisallowedHeaderNames();
		const GameProjectUtils::EAddCodeToProjectResult AddCodeResult = 
			GameProjectUtils::AddCodeToProject(
			NewClassName, 
			NewClassPath, 
			*SelectedModuleInfo, 
			NewClassInfo, 
			DisallowedHeaderNames, 
			/*out*/ HeaderFilePath,
			/*out*/ CppFilePath,
			/*out*/ FailReason
		);

		if (AddCodeResult == GameProjectUtils::EAddCodeToProjectResult::Succeeded)
		{			
			FString AddedClassName = FString::Printf(TEXT("/Script/%s.%s"), *SelectedModuleInfo->ModuleName, *NewClassName);
			NewClass = LoadClass<UActorComponent>(nullptr, *AddedClassName, nullptr, LOAD_None, nullptr);
		}
		else
		{
			UE_LOG(LogSubobjectSubsystem, Error, TEXT("Failed to create a new CPP component: %s"), *FailReason.ToString())
		}
	}

	return NewClass;
}

UClass* USubobjectDataSubsystem::CreateNewBPComponent(TSubclassOf<UActorComponent> ComponentClass, const FString& NewClassPath, const FString& NewClassName)
{
	UClass* NewClass = nullptr;
	if (ComponentClass && !NewClassName.IsEmpty() && !NewClassPath.IsEmpty())
	{
		const FString PackagePath = NewClassPath / NewClassName;

		if (UPackage* Package = CreatePackage(*PackagePath))
		{
			// Create and init a new Blueprint
			UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(ComponentClass, Package, FName(*NewClassName), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (NewBP)
			{
				// Notify the asset registry
				FAssetRegistryModule::AssetCreated(NewBP);

				// Mark the package dirty...
				Package->MarkPackageDirty();
				NewClass = NewBP->GeneratedClass;
			}
		}
	}
	return NewClass;
}

static void SaveSCSCurrentState(USimpleConstructionScript* SCSObj)
{
	if (SCSObj)
	{
		SCSObj->SaveToTransactionBuffer();
	}
}

static void ConformTransformRelativeToParent(USceneComponent* SceneComponentTemplate, const USceneComponent* ParentSceneComponent)
{
	// If we find a match, calculate its new position relative to the scene root component instance in its current scene
	FTransform ComponentToWorld(SceneComponentTemplate->GetRelativeRotation(), SceneComponentTemplate->GetRelativeLocation(), SceneComponentTemplate->GetRelativeScale3D());
	FTransform ParentToWorld = (SceneComponentTemplate->GetAttachSocketName() != NAME_None) ? ParentSceneComponent->GetSocketTransform(SceneComponentTemplate->GetAttachSocketName(), RTS_World) : ParentSceneComponent->GetComponentToWorld();
	FTransform RelativeTM = ComponentToWorld.GetRelativeTransform(ParentToWorld);

	// Store new relative location value (if not set to absolute)
	if (!SceneComponentTemplate->IsUsingAbsoluteLocation())
	{
		SceneComponentTemplate->SetRelativeLocation_Direct(RelativeTM.GetTranslation());
	}

	// Store new relative rotation value (if not set to absolute)
	if (!SceneComponentTemplate->IsUsingAbsoluteRotation())
	{
		SceneComponentTemplate->SetRelativeRotation_Direct(RelativeTM.Rotator());
	}

	// Store new relative scale value (if not set to absolute)
	if (!SceneComponentTemplate->IsUsingAbsoluteScale())
	{
		SceneComponentTemplate->SetRelativeScale3D_Direct(RelativeTM.GetScale3D());
	}
}

FSubobjectDataHandle USubobjectDataSubsystem::FindSceneRootForSubobject(const FSubobjectDataHandle& InHandle) const
{
	if(!InHandle.IsValid())
	{
		return FSubobjectDataHandle::InvalidHandle;
	}
	
	FSubobjectDataHandle ActorHandle = InHandle;
	FSubobjectData* ActorData = ActorHandle.GetData();
	
	// If the given handle is not an actor, then we can walk it's parent chain back up until we find it
	while (ActorData && !ActorData->IsActor())
	{
		ActorHandle = ActorData->GetParentHandle();
		ActorData = ActorHandle.GetData();
	}

	// If the given handle is an actor, then we have to use it's first scene component
	if(ensure(ActorData && ActorData->IsActor()))
	{
		TArray<FSubobjectDataHandle> ChildHandles = ActorData->GetChildrenHandles();
		for(const FSubobjectDataHandle& ChildHandle : ChildHandles)
		{
			FSubobjectData* ChildData = ChildHandle.GetData();
			if(ChildData && ChildData->IsDefaultSceneRoot())
			{
				return ChildHandle;
			}
		}
	}

	// Last resort is to just return the last known actor handle
	return ActorHandle;
}

FSubobjectDataHandle USubobjectDataSubsystem::FindParentForNewSubobject(const UObject* NewSubobject, const FSubobjectDataHandle& SelectedParent)
{
	FSubobjectDataHandle TargetParentHandle = SelectedParent;
	check(TargetParentHandle.IsValid());
	FSubobjectData* TargetParentData = TargetParentHandle.GetData();

	// If the current selection belongs to a child actor template, move the target to its outer component node.
	while(TargetParentHandle.IsValid() && TargetParentData->IsChildActor())
	{
		TargetParentHandle = TargetParentData->GetParentHandle();
		TargetParentData = TargetParentHandle.GetData();
		check(TargetParentData);
	}
	
	if(const USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewSubobject))
	{
		if(TargetParentData)
		{
			if (TargetParentData->IsActor())
			{
				AActor* TargetActor = TargetParentData->GetMutableObject<AActor>();
				check(TargetActor);
				USceneComponent* TargetRootComp = TargetActor ? TargetActor->GetDefaultAttachComponent() : nullptr;
				// A component should attach to the target scene component 
				FSubobjectDataHandle RootComponentHandle = FactoryCreateSubobjectDataWithParent(
					TargetRootComp ? Cast<UObject>(TargetRootComp) : Cast<UObject>(TargetActor),
					TargetParentData->GetHandle()
				);
				
				if (RootComponentHandle.IsValid())
				{
					TargetParentHandle = RootComponentHandle;
					const USceneComponent* CastTargetToSceneComponent = Cast<USceneComponent>(TargetParentData->GetComponentTemplate());
					if (CastTargetToSceneComponent == nullptr || !NewSceneComponent->CanAttachAsChild(CastTargetToSceneComponent, NAME_None))
					{
						TargetParentHandle = FindSceneRootForSubobject(SelectedParent); // Default to SceneRoot
					}
				}
			}
			else if(TargetParentData->IsComponent())
			{
				const USceneComponent* CastTargetToSceneComponent = Cast<USceneComponent>(TargetParentData->GetComponentTemplate());
				if (CastTargetToSceneComponent == nullptr || !NewSceneComponent->CanAttachAsChild(CastTargetToSceneComponent, NAME_None))
				{
					TargetParentHandle = FindSceneRootForSubobject(SelectedParent); // Default to SceneRoot
				}
			}
		}
		else
		{
			TargetParentHandle = FindSceneRootForSubobject(SelectedParent);
		}
	}
	else
	{
		if (TargetParentData->IsValid())
		{
			TargetParentHandle = FindSceneRootForSubobject(SelectedParent);
		}
		else
		{
			TargetParentHandle = SelectedParent;
		}

		check(TargetParentHandle.IsValid() && TargetParentData->IsActor());
	}
	
	return TargetParentHandle;
}

FSubobjectDataHandle USubobjectDataSubsystem::AddNewSubobject(const FAddNewSubobjectParams& Params, FText& FailReason)
{
	FSubobjectDataHandle NewDataHandle = FSubobjectDataHandle::InvalidHandle;
	// Check for valid parent
	if (!Params.ParentHandle.IsValid() || !Params.NewClass)
	{
		return NewDataHandle;
	}

	UClass* NewClass = Params.NewClass;
	UObject* Asset = Params.AssetOverride;
	const FSubobjectDataHandle& ParentObjHandle = Params.ParentHandle;

	if (NewClass->ClassWithin && NewClass->ClassWithin != UObject::StaticClass())
	{
		FailReason =  LOCTEXT("AddComponentFailed", "Cannot add components that have \"Within\" markup");
		return NewDataHandle;
	}
	
	FName TemplateVariableName;
	const USCS_Node* SCSNode = Cast<const USCS_Node>(Asset);
	UActorComponent* ComponentTemplate = (SCSNode ? ToRawPtr(SCSNode->ComponentTemplate) : Cast<UActorComponent>(Asset));

	if (SCSNode)
	{
		TemplateVariableName = SCSNode->GetVariableName();
		Asset = nullptr;
	}
	else if (ComponentTemplate)
	{
		Asset = nullptr;
	}
	
	if(Params.BlueprintContext)
	{
		UBlueprint* Blueprint = Params.BlueprintContext;
		check(Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr);
		Blueprint->Modify();
		SaveSCSCurrentState(Blueprint->SimpleConstructionScript);
		UActorComponent* NewComponent = nullptr;
		
		// Defer Blueprint class regeneration and tree updates until after we copy any object properties from a source template.
        const bool bMarkBlueprintModified = false;
		
		FName NewVariableName;
		if (ComponentTemplate)
		{
			if (!TemplateVariableName.IsNone())
			{
				NewVariableName = TemplateVariableName;
			}
			else
			{
				FString TemplateName = ComponentTemplate->GetName();
				NewVariableName = (TemplateName.EndsWith(USimpleConstructionScript::ComponentTemplateNameSuffix) 
                                    ? FName(*TemplateName.LeftChop(USimpleConstructionScript::ComponentTemplateNameSuffix.Len()))
                                    : ComponentTemplate->GetFName());
			}
		}
		else if (Asset)
		{
			NewVariableName = *FComponentEditorUtils::GenerateValidVariableNameFromAsset(Asset, nullptr);
		}

		USCS_Node* NewSCSNode = Blueprint->SimpleConstructionScript->CreateNode(NewClass, NewVariableName);
		NewSCSNode->Modify();
		NewComponent = NewSCSNode->ComponentTemplate;
		// stuff from AddNewNode
		// Assign this new node to an override asset if there is one
		if(Asset)
		{
			FComponentAssetBrokerage::AssignAssetToComponent(NewComponent, Asset);
		}

		FSubobjectDataHandle TargetAttachmentHandle = FindParentForNewSubobject(NewComponent, ParentObjHandle);
		FSubobjectData* TargetAttachment = TargetAttachmentHandle.GetData();
		check(TargetAttachment);
		
		// Create a new subobject data set with this component. Use the SCS node here and the subobject data
		// will correctly associate the component template
		NewDataHandle = FactoryCreateSubobjectDataWithParent(NewSCSNode, TargetAttachment->GetHandle());

		// Actually add this new node to the SimpleConstructionScript on the blueprint
		AttachSubobject(TargetAttachment->GetHandle(), NewDataHandle);
		
		// Potentially adjust variable names for any child blueprints
        const FName VariableName = NewSCSNode->GetVariableName();
        if(VariableName != NAME_None)
        {
        	FBlueprintEditorUtils::ValidateBlueprintChildVariables(Blueprint, VariableName);
        }
		
		if (ComponentTemplate)
		{
			//Serialize object properties using write/read operations.
			TArray<uint8> SavedProperties;
			FObjectWriter Writer(ComponentTemplate, SavedProperties);
			FObjectReader(NewComponent, SavedProperties);
			NewComponent->UpdateComponentToWorld();
		}

		if (Params.bConformTransformToParent)
		{
			if (USceneComponent* AsSceneComp = Cast<USceneComponent>(NewComponent))
			{
				// This problem because it is using the generated bp class as a parent
				if (const USceneComponent* ParentSceneComp = CastChecked<USceneComponent>(TargetAttachment->GetComponentTemplate(), ECastCheckedType::NullAllowed))
				{
					ConformTransformRelativeToParent(AsSceneComp, ParentSceneComp);
				}
			}
		}

		// Wait until here to mark as structurally modified because we don't want any RerunConstructionScript() calls
		// to happen until AFTER we've serialized properties from the source object.
		if (!Params.bSkipMarkBlueprintModified)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}
	else	// Not in a BP context
	{
		FSubobjectData* ParentObjData = ParentObjHandle.GetData();

		check(ParentObjData);
		
		// If this is a component template, then we can just duplicate it
		if(ComponentTemplate)
		{
			UActorComponent* NewComponent = FComponentEditorUtils::DuplicateComponent(ComponentTemplate);
			// Create a new subobject data set with this component
			NewDataHandle = FactoryCreateSubobjectDataWithParent(NewComponent, ParentObjHandle);
		}
		else if(AActor* ActorInstance = ParentObjData->GetMutableActorContext())
		{
			ActorInstance->Modify();

			// Create an appropriate name for the new component
			FName NewComponentName = NAME_None;
			if (Asset)
			{
				NewComponentName = *FComponentEditorUtils::GenerateValidVariableNameFromAsset(Asset, ActorInstance);
			}
			else
			{
				NewComponentName = *FComponentEditorUtils::GenerateValidVariableName(NewClass, ActorInstance);
			}
			
			// Get the set of owned components that exists prior to instancing the new component.
			TInlineComponentArray<UActorComponent*> PreInstanceComponents;
			ActorInstance->GetComponents(PreInstanceComponents);

			// Construct the new component and attach as needed
			UActorComponent* NewInstanceComponent = NewObject<UActorComponent>(ActorInstance, NewClass, NewComponentName, RF_Transactional);
			
			// Do Scene Attachment if this new Component is a USceneComponent
			if (USceneComponent* NewSceneComponent = Cast<USceneComponent>(NewInstanceComponent))
			{
				if(ParentObjData->IsDefaultSceneRoot())
				{
					ActorInstance->SetRootComponent(NewSceneComponent);
				}
				else
				{
					USceneComponent* AttachTo = Cast<USceneComponent>(ParentObjData->GetMutableComponentTemplate());
					if (AttachTo == nullptr)
					{
						AttachTo = ActorInstance->GetRootComponent();
					}
					check(AttachTo != nullptr);

					// Make sure that the mobility of the new scene component is such that we can attach it
					if (AttachTo->Mobility == EComponentMobility::Movable)
					{
						NewSceneComponent->Mobility = EComponentMobility::Movable;
					}
					else if (AttachTo->Mobility == EComponentMobility::Stationary && NewSceneComponent->Mobility == EComponentMobility::Static)
					{
						NewSceneComponent->Mobility = EComponentMobility::Stationary;
					}

					NewSceneComponent->AttachToComponent(AttachTo, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				}
			}

			// If the component was created from/for a particular asset, assign it now
			if (Asset)
			{
				FComponentAssetBrokerage::AssignAssetToComponent(NewInstanceComponent, Asset);
			}

			// Add to SerializedComponents array so it gets saved
			ActorInstance->AddInstanceComponent(NewInstanceComponent);
			NewInstanceComponent->OnComponentCreated();
			NewInstanceComponent->RegisterComponent();

			// Register any new components that may have been created during construction of the instanced component, but were not explicitly registered.
			TInlineComponentArray<UActorComponent*> PostInstanceComponents;
			ActorInstance->GetComponents(PostInstanceComponents);
			for (UActorComponent* ActorComponent : PostInstanceComponents)
			{
				if (!ActorComponent->IsRegistered() && ActorComponent->bAutoRegister && !ActorComponent->IsPendingKill() && !PreInstanceComponents.Contains(ActorComponent))
				{
					ActorComponent->RegisterComponent();
				}
			}

			// Rerun construction scripts
			ActorInstance->RerunConstructionScripts();

			// If the running the construction script destroyed the new node, don't create an entry for it
			if (!NewInstanceComponent->IsPendingKill())
			{
				// Create a new subobject data set with this component
				NewDataHandle = FactoryCreateSubobjectDataWithParent(NewInstanceComponent, ParentObjHandle);
			}
		}
		else
		{
			FailReason =  LOCTEXT("AddComponentFailed_Inherited", "Cannot add components within an Inherited heirarchy");
		}
	}
	
	return NewDataHandle;
}

int32 USubobjectDataSubsystem::DeleteSubobjects(const FSubobjectDataHandle& ContextHandle, const TArray<FSubobjectDataHandle>& SubobjectsToDelete, FSubobjectDataHandle& OutComponentToSelect, UBlueprint* BPContext/* = nullptr*/)
{
	int32 NumDeletedSubobjects = 0;

	if(!ContextHandle.IsValid() || SubobjectsToDelete.Num() == 0)
	{
		return NumDeletedSubobjects;
	}
	const FSubobjectData* ContextData = ContextHandle.GetData();
	UObject* ContextObj = ContextData->GetMutableObject();
	check(ContextObj);

	ContextObj->Modify();
	
	if (BPContext)
	{
		for (const FSubobjectDataHandle& Handle : SubobjectsToDelete)
		{
			if (Handle.IsValid())
			{
				if (const FSubobjectData* Data = Handle.GetData())
				{
					USCS_Node* SCS_Node = Data->GetSCSNode();
					
					if (SCS_Node)
					{
						USimpleConstructionScript* SCS = SCS_Node->GetSCS();
						check(SCS != nullptr && BPContext == SCS->GetBlueprint());
						BPContext->Modify();
						SaveSCSCurrentState(SCS);
					
						FBlueprintEditorUtils::RemoveVariableNodes(BPContext, Data->GetVariableName());

						// If there are any Bound Component events for this property then give them compiler errors
						TArray<UK2Node_ComponentBoundEvent*> EventNodes;
						FKismetEditorUtilities::FindAllBoundEventsForComponent(BPContext, SCS_Node->GetVariableName(), EventNodes);
						if (EventNodes.Num() > 0)
						{
							// Find any dynamic delegate nodes and give a compiler error for each that is problematic
							FCompilerResultsLog LogResults;
							FMessageLog MessageLog("BlueprintLog");

							// Add a compiler error for each bound event node
							for (UK2Node_ComponentBoundEvent* Node : EventNodes)
							{
								LogResults.Error(*LOCTEXT("RemoveBoundEvent_Error", "The component that @@ was bound to has been deleted! This node is no longer valid").ToString(), Node);
							}

							// Notify the user that these nodes are no longer valid
							MessageLog.NewPage(LOCTEXT("RemoveBoundEvent_Error_Label", "Removed Owner of Component Bound Event"));
							MessageLog.AddMessages(LogResults.Messages);
							MessageLog.Notify(LOCTEXT("RemoveBoundEvent_Error_Msg", "Removed Owner of Component Bound Event"));

							// Focus on the first node that we found
							FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(EventNodes[0]);
						}

						// Remove node from SCS tree
						SCS->RemoveNodeAndPromoteChildren(SCS_Node);
						++NumDeletedSubobjects;
						
						// Clear the delegate
						SCS_Node->SetOnNameChanged(FSCSNodeNameChanged());

						// on removal, since we don't move the template from the GeneratedClass (which we shouldn't, as it would create a 
						// discrepancy with existing instances), we rename it instead so that we can re-use the name without having to compile  
						// (we still have a problem if they attempt to name it to what ever we choose here, but that is unlikely)
						// note: skip this for the default scene root; we don't actually destroy that node when it's removed, so we don't need the template to be renamed.
						if (!Data->IsDefaultSceneRoot() && SCS_Node->ComponentTemplate != nullptr)
						{
							const FName TemplateName = SCS_Node->ComponentTemplate->GetFName();
							const FString RemovedName = SCS_Node->GetVariableName().ToString() + TEXT("_REMOVED_") + FGuid::NewGuid().ToString();

							SCS_Node->ComponentTemplate->Modify();
							SCS_Node->ComponentTemplate->Rename(*RemovedName, /*NewOuter =*/nullptr, REN_DontCreateRedirectors);

							TArray<UObject*> ArchetypeInstances;
							auto DestroyArchetypeInstances = [&ArchetypeInstances, &RemovedName](UActorComponent* ComponentTemplate)
							{
								ComponentTemplate->GetArchetypeInstances(ArchetypeInstances);
								for (UObject* ArchetypeInstance : ArchetypeInstances)
								{
									if (!ArchetypeInstance->HasAllFlags(RF_ArchetypeObject | RF_InheritableComponentTemplate))
									{
										CastChecked<UActorComponent>(ArchetypeInstance)->DestroyComponent();
										ArchetypeInstance->Rename(*RemovedName, nullptr, REN_DontCreateRedirectors);
									}
								}
							};

							DestroyArchetypeInstances(SCS_Node->ComponentTemplate);

							if (BPContext)
							{
								// Children need to have their inherited component template instance of the component renamed out of the way as well
								TArray<UClass*> ChildrenOfClass;
								GetDerivedClasses(BPContext->GeneratedClass, ChildrenOfClass);

								for (UClass* ChildClass : ChildrenOfClass)
								{
									UBlueprintGeneratedClass* BPChildClass = CastChecked<UBlueprintGeneratedClass>(ChildClass);

									if (UActorComponent* Component = (UActorComponent*)FindObjectWithOuter(BPChildClass, UActorComponent::StaticClass(), TemplateName))
									{
										Component->Modify();
										Component->Rename(*RemovedName, /*NewOuter =*/nullptr, REN_DontCreateRedirectors);

										DestroyArchetypeInstances(Component);
									}
								}
							}
						}
					}
				}
			}
		}

		// Will call UpdateTree as part of OnBlueprintChanged handling
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BPContext);
	}
	else	// This is an actor instance
	{
		TArray<UActorComponent*> ComponentsToDelete;
		for(const FSubobjectDataHandle& Handle : SubobjectsToDelete)
		{
			if(Handle.IsValid())
			{
				if(const FSubobjectData* Data = Handle.GetData())
				{
					ComponentsToDelete.Add(Data->GetMutableComponentTemplate());		
				}		
			}
		}
		UActorComponent* ActorComponentToSelect = nullptr;
		NumDeletedSubobjects = FComponentEditorUtils::DeleteComponents(ComponentsToDelete, ActorComponentToSelect);

		// Find the handle that matches with this actor component given our current context
	}

	return NumDeletedSubobjects;
}

int32 USubobjectDataSubsystem::DeleteSubobjects(const FSubobjectDataHandle& ContextHandle, const TArray<FSubobjectDataHandle>& SubobjectsToDelete, UBlueprint* BPContext /*= nullptr*/)
{
	FSubobjectDataHandle Dummy;
	return DeleteSubobjects(ContextHandle, SubobjectsToDelete, Dummy, BPContext);
}

int32 USubobjectDataSubsystem::DeleteSubobject(const FSubobjectDataHandle& ContextHandle, const FSubobjectDataHandle& SubobjectToDelete, UBlueprint* BPContext)
{
	TArray<FSubobjectDataHandle> Handles { SubobjectToDelete };
	return DeleteSubobjects(ContextHandle, Handles, BPContext);
}

bool USubobjectDataSubsystem::RenameSubobject(const FSubobjectDataHandle& Handle, const FText& InNewName)
{
	FText OutErrorMessage;
	if(!IsValidRename(Handle, InNewName, OutErrorMessage))
	{
		return false;	
	}

	const FSubobjectData* Data = Handle.GetData();
	if(!Data)
	{
		return false;
	}

	// For root actor nodes
	if (AActor* Actor = Data->GetMutableObject<AActor>())
	{
		if (Actor->IsActorLabelEditable() && !InNewName.ToString().Equals(Actor->GetActorLabel(), ESearchCase::CaseSensitive))
		{
			const FScopedTransaction Transaction(LOCTEXT("SCSEditorRenameActorTransaction", "Rename Actor"));
			FActorLabelUtilities::RenameExistingActor(Actor, InNewName.ToString());
			return true;
		}
	}
	
	// For instanced components
	if(UActorComponent* ComponentInstance = Data->GetMutableComponentTemplate())
	{
		if(Data->IsInstancedComponent())
		{
			ERenameFlags RenameFlags = REN_DontCreateRedirectors;
	
			// name collision could occur due to e.g. our archetype being updated and causing a conflict with our ComponentInstance:
			FString NewNameAsString = InNewName.ToString();
			if(StaticFindObject(UObject::StaticClass(), ComponentInstance->GetOuter(), *NewNameAsString) == nullptr)
			{
				ComponentInstance->Rename(*NewNameAsString, nullptr, RenameFlags);
			}
			return true;
		}
		else if(UBlueprint* BP = Data->GetBlueprint())
		{
			FName ValidatedNewName;
			FString DesiredName = InNewName.ToString();
			
			// Is this desired name the same as what is already there? If so then don't bother
			USCS_Node* SCSNode = Data->GetSCSNode();
			if(SCSNode && SCSNode->GetVariableName().ToString().Equals(DesiredName))
			{
				return true;
			}
			
			if (FKismetNameValidator(BP).IsValid(DesiredName) == EValidatorResult::Ok)
			{
				ValidatedNewName = FName(DesiredName);
			}
			else
			{
				ValidatedNewName = FBlueprintEditorUtils::FindUniqueKismetName(BP, DesiredName);
			}
			
			FBlueprintEditorUtils::RenameComponentMemberVariable(BP, SCSNode, ValidatedNewName);
			return true;
		}
	}
	
	return false;
}

bool USubobjectDataSubsystem::ReparentSubobject(const FReparentSubobjectParams& Params, const FSubobjectDataHandle& ToReparentHandle)
{
	TArray<FSubobjectDataHandle> Handles;
	Handles.Add(ToReparentHandle);
	
	return ReparentSubobjects(Params, Handles);
}

bool USubobjectDataSubsystem::MakeNewSceneRoot(const FSubobjectDataHandle& Context, const FSubobjectDataHandle& DroppedNewSceneRootHandle, UBlueprint* Blueprint)
{
	if(!ensure(Context.IsValid()) || !ensure(DroppedNewSceneRootHandle.IsValid()))
	{
		UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Failed to make new scene root: Invalid context or scene root handle!"));
		return false;
	}

	FSubobjectData* DroppedNewSceneRootData = DroppedNewSceneRootHandle.GetData();
	
	FSubobjectDataHandle StartingRootHandle = FindSceneRootForSubobject(Context);
	FSubobjectData* StartingRootData = StartingRootHandle.GetData();
	// Remember whether or not we're replacing the default scene root
	bool bWasDefaultSceneRoot = StartingRootData && StartingRootData->IsDefaultSceneRoot();
	
	FSubobjectDataHandle OldSceneRoot = FSubobjectDataHandle::InvalidHandle;
	
	if(Blueprint)
	{
		check(Blueprint && Blueprint->SimpleConstructionScript);

		// Clone the component if it's being dropped into a different subobject tree
		if(DroppedNewSceneRootData->GetBlueprint() != Blueprint)
		{
			UActorComponent* ComponentTemplate = DroppedNewSceneRootData->GetMutableComponentTemplate();
			check(ComponentTemplate);
			FAddNewSubobjectParams Params;
			Params.NewClass = ComponentTemplate->GetClass();
			Params.BlueprintContext = Blueprint;
			Params.AssetOverride = nullptr;
			Params.ParentHandle = Context;
			FText FailReason;
			
			// Note: This will mark the Blueprint as structurally modified
			FSubobjectDataHandle ClonedHandle = AddNewSubobject(Params, FailReason);
			check(ClonedHandle.IsValid());
			UActorComponent* ClonedComponent = ClonedHandle.GetData()->GetMutableComponentTemplate();
			check(ClonedComponent);

			// Serialize object properties using write/read operations.
			TArray<uint8> SavedProperties;
			FObjectWriter Writer(ComponentTemplate, SavedProperties);
			FObjectReader(ClonedComponent, SavedProperties);

			DroppedNewSceneRootData = ClonedHandle.GetData();
			check(DroppedNewSceneRootData->IsValid());
		}

		if(DroppedNewSceneRootData->GetParentHandle().IsValid() && DroppedNewSceneRootData->GetBlueprint() == Blueprint)
		{
			// If the associated component template is a scene component, reset its transform since it will now become the root
			if(USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(DroppedNewSceneRootData->GetMutableComponentTemplate()))
			{
				// Save current state
				SceneComponentTemplate->Modify();

				// Reset the attach socket name
				SceneComponentTemplate->SetupAttachment(SceneComponentTemplate->GetAttachParent(), NAME_None);
				
				if(USCS_Node* SCS_Node = DroppedNewSceneRootData->GetSCSNode())
				{
					SCS_Node->Modify();
					SCS_Node->AttachToName = NAME_None;
				}

				// Cache the current relative location and rotation values (for propagation)
				const FVector OldRelativeLocation = SceneComponentTemplate->GetRelativeLocation();
				const FRotator OldRelativeRotation = SceneComponentTemplate->GetRelativeRotation();

				// Reset the relative transform (location and rotation only; scale is preserved)
				SceneComponentTemplate->SetRelativeLocation(FVector::ZeroVector);
				SceneComponentTemplate->SetRelativeRotation(FRotator::ZeroRotator);

				// Propagate the root change & detachment to any instances of the template (done within the context of the current transaction)
				TArray<UObject*> ArchetypeInstances;
				SceneComponentTemplate->GetArchetypeInstances(ArchetypeInstances);
				FDetachmentTransformRules DetachmentTransformRules(EDetachmentRule::KeepWorld, EDetachmentRule::KeepWorld, EDetachmentRule::KeepRelative, true);
				for (int32 InstanceIndex = 0; InstanceIndex < ArchetypeInstances.Num(); ++InstanceIndex)
				{
					USceneComponent* SceneComponentInstance = Cast<USceneComponent>(ArchetypeInstances[InstanceIndex]);
					if (SceneComponentInstance != nullptr)
					{
						// Detach from root (keeping world transform, except for scale)
						SceneComponentInstance->DetachFromComponent(DetachmentTransformRules);

						// Propagate the default relative location & rotation reset from the template to the instance
						FComponentEditorUtils::ApplyDefaultValueChange(SceneComponentInstance, SceneComponentInstance->GetRelativeLocation_DirectMutable(), OldRelativeLocation, SceneComponentTemplate->GetRelativeLocation());
						FComponentEditorUtils::ApplyDefaultValueChange(SceneComponentInstance, SceneComponentInstance->GetRelativeRotation_DirectMutable(), OldRelativeRotation, SceneComponentTemplate->GetRelativeRotation());

						// Must also reset the root component here, so that RerunConstructionScripts() will cache the correct root component instance data
						AActor* Owner = SceneComponentInstance->GetOwner();
						if (Owner)
						{
							Owner->Modify();
							Owner->SetRootComponent(SceneComponentInstance);
						}
					}
				}
			}
			
			// Remove the dropped node from its existing parent
			DetachSubobject(DroppedNewSceneRootData->GetParentHandle(), DroppedNewSceneRootData->GetHandle());
		}

		check(StartingRootData && (bWasDefaultSceneRoot || StartingRootData->CanReparent()));

		// Remove the current scene root node from the SCS context
		Blueprint->SimpleConstructionScript->RemoveNode(StartingRootData->GetSCSNode(), /*bValidateSceneRootNodes=*/false);

		// Save old root node
		OldSceneRoot = StartingRootData->GetHandle();

		// Add dropped node to the SCS context
		Blueprint->SimpleConstructionScript->AddNode(DroppedNewSceneRootData->GetSCSNode());
		
		// Remove or re-parent the old root
		if (OldSceneRoot.IsValid())
		{
			check(DroppedNewSceneRootData->CanReparent());

			// Set old root as child of new root
			AttachSubobject(DroppedNewSceneRootData->GetHandle(), OldSceneRoot);

			if (bWasDefaultSceneRoot)
			{
				DeleteSubobject(Context, OldSceneRoot, Blueprint);
			}
		}
	}
	else
	{
		// Remove the dropped node from its existing parent
		if(DroppedNewSceneRootData->HasParent())
		{
			DetachSubobject(DroppedNewSceneRootData->GetParentHandle(), DroppedNewSceneRootData->GetHandle());
		}
		
		// Save old root node
		OldSceneRoot = StartingRootHandle;

		if(OldSceneRoot.IsValid())
		{
			// If the thing we replaced was the default scene root, then just delete it
			if(bWasDefaultSceneRoot)
			{
				DeleteSubobject(Context, OldSceneRoot, Blueprint);
				AActor* ActorContext = Context.GetData()->GetMutableActorContext();
				ActorContext->SetRootComponent(CastChecked<USceneComponent>(DroppedNewSceneRootData->GetMutableComponentTemplate()));
			}
			// Otherwise, reparent it to the new scene root
			else
			{
				FReparentSubobjectParams Params;
				Params.BlueprintContext = nullptr;
				Params.ActorPreviewContext = nullptr;
				Params.NewParentHandle = DroppedNewSceneRootHandle;
				
				ReparentSubobject(Params, OldSceneRoot);
			}
		}
	}

	return true;
}

bool USubobjectDataSubsystem::ReparentSubobjects(const FReparentSubobjectParams& Params, const TArray<FSubobjectDataHandle>& HandlesToMove)
{
	if(!Params.NewParentHandle.IsValid())
	{
		UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Failed to reparent: Invalid parent handle when reparenting!"));
		return false;
	}
	
	FSubobjectData* NewParentData = Params.NewParentHandle.GetData();
	check(NewParentData);
	
	if (Params.BlueprintContext)
	{
		if (!Params.ActorPreviewContext)
		{
			UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Failed to reparent: In a blueprint context there must be an actor preview!"));
			return false;
		}

		for (const FSubobjectDataHandle& HandleToMove : HandlesToMove)
		{
			FSubobjectData* DroppedData = HandleToMove.GetSharedDataPtr().Get();
			if(DroppedData->GetBlueprint() != Params.BlueprintContext)
			{
				UActorComponent* ComponentTemplate = DroppedData->GetMutableComponentTemplate();
				check(ComponentTemplate);
	
				FAddNewSubobjectParams AddNewParms;
				AddNewParms.BlueprintContext = Params.BlueprintContext;
				AddNewParms.NewClass = ComponentTemplate->GetClass();
				AddNewParms.ParentHandle = NewParentData->GetHandle();
				
				// Note: This will mark the Blueprint as structurally modified
				FText FailReason;
				FSubobjectDataHandle ClonedSubobject = AddNewSubobject(AddNewParms, FailReason);
				check(ClonedSubobject.IsValid());
				
				FSubobjectData* ClonedData = ClonedSubobject.GetSharedDataPtr().Get();
				UActorComponent* ClonedComponent = ClonedData->GetMutableComponentTemplate();
	
				// Serialize object properties using write/read operations.
				TArray<uint8> SavedProperties;
				FObjectWriter Writer(ComponentTemplate, SavedProperties);
				FObjectReader(ClonedComponent, SavedProperties);			
			}
			else
			{
				USceneComponent* SceneComponentTemplate = Cast<USceneComponent>(DroppedData->GetMutableComponentTemplate());
				// Cache current default values for propagation
				FVector OldRelativeLocation, OldRelativeScale3D;
				FRotator OldRelativeRotation;
				if(SceneComponentTemplate)
				{
					OldRelativeLocation = SceneComponentTemplate->GetRelativeLocation();
					OldRelativeRotation = SceneComponentTemplate->GetRelativeRotation();
					OldRelativeScale3D = SceneComponentTemplate->GetRelativeScale3D();
				}

				FSubobjectDataHandle OldParentHandle = DroppedData->GetParentHandle();
				FSubobjectData* OldParentData = OldParentHandle.IsValid() ? OldParentHandle.GetSharedDataPtr().Get() : nullptr;

				if(OldParentData)
				{
					DetachSubobject(OldParentHandle, DroppedData->GetHandle());
					// If the associated component template is a scene component, maintain its preview world position
					if(SceneComponentTemplate)
					{
						// Save current state
						SceneComponentTemplate->Modify();

						// Reset the attach socket name
						SceneComponentTemplate->SetupAttachment(SceneComponentTemplate->GetAttachParent(), NAME_None);
						USCS_Node* SCS_Node = DroppedData->GetSCSNode();
						if(SCS_Node)
						{
							SCS_Node->Modify();
							SCS_Node->AttachToName = NAME_None;
						}

						// Attempt to locate a matching registered instance of the component template in the Actor context that's being edited
						const USceneComponent* InstancedSceneComponent = Cast<USceneComponent>(DroppedData->FindMutableComponentInstanceInActor(Params.ActorPreviewContext));
						if(InstancedSceneComponent && InstancedSceneComponent->IsRegistered())
						{
							// If we find a match, save off the world position
							const FTransform& ComponentToWorld = InstancedSceneComponent->GetComponentToWorld();
							SceneComponentTemplate->SetRelativeTransform_Direct(ComponentToWorld);
						}
					}
				}

				// Attach the dropped node to the given node
				AttachSubobject(NewParentData->GetHandle(), DroppedData->GetHandle());

				// Attempt to locate a matching instance of the parent component template in the Actor context that's being edited
				USceneComponent* ParentSceneComponent = Cast<USceneComponent>(DroppedData->FindMutableComponentInstanceInActor(Params.ActorPreviewContext));
				if(SceneComponentTemplate && ParentSceneComponent && ParentSceneComponent->IsRegistered())
				{
					ConformTransformRelativeToParent(SceneComponentTemplate, ParentSceneComponent);
				}

				// Propagate any default value changes out to all instances of the template. If we didn't do this, then instances could incorrectly override the new default value with the old default value when construction scripts are re-run.
				if(SceneComponentTemplate)
				{
					TArray<UObject*> InstancedSceneComponents;
					SceneComponentTemplate->GetArchetypeInstances(InstancedSceneComponents);
					for(int32 InstanceIndex = 0; InstanceIndex < InstancedSceneComponents.Num(); ++InstanceIndex)
					{
						USceneComponent* InstancedSceneComponent = Cast<USceneComponent>(InstancedSceneComponents[InstanceIndex]);
						if(InstancedSceneComponent != nullptr)
						{
							FComponentEditorUtils::ApplyDefaultValueChange(InstancedSceneComponent, InstancedSceneComponent->GetRelativeLocation_DirectMutable(), OldRelativeLocation, SceneComponentTemplate->GetRelativeLocation());
							FComponentEditorUtils::ApplyDefaultValueChange(InstancedSceneComponent, InstancedSceneComponent->GetRelativeRotation_DirectMutable(), OldRelativeRotation, SceneComponentTemplate->GetRelativeRotation());
							FComponentEditorUtils::ApplyDefaultValueChange(InstancedSceneComponent, InstancedSceneComponent->GetRelativeScale3D_DirectMutable(),  OldRelativeScale3D,  SceneComponentTemplate->GetRelativeScale3D());
						}
					}
				}
			}
		}

		FBlueprintEditorUtils::PostEditChangeBlueprintActors(Params.BlueprintContext, true);
		return true;
	}
	else
	{
		for (const FSubobjectDataHandle& HandleToMove : HandlesToMove)
		{
			// Remove this handle from it's parent if it has one
			FSubobjectData* DataToMove = HandleToMove.GetData();

			if (DataToMove->HasParent())
			{
				DetachSubobject(DataToMove->GetParentHandle(), HandleToMove);
			}

			AttachSubobject(Params.NewParentHandle, HandleToMove);
		}

		// Rerun construction scripts on the base actor 
		if (AActor* ActorInstance = NewParentData->GetMutableActorContext())
		{
			ActorInstance->RerunConstructionScripts();
		}
	}
	
	return true;
}

bool USubobjectDataSubsystem::DetachSubobject(const FSubobjectDataHandle& OwnerHandle, const FSubobjectDataHandle& ChildToRemove)
{
	FSubobjectData* OwnerData = OwnerHandle.GetData();
	FSubobjectData* ChildToRemoveData = ChildToRemove.GetData();
	if(!OwnerData || !ChildToRemoveData)
	{
		return false;
	}

	// Remove this subobject handle from the parent
	OwnerData->RemoveChildHandleOnly(ChildToRemoveData->GetHandle());
	ChildToRemoveData->ClearParentHandle();
	// if its an instance component, call detact from compont
	if(ChildToRemoveData->IsInstancedComponent())
	{
		USceneComponent* ChildInstance = Cast<USceneComponent>(ChildToRemoveData->GetMutableComponentTemplate());
		if (ensure(ChildInstance))
		{
			// Handle detachment at the instance level
			ChildInstance->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}
		return true;
	}

	// Bypass removal logic if we're part of a child actor subtree
	if (ChildToRemoveData->IsChildActor())
	{
		return true;
	}
	
	if(USCS_Node* SCS_ChildNode = ChildToRemoveData->GetSCSNode())
	{
		USimpleConstructionScript* SCS = SCS_ChildNode->GetSCS();
		
		if (SCS != nullptr)
		{
			SCS->RemoveNode(SCS_ChildNode);
		}
	}
	
	return true;
}

bool USubobjectDataSubsystem::AttachSubobject(const FSubobjectDataHandle& OwnerHandle, const FSubobjectDataHandle& ChildToAddHandle)
{
	FSubobjectData* OwnerData = OwnerHandle.GetData();
	FSubobjectData* ChildToAddData = ChildToAddHandle.GetData();

	if(!OwnerData || !ChildToAddData)
	{
		return false;
	}

	// If the given child has a parent already, remove it from that first
	if(ChildToAddData->HasParent())
	{
		DetachSubobject(ChildToAddData->GetParentHandle(), ChildToAddHandle);
	}

	check(!ChildToAddData->HasParent());
	
	// Reparent it to the new data
	OwnerData->AddChildHandleOnly(ChildToAddHandle);
	ChildToAddData->SetParentHandle(OwnerHandle);
	
	if(ChildToAddData->IsComponent())
	{
		USCS_Node* SCS_Node = OwnerData->GetSCSNode();
		const UActorComponent* ComponentTemplate = OwnerData->GetObject<UActorComponent>();
		
		// Add a child node to the SCS tree node if not already present
		if(USCS_Node* SCS_ChildNode = ChildToAddData->GetSCSNode())
		{
			// Get the SCS instance that owns the child node
			if(USimpleConstructionScript* SCS = SCS_ChildNode->GetSCS())
			{
				if(SCS_Node)
				{
					// If the parent and child are both owned by the same SCS instance
					if (SCS_Node->GetSCS() == SCS)
					{
						// Add the child into the parent's list of children
						if (!SCS_Node->GetChildNodes().Contains(SCS_ChildNode))
						{
							SCS_Node->AddChildNode(SCS_ChildNode);
						}
					}
					else
					{
						// Adds the child to the SCS root set if not already present
						SCS->AddNode(SCS_ChildNode);

						// Set parameters to parent this node to the "inherited" SCS node
						SCS_ChildNode->SetParent(SCS_Node);
					}
				}
				else if(ComponentTemplate)
				{
					// Adds the child to the SCS root set if not already present
					SCS->AddNode(SCS_ChildNode);
					
					// Set parameters to parent this node to the native component template
					SCS_ChildNode->SetParent(Cast<const USceneComponent>(ComponentTemplate));
				}
				else
				{
					// Adds the child to the SCS root set if not already present
					SCS->AddNode(SCS_ChildNode);
				}
			}
		}
		else if(OwnerData->IsInstancedComponent())
		{
			USceneComponent* ChildInstance = Cast<USceneComponent>(ChildToAddData->GetMutableComponentTemplate());
			if (ensure(ChildInstance != nullptr))
			{
				USceneComponent* ParentInstance = Cast<USceneComponent>(OwnerData->GetMutableComponentTemplate());
				if (ensure(ParentInstance != nullptr))
				{
					// Handle attachment at the instance level
					if (ChildInstance->GetAttachParent() != ParentInstance)
					{
						AActor* Owner = ParentInstance->GetOwner();
						if (Owner->GetRootComponent() == ChildInstance)
						{
							Owner->SetRootComponent(ParentInstance);
						}
						ChildInstance->AttachToComponent(ParentInstance, FAttachmentTransformRules::KeepWorldTransform);
					}
				}
			}
		}
	}
	
	return true;
}

bool USubobjectDataSubsystem::IsValidRename(const FSubobjectDataHandle& Handle, const FText& InNewText, FText& OutErrorMessage) const
{
	const FSubobjectData* Data = Handle.GetData();
	if(!Data)
	{
		return false;		
	}
	
	const UBlueprint* Blueprint = Data->GetBlueprint();
	const FString& NewTextStr = InNewText.ToString();

	if (!NewTextStr.IsEmpty())
	{
		if (Data->GetVariableName().ToString() == NewTextStr)
		{
			return true;
		}

		if (const UActorComponent* ComponentInstance = Data->GetComponentTemplate())
		{
			AActor* ExistingNameSearchScope = ComponentInstance->GetOwner();
			if ((ExistingNameSearchScope == nullptr) && (Blueprint != nullptr))
			{
				ExistingNameSearchScope = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
			}

			if (!FComponentEditorUtils::IsValidVariableNameString(ComponentInstance, NewTextStr))
			{
				OutErrorMessage = LOCTEXT("RenameFailed_EngineReservedName", "This name is reserved for engine use.");
				return false;
			}
			else if (NewTextStr.Len() > NAME_SIZE)
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("CharCount"), NAME_SIZE);
				OutErrorMessage = FText::Format(LOCTEXT("ComponentRenameFailed_TooLong", "Component name must be less than {CharCount} characters long."), Arguments);
				return false;
			}
			else if (!FComponentEditorUtils::IsComponentNameAvailable(NewTextStr, ExistingNameSearchScope, ComponentInstance) 
                    || !FComponentEditorUtils::IsComponentNameAvailable(NewTextStr, ComponentInstance->GetOuter(), ComponentInstance ))
			{
				OutErrorMessage = LOCTEXT("RenameFailed_ExistingName", "Another component already has the same name.");
				return false;
			}
		}
		else if(const AActor* ActorInstance = Data->GetObject<AActor>())
		{
			// #TODO_BH Validation of actor instance
		}
		else
		{
			OutErrorMessage = LOCTEXT("RenameFailed_InvalidComponentInstance", "This node is referencing an invalid component instance and cannot be renamed. Perhaps it was destroyed?");
			return false;
		}
	}
	
	TSharedPtr<INameValidatorInterface> NameValidator;
	if (Blueprint != nullptr)
	{
		NameValidator = MakeShareable(new FKismetNameValidator(Blueprint, Data->GetVariableName()));
	}
	else if(const UActorComponent* CompTemplate = Data->GetComponentTemplate())
	{
		NameValidator = MakeShareable(new FStringSetNameValidator(CompTemplate->GetName()));
	}

	if(NameValidator)
	{
		EValidatorResult ValidatorResult = NameValidator->IsValid(NewTextStr);
		if (ValidatorResult == EValidatorResult::AlreadyInUse)
		{
			OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_InUse", "{0} is in use by another variable or function!"), InNewText);
		}
		else if (ValidatorResult == EValidatorResult::EmptyName)
		{
			OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Names cannot be left blank!");
		}
		else if (ValidatorResult == EValidatorResult::TooLong)
		{
			OutErrorMessage = LOCTEXT("RenameFailed_NameTooLong", "Names must have fewer than 100 characters!");
		}
	}

	if (OutErrorMessage.IsEmpty())
	{
		return true;
	}

	return false;
}

bool USubobjectDataSubsystem::CanCopySubobjects(const TArray<FSubobjectDataHandle>& Handles) const
{
	TArray<UActorComponent*> ComponentsToCopy;
	
	for (const FSubobjectDataHandle& Handle : Handles)
    {
    	if(FSubobjectData* Data = Handle.GetData())
    	{
    		if(!Data->CanCopy())
    		{
    			return false;
    		}
    		
    		// Get the component template associated with the selected node
    		if (UActorComponent* ComponentTemplate = Data->GetMutableComponentTemplate())
    		{
    			ComponentsToCopy.Add(ComponentTemplate);
    		}
    	}
    }

    // Verify that the components can be copied
    return FComponentEditorUtils::CanCopyComponents(ComponentsToCopy);
}

void USubobjectDataSubsystem::CopySubobjects(const TArray<FSubobjectDataHandle>& Handles, UBlueprint* BpContext)
{
	if(!CanCopySubobjects(Handles))
	{
		return;
	}

	TArray<UActorComponent*> ComponentsToCopy;

	for (const FSubobjectDataHandle& Handle : Handles)
	{
		if(FSubobjectData* Data = Handle.GetData())
		{
			ensureMsgf(Data->CanCopy(), TEXT("A non-copiable subobject has been allowed to copy!"));
    		
			// Get the component template associated with the selected node
			if (UActorComponent* ComponentTemplate = Data->GetMutableComponentTemplate())
			{
				ComponentsToCopy.Add(ComponentTemplate);
				if(BpContext && ComponentTemplate->CreationMethod != EComponentCreationMethod::UserConstructionScript)
				{
					// CopyComponents uses component attachment to maintain hierarchy, but the SCS templates are not
                    // setup with a relationship to each other. Briefly setup the attachment between the templates being
                    // copied so that the hierarchy is retained upon pasting
                    if (USceneComponent* SceneTemplate = Cast<USceneComponent>(ComponentTemplate))
                    {
                    	if (FSubobjectData* SelectedParentNodePtr = Data->GetParentHandle().GetData())
                    	{
                    		if (USceneComponent* ParentSceneTemplate = Cast<USceneComponent>(SelectedParentNodePtr->GetMutableComponentTemplate()))
                    		{
                    			SceneTemplate->SetupAttachment(ParentSceneTemplate);
                    		}
                    	}
                    }
				}
			}
		}
	}
	
	// Copy the components to the clipboard
	FComponentEditorUtils::CopyComponents(ComponentsToCopy);
	
	if(BpContext)
	{
		for (UActorComponent* ComponentTemplate : ComponentsToCopy)
		{
			if (ComponentTemplate->CreationMethod != EComponentCreationMethod::UserConstructionScript)
			{
				if (USceneComponent* SceneTemplate = Cast<USceneComponent>(ComponentTemplate))
				{
					// clear back out any temporary attachments we set up for the copy
					SceneTemplate->SetupAttachment(nullptr);
				}
			}
		}
	}
}

bool USubobjectDataSubsystem::CanPasteSubobjects(const FSubobjectDataHandle& RootHandle, UBlueprint* BPContext) const
{
	const FSubobjectDataHandle& SceneRootHandle = FindSceneRootForSubobject(RootHandle);
	const FSubobjectData* RootData = SceneRootHandle.GetData();
	const USceneComponent* SceneComponent = Cast<USceneComponent>(RootData->GetComponentTemplate());
	if(const AActor* RootActor = RootData->GetObject<AActor>())
	{
		SceneComponent = RootActor->GetRootComponent();
	}
	
	return
		(BPContext && RootData->IsActor())  ||
		(SceneComponent && FComponentEditorUtils::CanPasteComponents(SceneComponent, RootData->IsDefaultSceneRoot(), true));
}

void USubobjectDataSubsystem::PasteSubobjects(const FSubobjectDataHandle& PasteToContext, const TArray<FSubobjectDataHandle>& NewParentHandles, UBlueprint* Blueprint, TArray<FSubobjectDataHandle>& OutPastedHandles)
{
	if(!PasteToContext.IsValid() || NewParentHandles.IsEmpty())
	{
		return;
	}
	
	FSubobjectData* PasteToContextData = PasteToContext.GetSharedDataPtr().Get();	
	
	const FScopedTransaction Transaction(LOCTEXT("PasteComponents", "Paste Component(s)"));
	if(Blueprint)
	{
		// Get the components to paste from the clipboard
		TMap<FName, FName> ParentMap;
		TMap<FName, UActorComponent*> NewObjectMap;
		FComponentEditorUtils::GetComponentsFromClipboard(ParentMap, NewObjectMap, true);

		check(Blueprint->SimpleConstructionScript != nullptr);
		
		Blueprint->Modify();
		SaveSCSCurrentState(Blueprint->SimpleConstructionScript);
		
		// Create a new tree node for each new (pasted) component
        FSubobjectDataHandle FirstNode;
        TMap<FName, FSubobjectDataHandle> NewNodeMap;

		for (const TPair<FName, UActorComponent*>& NewObjectPair : NewObjectMap)
		{
			// Get the component object instance
			UActorComponent* NewActorComponent = NewObjectPair.Value;
			check(NewActorComponent);
			
			// Create a new SCS node to contain the new component and add it to the tree
			USCS_Node* NewSCSNode = Blueprint->SimpleConstructionScript->CreateNodeAndRenameComponent(NewActorComponent);
			NewActorComponent = NewSCSNode ? ToRawPtr(NewSCSNode->ComponentTemplate) : nullptr;

			FSubobjectDataHandle TargetParentHandle = FindParentForNewSubobject(NewActorComponent, PasteToContext);
			TSharedPtr<FInheritedSubobjectData> TargetData = StaticCastSharedPtr<FInheritedSubobjectData>(TargetParentHandle.GetSharedDataPtr());
			
			// Create a new subobject data set with this component. Use the SCS node here and the subobject data
			// will correctly associate the component template
			FSubobjectDataHandle NewDataHandle = FactoryCreateSubobjectDataWithParent(
				NewActorComponent,
				TargetParentHandle,
				TargetData ? TargetData->bIsInheritedSCS : false
			);

			AttachSubobject(TargetParentHandle, NewDataHandle);

			// Map the new subobject's data handle to it's instance name
			NewNodeMap.Add(NewObjectPair.Key, NewDataHandle);
		}

		// Restore the node hierarchy from the original copy
		for (const TPair<FName, FSubobjectDataHandle>& NewNodePair : NewNodeMap)
		{
			// If an entry exists in the set of known parent nodes for the current node
			if (ParentMap.Contains(NewNodePair.Key))
			{
				// Get the parent node name
				FName ParentName = ParentMap[NewNodePair.Key];
				if (NewNodeMap.Contains(ParentName))
				{
					// Reattach the current node to the parent node (this will also handle detachment from the scene root node)
					const FSubobjectDataHandle& DesiredParentHandle = NewNodeMap[ParentName];
					AttachSubobject(DesiredParentHandle, NewNodePair.Value);
				}
			}
		}

		// Modify the Blueprint generated class structure (this will also call UpdateTree() as a result)
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else if(AActor* ActorContext = PasteToContextData->GetMutableObject<AActor>())
	{
		// Determine where in the hierarchy to paste (default to the root)
		USceneComponent* TargetComponent = ActorContext->GetRootComponent();
		for (FSubobjectDataHandle SelectedNode : NewParentHandles)
		{
			FSubobjectData* SelectedData = SelectedNode.GetSharedDataPtr().Get();
			check(SelectedData);
		
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(SelectedData->GetMutableComponentTemplate()))
			{
				TargetComponent = SceneComponent;
				break;
			}
		}
		
		// Paste the components. This uses the current data from the clipboard
		TArray<UActorComponent*> PastedComponents;
		FComponentEditorUtils::PasteComponents(PastedComponents, ActorContext, TargetComponent);

		// Create handles for each pasted subobject, attaching it to it's parent as necessary
		for(UActorComponent* PastedComponent : PastedComponents)
		{
			FSubobjectDataHandle ParentHandle = FindHandleForObject(PasteToContext, PastedComponent->GetOuter());
			FSubobjectDataHandle PastedHandle = FactoryCreateSubobjectDataWithParent(PastedComponent, ParentHandle);
			if(PastedHandle.IsValid())
			{
				OutPastedHandles.AddUnique(PastedHandle);
			}
		}
	}
}

void USubobjectDataSubsystem::DuplicateSubobjects(const FSubobjectDataHandle& Context, const TArray<FSubobjectDataHandle>& SubobjectsToDup, UBlueprint* BpContext)
{
	if(!Context.IsValid() || SubobjectsToDup.IsEmpty())
	{
		return;
	}

	FAddNewSubobjectParams NewSubobjectParams;
	NewSubobjectParams.BlueprintContext = BpContext;
	NewSubobjectParams.ParentHandle = Context;
	NewSubobjectParams.bConformTransformToParent = false;
	
	FText FailedAddReason = FText::GetEmpty();
	
	TMap<FSubobjectData*, FSubobjectData*> DuplicateSceneComponentMap;

	// For each Subobject to dup, add it as a new subobejct to the context
	for(const FSubobjectDataHandle& OriginalHandle : SubobjectsToDup)
	{
		if(!OriginalHandle.IsValid())
		{
			UE_LOG(LogSubobjectSubsystem, Warning, TEXT("Could not duplicate one or more subobjects, an invalid SubobjectToDup was given!"));
			continue;
		}

		FSubobjectData* OriginalData = OriginalHandle.GetSharedDataPtr().Get();
		if(UActorComponent* ComponentTemplate = OriginalData->GetMutableComponentTemplate())
		{
			USCS_Node* SCSNode = OriginalData->GetSCSNode();
			check(SCSNode == nullptr || SCSNode->ComponentTemplate == ComponentTemplate);

			NewSubobjectParams.NewClass = ComponentTemplate->GetClass();
			NewSubobjectParams.AssetOverride = SCSNode ? (UObject*)SCSNode : ComponentTemplate;
			
			FSubobjectDataHandle ClonedSubobject = AddNewSubobject(NewSubobjectParams, FailedAddReason);
			FSubobjectData* ClonedData = ClonedSubobject.GetSharedDataPtr().Get();
			
			if(ClonedData && ClonedData->IsSceneComponent())
			{
				DuplicateSceneComponentMap.Add(OriginalData, ClonedData);
			}
		}
	}

	for (const TPair<FSubobjectData*, FSubobjectData*>& DuplicatedPair : DuplicateSceneComponentMap)
	{
		FSubobjectData* OriginalData = DuplicatedPair.Key;
		FSubobjectData* NewData = DuplicatedPair.Value;

		USceneComponent* OriginalComponent = CastChecked<USceneComponent>(OriginalData->GetMutableComponentTemplate());
		USceneComponent* NewSceneComponent = CastChecked<USceneComponent>(NewData->GetMutableComponentTemplate());
		
		if(BpContext)
		{
			// Ensure that any native attachment relationship inherited from the original copy is removed (to prevent a GLEO assertion)
			NewSceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}

		// If we're duplicating the root then we're already a child of it so need to reparent, but we do need to reset the scale
		// otherwise we'll end up with the square of the root's scale instead of being the same size.
		if(OriginalData->IsDefaultSceneRoot())
		{
			NewSceneComponent->SetRelativeScale3D_Direct(FVector(1.f));
		}
		else
		{
			// If the original node was parented, attempt to add the duplicate as a child of the same parent node if the parent is not
			// part of the duplicate set, otherwise parent to the parent's duplicate
			FSubobjectDataHandle ParentHandle = OriginalData->GetParentHandle();
			if(ParentHandle.IsValid())
			{
				if (FSubobjectData** ParentDuplicateComponent = DuplicateSceneComponentMap.Find(ParentHandle.GetData()))
				{
					//FSCSEditorTreeNodePtrType DuplicateParentNodePtr = FindTreeNode(*ParentDuplicateComponent);
					//if (DuplicateParentNodePtr.IsValid())
					{
						//ParentNodePtr = DuplicateParentNodePtr;
					}
				}
			}
		}
	}
}

FScopedTransaction* USubobjectDataSubsystem::BeginTransaction(const TArray<FSubobjectDataHandle>& Handles, const FText& Description, UBlueprint* InBlueprint)
{
	FScopedTransaction* OutTransaction = new FScopedTransaction(Description);
	if(InBlueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(InBlueprint);
	}

	for (const FSubobjectDataHandle& Handle : Handles)
	{
		if(FSubobjectData* Data = Handle.GetData())
		{
			if(USCS_Node* SCS_Node = Data->GetSCSNode())
			{
				USimpleConstructionScript* SCS = SCS_Node->GetSCS();
				UBlueprint* Blueprint = SCS ? SCS->GetBlueprint() : nullptr;
				if (Blueprint == InBlueprint)
				{
					SCS_Node->Modify();
				}
			}

			// Modify template, any instances will be reconstructed as part of PostUndo:
			if (UActorComponent* ComponentTemplate = Data->GetMutableObjectForBlueprint<UActorComponent>(InBlueprint))
			{
				ComponentTemplate->SetFlags(RF_Transactional);
				ComponentTemplate->Modify();
			}
		}
	}
	return OutTransaction;
}

FSubobjectDataHandle USubobjectDataSubsystem::CreateSubobjectData(UObject* Context, const FSubobjectDataHandle& ParentHandle /* = FSubobjectDataHandle::InvalidHandle */, bool bIsInheritedSCS/* = false */)
{
	FCreateSubobjectParams Params;
	Params.Context = Context;
	Params.ParentHandle = ParentHandle;
	Params.bIsInheritedSCS = bIsInheritedSCS;

	ISubobjectDataFactory* Factory = FactoryManager->FindFactoryToUse(Params);
	check(Factory);
	TSharedPtr<FSubobjectData> SharedPtr = Factory->CreateSubobjectData(Params);
	
	if(!SharedPtr.IsValid())
	{
		ensureMsgf(false, TEXT("The subobject data factories failed to create subobject data!"));
		SharedPtr = TSharedPtr<FSubobjectData>(new FSubobjectData(Context, ParentHandle));
	}

	check(SharedPtr.IsValid());
	
	SharedPtr->Handle.DataPtr = SharedPtr;

	return SharedPtr->GetHandle();
}

FSubobjectDataHandle USubobjectDataSubsystem::FactoryCreateSubobjectDataWithParent(UObject* Context, const FSubobjectDataHandle& ParentHandle, bool bIsInheritedSCS /* = false */)
{
	FSubobjectData* ParentData = ParentHandle.GetSharedDataPtr().Get();
	if (!ensureMsgf(ParentData, TEXT("Attempted to use an invalid parent subobject handle!")))
	{
		return FSubobjectDataHandle::InvalidHandle;
	}

	// Does this parent subobject already have this object listed in the children?
	// if yes, then just return that handle
	const FSubobjectDataHandle& ExistingChild = ParentData->FindChildByObject(Context);
	if (ExistingChild.IsValid())
	{
		return ExistingChild;
	}

	// Otherwise, we need to create a new handle
	FSubobjectDataHandle OutHandle = CreateSubobjectData(Context, ParentHandle);
	
	// Inform the parent that it has a new child
	const bool bSuccess = ParentData->AddChildHandleOnly(OutHandle);
	ensureMsgf(bSuccess, TEXT("Failed to add a child to parent subobject!"));

	return OutHandle;
}

FSubobjectDataHandle USubobjectDataSubsystem::FactoryCreateInheritedBpSubobject(UObject* Context, const FSubobjectDataHandle& InParentHandle, bool bIsInherited, TArray<FSubobjectDataHandle>& OutArray)
{
	USCS_Node* InSCSNode = Cast<USCS_Node>(Context);
	
	FSubobjectDataHandle ParentHandle = InParentHandle;
	FSubobjectData* ParentData = ParentHandle.GetData();
	
	check(InSCSNode && ParentData && ParentData->IsValid());
	// Get a handle for us to work with
	FSubobjectDataHandle OutHandle = FactoryCreateSubobjectDataWithParent(InSCSNode, ParentHandle);
	check(OutHandle.IsValid());
	FSubobjectData* NewData = OutHandle.GetData();

	// @todo the concept of inherited SCS nodes will not exist once the
	// larger subobject refactor roles out, but this is necessary for now.
	if(TSharedPtr<FInheritedSubobjectData> InheritedData = StaticCastSharedPtr<FInheritedSubobjectData>(OutHandle.GetSharedDataPtr()))
	{
		InheritedData->bIsInheritedSCS = bIsInherited;
	}
	
	// Determine whether or not the given node is inherited from a parent Blueprint
	USimpleConstructionScript* NodeSCS = InSCSNode->GetSCS();
	
	if( InSCSNode->ComponentTemplate && 
        InSCSNode->ComponentTemplate->IsA(USceneComponent::StaticClass()) && 
        ParentData->IsComponent())
	{
		bool bParentIsEditorOnly = ParentData->GetComponentTemplate()->IsEditorOnly();
		// if you can't nest this new node under the proposed parent (then swap the two)
		if (bParentIsEditorOnly && !InSCSNode->ComponentTemplate->IsEditorOnly() && ParentData->CanReparent())
		{
			FSubobjectData* OldParentPtr = ParentData;
			FSubobjectData* GrandparentPtr = OldParentPtr->GetParentHandle().GetData();

			DetachSubobject(OldParentPtr->GetHandle(), NewData->GetHandle());
			NodeSCS->RemoveNode(OldParentPtr->GetSCSNode());
			
			// if the grandparent node is invalid (assuming this means that the parent node was the scene-root)
			if (!GrandparentPtr->IsValid())
			{
				NodeSCS->AddNode(NewData->GetSCSNode());
			}
			else 
			{
				AttachSubobject(GrandparentPtr->GetHandle(), NewData->GetHandle());
			}
			
			// move the proposed parent in as a child to the new node
			AttachSubobject(NewData->GetHandle(), OldParentPtr->GetHandle());

		} // if bParentIsEditorOnly...
	}
	else
	{
		// If the SCS root node array does not already contain the given node, this will add it (this should only occur after node creation)
		if(NodeSCS != nullptr)
		{
			NodeSCS->AddNode(InSCSNode);
		}
	}

	// Recursively add the given SCS node's child nodes
	if(OutHandle.IsValid())
	{
		OutArray.Add(OutHandle);

		for (USCS_Node* ChildNode : InSCSNode->GetChildNodes())
		{
			FSubobjectDataHandle NewChildHandle = FactoryCreateInheritedBpSubobject(ChildNode, OutHandle, bIsInherited, OutArray);
			ensure(NewChildHandle.IsValid());
			OutArray.Add(NewChildHandle);
		}	
	}
	
	return OutHandle;
}

void USubobjectDataSubsystem::RenameSubobjectMemberVariable(UBlueprint* BPContext, const FSubobjectDataHandle& InHandle, const FName NewName)
{
	if (!BPContext || !InHandle.IsValid())
	{
		return;
	}

	if(FSubobjectData* Data = InHandle.GetSharedDataPtr().Get())
	{
		if(USCS_Node* Node = Data->GetSCSNode())
		{
			FBlueprintEditorUtils::RenameComponentMemberVariable(BPContext, Node, NewName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
