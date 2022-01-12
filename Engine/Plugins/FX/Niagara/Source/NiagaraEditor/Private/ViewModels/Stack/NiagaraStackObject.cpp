// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackPropertyRow.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraNode.h"
#include "NiagaraEditorModule.h"

#include "Modules/ModuleManager.h"
#include "IPropertyRowGenerator.h"
#include "PropertyEditorModule.h"
#include "IDetailTreeNode.h"

#include "NiagaraPlatformSet.h"
#include "ViewModels/Stack/NiagaraStackObjectIssueGenerator.h"

UNiagaraStackObject::UNiagaraStackObject()
{
}

void UNiagaraStackObject::Initialize(FRequiredEntryData InRequiredEntryData, UObject* InObject, FString InOwnerStackItemEditorDataKey, UNiagaraNode* InOwningNiagaraNode)
{
	checkf(WeakObject.IsValid() == false, TEXT("Can only initialize once."));
	FString ObjectStackEditorDataKey = FString::Printf(TEXT("%s-%s"), *InOwnerStackItemEditorDataKey, *InObject->GetName());
	Super::Initialize(InRequiredEntryData, InOwnerStackItemEditorDataKey, ObjectStackEditorDataKey);
	WeakObject = InObject;
	OwningNiagaraNode = InOwningNiagaraNode;
	bIsRefresingDataInterfaceErrors = false;
}

void UNiagaraStackObject::SetOnSelectRootNodes(FOnSelectRootNodes OnSelectRootNodes)
{
	OnSelectRootNodesDelegate = OnSelectRootNodes;
}

void UNiagaraStackObject::RegisterInstancedCustomPropertyLayout(UStruct* Class, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	checkf(PropertyRowGenerator.IsValid() == false, TEXT("Can not add additional customizations after children have been refreshed."));
	RegisteredClassCustomizations.Add({ Class, DetailLayoutDelegate });
}

void UNiagaraStackObject::RegisterInstancedCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate, TSharedPtr<IPropertyTypeIdentifier> Identifier)
{
	checkf(PropertyRowGenerator.IsValid() == false, TEXT("Can not add additional customizations after children have been refreshed."));
	RegisteredPropertyCustomizations.Add({ PropertyTypeName, PropertyTypeLayoutDelegate, Identifier });
}

UObject* UNiagaraStackObject::GetObject()
{
	return WeakObject.Get();
}

void UNiagaraStackObject::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	TArray<UObject*> ChangedObjects;
	ChangedObjects.Add(GetObject());
	OnDataObjectModified().Broadcast(ChangedObjects, ENiagaraDataObjectChange::Changed);
}

