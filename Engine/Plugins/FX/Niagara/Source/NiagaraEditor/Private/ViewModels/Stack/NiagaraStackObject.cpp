// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackObject.h"
#include "ViewModels/Stack/NiagaraStackPropertyRow.h"
#include "NiagaraNode.h"
#include "NiagaraEditorModule.h"

#include "Modules/ModuleManager.h"
#include "IPropertyRowGenerator.h"
#include "PropertyEditorModule.h"
#include "IDetailTreeNode.h"

UNiagaraStackObject::UNiagaraStackObject()
	: Object(nullptr)
{
}

void UNiagaraStackObject::Initialize(FRequiredEntryData InRequiredEntryData, UObject* InObject, FString InOwnerStackItemEditorDataKey, UNiagaraNode* InOwningNiagaraNode)
{
	checkf(Object == nullptr, TEXT("Can only initialize once."));
	FString ObjectStackEditorDataKey = FString::Printf(TEXT("%s-%s"), *InOwnerStackItemEditorDataKey, *InObject->GetName());
	Super::Initialize(InRequiredEntryData, false, InOwnerStackItemEditorDataKey, ObjectStackEditorDataKey);
	Object = InObject;
	OwningNiagaraNode = InOwningNiagaraNode;
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
	return Object;
}

void UNiagaraStackObject::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	OnDataObjectModified().Broadcast(Object);
}

bool UNiagaraStackObject::GetIsEnabled() const
{
	return OwningNiagaraNode == nullptr || OwningNiagaraNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

bool UNiagaraStackObject::GetShouldShowInStack() const
{
	return false;
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
	if (PropertyRowGenerator.IsValid() == false)
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

	// TODO: Handle this in a more generic way.  Maybe add error apis to UNiagaraMergable, or use a UObject interface, or create a
	// data interface specific implementation of UNiagaraStackObject.
	UNiagaraDataInterface* DataInterfaceObject = Cast<UNiagaraDataInterface>(Object);
	if (DataInterfaceObject != nullptr)
	{
		// First we need to refresh the errors on the data interface so that the rows in the property row generator 
		// are correct.  We need to remove the delegate handler first so that we don't cause a reentrant refresh.
		// This can be updated to use a guard value post 4.25.
		PropertyRowGenerator->OnRowsRefreshed().RemoveAll(this);
		DataInterfaceObject->RefreshErrors();
		PropertyRowGenerator->OnRowsRefreshed().AddUObject(this, &UNiagaraStackObject::PropertyRowsRefreshed);

		// Generate the summary stack issue for any errors which are generated.
		TArray<FNiagaraDataInterfaceError> Errors;
		TArray<FNiagaraDataInterfaceFeedback> Warnings, Info;
		UNiagaraDataInterface::GetFeedback(DataInterfaceObject, Errors, Warnings, Info);
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

void UNiagaraStackObject::PostRefreshChildrenInternal()
{
	Super::PostRefreshChildrenInternal();
}

void UNiagaraStackObject::PropertyRowsRefreshed()
{
	RefreshChildren();
}
