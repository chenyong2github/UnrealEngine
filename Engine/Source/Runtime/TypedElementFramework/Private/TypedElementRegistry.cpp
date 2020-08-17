// Copyright Epic Games, Inc. All Rights Reserved.

#include "TypedElementRegistry.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"

const FTypedElementId FTypedElementId::Unset;

UTypedElementRegistry* UTypedElementRegistry::Instance = nullptr;

UTypedElementRegistry::UTypedElementRegistry()
{
	FCoreDelegates::OnEndFrame.AddUObject(this, &UTypedElementRegistry::NotifyElementListPendingChanges);
}

void UTypedElementRegistry::Private_InitializeInstance()
{
	checkf(!Instance, TEXT("Instance was already initialized!"));
	Instance = NewObject<UTypedElementRegistry>();
	Instance->AddToRoot();
}

void UTypedElementRegistry::Private_ShutdownInstance()
{
	checkf(Instance, TEXT("Instance was already shutdown!"));
	Instance->RemoveFromRoot();
	Instance = nullptr;
}

UTypedElementRegistry* UTypedElementRegistry::GetInstance()
{
	return Instance;
}

void UTypedElementRegistry::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UTypedElementRegistry* This = CastChecked<UTypedElementRegistry>(InThis);
	for (TUniquePtr<FRegisteredElementType>& RegisteredElementType : This->RegisteredElementTypes)
	{
		if (RegisteredElementType)
		{
			for (auto& InterfacesPair : RegisteredElementType->Interfaces)
			{
				Collector.AddReferencedObject(InterfacesPair.Value);
			}
		}
	}
}

void UTypedElementRegistry::RegisterElementTypeImpl(const FName InElementTypeName, TUniquePtr<FRegisteredElementType>&& InRegisteredElementType)
{
	// Query whether this type has previously been registered in any type registry, and if so re-use that ID
	// If not (or if the element is typeless) then assign the next available ID
	FTypedHandleTypeId TypeId = InRegisteredElementType->GetDataTypeId();
	if (TypeId == 0)
	{
		static FCriticalSection NextTypeIdCS;
		static FTypedHandleTypeId NextTypeId = 1;

		FScopeLock NextTypeIdLock(&NextTypeIdCS);

		checkf(NextTypeId <= TypedHandleMaxTypeId, TEXT("Ran out of typed element type IDs!"));

		TypeId = NextTypeId++;
		InRegisteredElementType->SetDataTypeId(TypeId);
	}

	InRegisteredElementType->TypeId = TypeId;
	InRegisteredElementType->TypeName = InElementTypeName;
	AddRegisteredElementType(MoveTemp(InRegisteredElementType));
}

void UTypedElementRegistry::RegisterElementInterfaceImpl(const FName InElementTypeName, UTypedElementInterface* InElementInterface, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType)
{
	checkf(InElementInterface->IsA(InBaseInterfaceType), TEXT("Interface '%s' of type '%s' does not derive from '%s'!"), *InElementInterface->GetPathName(), *InElementInterface->GetClass()->GetName(), *InBaseInterfaceType->GetName());

	FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromName(InElementTypeName);
	checkf(RegisteredElementType, TEXT("Element type '%s' has not been registered!"), *InElementTypeName.ToString());

	checkf(!RegisteredElementType->Interfaces.Contains(InBaseInterfaceType->GetFName()), TEXT("Element type '%s' has already registered an interface for '%s'!"), *InElementTypeName.ToString(), *InBaseInterfaceType->GetName());
	RegisteredElementType->Interfaces.Add(InBaseInterfaceType->GetFName(), InElementInterface);
}

UTypedElementInterface* UTypedElementRegistry::GetElementInterfaceImpl(const FTypedElementId& InElementId, const TSubclassOf<UTypedElementInterface>& InBaseInterfaceType) const
{
	if (!InElementId)
	{
		return nullptr;
	}

	FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InElementId.GetTypeId());
	checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InElementId.GetTypeId());

	return RegisteredElementType->Interfaces.FindRef(InBaseInterfaceType->GetFName());
}

void UTypedElementRegistry::ReleaseElementId(FTypedElementId& InOutElementId)
{
	if (!InOutElementId)
	{
		return;
	}

	FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InOutElementId.GetTypeId());
	checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InOutElementId.GetTypeId());

#if WITH_TYPED_ELEMENT_REFCOUNT
	const FTypedElementInternalData& ElementData = RegisteredElementType->GetDataForElement(InOutElementId.GetElementId());
	ElementData.ReleaseRef();
#endif	// WITH_TYPED_ELEMENT_REFCOUNT

	InOutElementId.Private_DestroyNoRef();
}

FTypedElementHandle UTypedElementRegistry::GetElementHandle(const FTypedElementId& InElementId) const
{
	if (!InElementId)
	{
		return FTypedElementHandle();
	}

	FRegisteredElementType* RegisteredElementType = GetRegisteredElementTypeFromId(InElementId.GetTypeId());
	checkf(RegisteredElementType, TEXT("Element type ID '%d' has not been registered!"), InElementId.GetTypeId());

	FTypedElementHandle ElementHandle;
	ElementHandle.Private_InitializeAddRef(InElementId.GetTypeId(), InElementId.GetElementId(), RegisteredElementType->GetDataForElement(InElementId.GetElementId()));

	return ElementHandle;
}

FTypedElementListPtr UTypedElementRegistry::CreateElementList(TArrayView<const FTypedElementId> InElementIds)
{
	FTypedElementListPtr ElementList = CreateElementList();

	for (const FTypedElementId& ElementId : InElementIds)
	{
		if (FTypedElementHandle ElementHandle = GetElementHandle(ElementId))
		{
			ElementList->Add(MoveTemp(ElementHandle));
		}
	}

	return ElementList;
}

FTypedElementListPtr UTypedElementRegistry::CreateElementList(TArrayView<const FTypedElementHandle> InElementHandles)
{
	FTypedElementListPtr ElementList = CreateElementList();
	ElementList->Append(InElementHandles);
	return ElementList;
}

void UTypedElementRegistry::NotifyElementListPendingChanges()
{
	FReadScopeLock ActiveElementListsLock(ActiveElementListsRW);

	for (FTypedElementList* ActiveElementList : ActiveElementLists)
	{
		ActiveElementList->NotifyPendingChanges();
	}
}