bool UNiagaraStackObject::GetIsEnabled() const
{
	return OwningNiagaraNode == nullptr || OwningNiagaraNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

bool UNiagaraStackObject::GetShouldShowInStack() const
{
	return false;
}

UObject* UNiagaraStackObject::GetDisplayedObject() const
{
	return WeakObject.Get();
}

void UNiagaraStackObject::FinalizeInternal()
{
	if (PropertyRowGenerator.IsValid())
	{
		PropertyRowGenerator->OnRowsRefreshed().RemoveAll(this);
		PropertyRowGenerator->SetObjects(TArray<UObject*>());

		// Enqueue the row generator for destruction because stack entries might be finalized during the system view model tick
		// and you can't destruct tickables while other tickables are being ticked.
		FNiagaraEditorModule::Get().EnqueueObjectForDeferredDestruction(PropertyRowGenerator.ToSharedRef());
		PropertyRowGenerator.Reset();
	}
	Super::FinalizeInternal();
}

void UNiagaraStackObject::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	FNiagaraEditorModule* NiagaraEditorModule = &FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	
	TFunction<void(uint8*, UStruct*, bool)> GatherIssueFromProperties;
	
	//Recurse into all our child properties and gather any issues they may generate via the INiagaraStackObjectIssueGenerator helper objects.
	GatherIssueFromProperties = [&](uint8* BasePtr, UStruct* InStruct, bool bRecurseChildren)
	{
		//TODO: Walk up the base class hierarchy. 
		//This class may not have an issue generator but it's base might.

		//Generate any issue for this property.
		if (INiagaraStackObjectIssueGenerator* IssueGenerator = NiagaraEditorModule->FindStackObjectIssueGenerator(InStruct->GetFName()))
		{
			IssueGenerator->GenerateIssues(BasePtr, this, NewIssues);
		}

		//Recurse into child properties to generate any issue there.
		for (TFieldIterator<FProperty> PropertyIt(InStruct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			uint8* PropPtr = BasePtr + PropertyIt->GetOffset_ForInternal();
			
			if (bRecurseChildren)
			{
				if (const FStructProperty* StructProp = CastField<const FStructProperty>(Property))
				{
					GatherIssueFromProperties(PropPtr, StructProp->Struct, bRecurseChildren);
				}
				else if (const FArrayProperty* ArrayProp = CastField<const FArrayProperty>(Property))
				{
					if (ArrayProp->Inner->IsA<FStructProperty>())
					{
						const FStructProperty* StructInner = CastFieldChecked<const FStructProperty>(ArrayProp->Inner);

						FScriptArrayHelper ArrayHelper(ArrayProp, PropPtr);
						for (int32 ArrayEntryIndex = 0; ArrayEntryIndex < ArrayHelper.Num(); ++ArrayEntryIndex)
						{
							uint8* ArrayEntryData = ArrayHelper.GetRawPtr(ArrayEntryIndex);
							GatherIssueFromProperties(ArrayEntryData, StructInner->Struct, bRecurseChildren);
						}
					}
				}
				//Recursing to object refs seems to have some circular links causing explosions.
				//For now lets just recurse down structs. 
				//UObjects are mostly their own stack objects anyway.
// 				else if (const FObjectPropertyBase* ObjProperty = CastField<const FObjectPropertyBase>(Property))
// 				{
// 					GatherIssueFromProperties(PropPtr, ObjProperty->PropertyClass, bRecurseChildren);
// 				}
			}
		}
	};

	UObject* Object = WeakObject.Get();
	if ( Object == nullptr )
	{
		return;
	}

	GatherIssueFromProperties((uint8*)Object, Object->GetClass(), true);

	if (GetSystemViewModel()->GetIsForDataProcessingOnly() == false && PropertyRowGenerator.IsValid() == false)
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FPropertyRowGeneratorArgs Args;
		Args.NotifyHook = this;
		PropertyRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(Args);

		for (FRegisteredClassCustomization& RegisteredClassCustomization : RegisteredClassCustomizations)
		{
			PropertyRowGenerator->RegisterInstancedCustomPropertyLayout(RegisteredClassCustomization.Class, RegisteredClassCustomization.DetailLayoutDelegate);
		}

		for (FRegisteredPropertyCustomization& RegisteredPropertyCustomization : RegisteredPropertyCustomizations)
		{
			PropertyRowGenerator->RegisterInstancedCustomPropertyTypeLayout(RegisteredPropertyCustomization.PropertyTypeName,
				RegisteredPropertyCustomization.PropertyTypeLayoutDelegate, RegisteredPropertyCustomization.Identifier);
		}

		TArray<UObject*> Objects;
		Objects.Add(Object);
		PropertyRowGenerator->SetObjects(Objects);

		// Add the refresh delegate after setting the objects to prevent refreshing children immediately.
		PropertyRowGenerator->OnRowsRefreshed().AddUObject(this, &UNiagaraStackObject::PropertyRowsRefreshed);
	}

	if (!Object->HasAllFlags(EObjectFlags::RF_Transactional))
	{
		NewIssues.Add(FStackIssue(
			EStackIssueSeverity::Warning,
			NSLOCTEXT("StackObject", "ObjectNotTransactionalShort", "Object is not transctional, undo won't work for it!"),
			NSLOCTEXT("StackObject", "ObjectNotTransactionalLong", "Object is not transctional, undo won't work for it! Please report this to the Niagara dev team."),
			GetStackEditorDataKey(),
			false,
			{
				FStackIssueFix(
					NSLOCTEXT("StackObject","TransactionalFix", "Fix transactional status."),
					FStackIssueFixDelegate::CreateLambda(
					[WeakObject=this->WeakObject]()
					{ 
						if ( UObject* Object = WeakObject.Get() )
						{
							Object->SetFlags(RF_Transactional);
						}
					}
				)),
			}));
	}

	// TODO: Handle this in a more generic way.  Maybe add error apis to UNiagaraMergable, or use a UObject interface, or create a
	// TODO: Possibly move to use INiagaraStackObjectIssueGenerator interface.
	// data interface specific implementation of UNiagaraStackObject.
	UNiagaraDataInterface* DataInterfaceObject = Cast<UNiagaraDataInterface>(Object);
	if (DataInterfaceObject != nullptr)
	{
		// First we need to refresh the errors on the data interface so that the rows in the property row generator 
		// are correct.
		{
			TGuardValue<bool> RefreGuard(bIsRefresingDataInterfaceErrors, true);
			DataInterfaceObject->RefreshErrors();
		}

		// Generate the summary stack issue for any errors which are generated.
		TArray<FNiagaraDataInterfaceError> Errors;
		TArray<FNiagaraDataInterfaceFeedback> Warnings, Info;
		FNiagaraEditorModule::Get().GetDataInterfaceFeedbackSafe(DataInterfaceObject, Errors, Warnings, Info);

		if (Errors.Num() > 0)
		{
			NewIssues.Add(FStackIssue(
				EStackIssueSeverity::Error,
				NSLOCTEXT("StackObject", "ObjectErrorsShort", "Object has errors"),
				NSLOCTEXT("StackObject", "ObjectErrorsLong", "The displayed object has errors.  Check the object properties or the message log for details."),
				GetStackEditorDataKey(),
				false));
		}
		if (Warnings.Num() > 0)
		{
			NewIssues.Add(FStackIssue(
				EStackIssueSeverity::Warning,
				NSLOCTEXT("StackObject", "ObjectWarningsShort", "Object has warnings"),
				NSLOCTEXT("StackObject", "ObjectWarningsLong", "The displayed object has warnings.  Check the object properties or the message log for details."),
				GetStackEditorDataKey(),
				false));
		}
	}

	if(PropertyRowGenerator.IsValid())
	{
		TArray<TSharedRef<IDetailTreeNode>> DefaultRootTreeNodes = PropertyRowGenerator->GetRootTreeNodes();
		TArray<TSharedRef<IDetailTreeNode>> RootTreeNodes;
		if (OnSelectRootNodesDelegate.IsBound())
		{
			OnSelectRootNodesDelegate.Execute(DefaultRootTreeNodes, &RootTreeNodes);
		}
		else
		{
			RootTreeNodes = DefaultRootTreeNodes;
		}

		for (TSharedRef<IDetailTreeNode> RootTreeNode : RootTreeNodes)
		{
			if (RootTreeNode->GetNodeType() == EDetailNodeType::Advanced)
			{
				continue;
			}

			UNiagaraStackPropertyRow* ChildRow = FindCurrentChildOfTypeByPredicate<UNiagaraStackPropertyRow>(CurrentChildren,
				[=](UNiagaraStackPropertyRow* CurrentChild) { return CurrentChild->GetDetailTreeNode() == RootTreeNode; });

			if (ChildRow == nullptr)
			{
				ChildRow = NewObject<UNiagaraStackPropertyRow>(this);
				ChildRow->Initialize(CreateDefaultChildRequiredData(), RootTreeNode, GetOwnerStackItemEditorDataKey(), GetOwnerStackItemEditorDataKey(), OwningNiagaraNode);
			}

			NewChildren.Add(ChildRow);
		}
	}
}

void UNiagaraStackObject::PostRefreshChildrenInternal()
{
	Super::PostRefreshChildrenInternal();
}

void UNiagaraStackObject::PropertyRowsRefreshed()
{
	if(bIsRefresingDataInterfaceErrors == false)
	{
		RefreshChildren();
	}
}
