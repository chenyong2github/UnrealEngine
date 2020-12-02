// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementOwnerStore.h"
#include "Elements/Framework/TypedElementUtil.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"

#include "Elements/Object/ObjectElementData.h"
#include "UObject/Object.h"

#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"

namespace EngineElementsLibraryUtil
{
template <typename ObjectClass, typename ElementDataType>
TTypedElementOwner<ElementDataType> CreateTypedElement(const ObjectClass* Object, const FName ElementTypeName)
{
	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();

	TTypedElementOwner<ElementDataType> TypedElement;
	if (ensureMsgf(Registry, TEXT("Typed element was requested for '%s' before the registry was available! This usually means that NewObject was used instead of CreateDefaultSubobject during CDO construction."), *Object->GetPathName()))
	{
		TypedElement = Registry->CreateElement<ElementDataType>(ElementTypeName);
	}

	return TypedElement;
}

#if WITH_EDITOR
template <typename ObjectClass, typename ElementDataType>
void CreateEditorTypedElement(const ObjectClass* Object, TTypedElementOwnerStore<ElementDataType, const ObjectClass*>& ElementOwnerStore, TFunctionRef<TTypedElementOwner<ElementDataType>(const ObjectClass*)> CreateElement)
{
	if (GIsEditor && !Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		ElementOwnerStore.RegisterElementOwner(Object, CreateElement(Object));
	}
}

template <typename ObjectClass, typename ElementDataType>
void DestroyEditorTypedElement(const ObjectClass* Object, TTypedElementOwnerStore<ElementDataType, const ObjectClass*>& ElementOwnerStore, TFunctionRef<void(const ObjectClass*, TTypedElementOwner<ElementDataType>&)> DestroyElement)
{
	if (TTypedElementOwner<ElementDataType> EditorElement = ElementOwnerStore.UnregisterElementOwner(Object))
	{
		DestroyElement(Object, EditorElement);
	}
}

template <typename ObjectClass, typename ElementDataType>
FTypedElementHandle AcquireEditorTypedElementHandle(const ObjectClass* Object, TTypedElementOwnerStore<ElementDataType, const ObjectClass*>& ElementOwnerStore, TFunctionRef<TTypedElementOwner<ElementDataType>(const ObjectClass*)> CreateElement, const bool bAllowCreate)
{
	if (GIsEditor)
	{
		TTypedElementOwnerScopedAccess<ElementDataType> EditorElement = bAllowCreate
			? ElementOwnerStore.FindOrRegisterElementOwner(Object, [Object, &CreateElement](){ return CreateElement(Object); })
			: ElementOwnerStore.FindElementOwner(Object);
		if (EditorElement)
		{
			return EditorElement->AcquireHandle();
		}
	}

	return FTypedElementHandle();
}
#endif
} // namespace EngineElementsLibraryUtil

#if WITH_EDITOR
TTypedElementOwnerStore<FObjectElementData, const UObject*> GObjectElementOwnerStore;
TTypedElementOwnerStore<FActorElementData, const AActor*> GActorElementOwnerStore;
TTypedElementOwnerStore<FComponentElementData, const UActorComponent*> GComponentElementOwnerStore;
#endif

UEngineElementsLibrary::UEngineElementsLibrary()
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// UObject exists inside CoreUObject, so it cannot call through directly to clean-up any element handles that have been created
		// Instead we rely on this GC hook to clean-up any element handles for unreachable objects prior to them being destroyed
		FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.AddStatic(&UEngineElementsLibrary::DestroyUnreachableEditorObjectElements);
	}
#endif
}

TTypedElementOwner<FObjectElementData> UEngineElementsLibrary::CreateObjectElement(const UObject* InObject)
{
	TTypedElementOwner<FObjectElementData> ObjectElement = EngineElementsLibraryUtil::CreateTypedElement<UObject, FObjectElementData>(InObject, NAME_Object);
	if (ObjectElement)
	{
		ObjectElement.GetDataChecked().Object = const_cast<UObject*>(InObject);
	}
	return ObjectElement;
}

void UEngineElementsLibrary::DestroyObjectElement(const UObject* InObject, TTypedElementOwner<FObjectElementData>& InOutObjectElement)
{
	if (InOutObjectElement)
	{
		checkf(InOutObjectElement.GetDataChecked().Object == InObject, TEXT("Object element was not for this object instance! %s"), *InObject->GetPathName());
		UTypedElementRegistry::GetInstance()->DestroyElement(InOutObjectElement);
	}
}

#if WITH_EDITOR
void UEngineElementsLibrary::CreateEditorObjectElement(const UObject* Object)
{
	EngineElementsLibraryUtil::CreateEditorTypedElement<UObject, FObjectElementData>(Object, GObjectElementOwnerStore, &UEngineElementsLibrary::CreateObjectElement);
}

void UEngineElementsLibrary::DestroyEditorObjectElement(const UObject* Object)
{
	EngineElementsLibraryUtil::DestroyEditorTypedElement<UObject, FObjectElementData>(Object, GObjectElementOwnerStore, &UEngineElementsLibrary::DestroyObjectElement);
}

void UEngineElementsLibrary::DestroyUnreachableEditorObjectElements()
{
	auto IsObjectElementUnreachable = [](const TTypedElementOwner<FObjectElementData>& EditorElement)
	{
		return EditorElement.GetDataChecked().Object->IsUnreachable();
	};

	auto DestroyUnreachableObjectElement = [Registry = UTypedElementRegistry::GetInstance()](TTypedElementOwner<FObjectElementData>&& EditorElement)
	{
		if (Registry)
		{
			Registry->DestroyElement(EditorElement);
		}
		else
		{
			EditorElement.Private_DestroyNoRef();
		}
	};

	GObjectElementOwnerStore.UnregisterElementOwners(IsObjectElementUnreachable, DestroyUnreachableObjectElement);
}

FTypedElementHandle UEngineElementsLibrary::AcquireEditorObjectElementHandle(const UObject* Object, const bool bAllowCreate)
{
	return EngineElementsLibraryUtil::AcquireEditorTypedElementHandle<UObject, FObjectElementData>(Object, GObjectElementOwnerStore, &UEngineElementsLibrary::CreateObjectElement, bAllowCreate);
}
#endif

TTypedElementOwner<FActorElementData> UEngineElementsLibrary::CreateActorElement(const AActor* InActor)
{
	TTypedElementOwner<FActorElementData> ActorElement = EngineElementsLibraryUtil::CreateTypedElement<AActor, FActorElementData>(InActor, NAME_Actor);
	if (ActorElement)
	{
		ActorElement.GetDataChecked().Actor = const_cast<AActor*>(InActor);
	}
	return ActorElement;
}

void UEngineElementsLibrary::DestroyActorElement(const AActor* InActor, TTypedElementOwner<FActorElementData>& InOutActorElement)
{
	if (InOutActorElement)
	{
		checkf(InOutActorElement.GetDataChecked().Actor == InActor, TEXT("Actor element was not for this actor instance! %s"), *InActor->GetPathName());
		UTypedElementRegistry::GetInstance()->DestroyElement(InOutActorElement);
	}
}

#if WITH_EDITOR
void UEngineElementsLibrary::CreateEditorActorElement(const AActor* Actor)
{
	EngineElementsLibraryUtil::CreateEditorTypedElement<AActor, FActorElementData>(Actor, GActorElementOwnerStore, &UEngineElementsLibrary::CreateActorElement);
}

void UEngineElementsLibrary::DestroyEditorActorElement(const AActor* Actor)
{
	EngineElementsLibraryUtil::DestroyEditorTypedElement<AActor, FActorElementData>(Actor, GActorElementOwnerStore, &UEngineElementsLibrary::DestroyActorElement);
}

FTypedElementHandle UEngineElementsLibrary::AcquireEditorActorElementHandle(const AActor* Actor, const bool bAllowCreate)
{
	return EngineElementsLibraryUtil::AcquireEditorTypedElementHandle<AActor, FActorElementData>(Actor, GActorElementOwnerStore, &UEngineElementsLibrary::CreateActorElement, bAllowCreate);
}
#endif

TTypedElementOwner<FComponentElementData> UEngineElementsLibrary::CreateComponentElement(const UActorComponent* InComponent)
{
	TTypedElementOwner<FComponentElementData> ComponentElement = EngineElementsLibraryUtil::CreateTypedElement<UActorComponent, FComponentElementData>(InComponent, NAME_Components);
	if (ComponentElement)
	{
		ComponentElement.GetDataChecked().Component = const_cast<UActorComponent*>(InComponent);
	}
	return ComponentElement;
}

void UEngineElementsLibrary::DestroyComponentElement(const UActorComponent* InComponent, TTypedElementOwner<FComponentElementData>& InOutComponentElement)
{
	if (InOutComponentElement)
	{
		checkf(InOutComponentElement.GetDataChecked().Component == InComponent, TEXT("Component element was not for this component instance! %s"), *InComponent->GetPathName());
		UTypedElementRegistry::GetInstance()->DestroyElement(InOutComponentElement);
	}
}

#if WITH_EDITOR
void UEngineElementsLibrary::CreateEditorComponentElement(const UActorComponent* Component)
{
	EngineElementsLibraryUtil::CreateEditorTypedElement<UActorComponent, FComponentElementData>(Component, GComponentElementOwnerStore, &UEngineElementsLibrary::CreateComponentElement);
}

void UEngineElementsLibrary::DestroyEditorComponentElement(const UActorComponent* Component)
{
	EngineElementsLibraryUtil::DestroyEditorTypedElement<UActorComponent, FComponentElementData>(Component, GComponentElementOwnerStore, &UEngineElementsLibrary::DestroyComponentElement);
}

FTypedElementHandle UEngineElementsLibrary::AcquireEditorComponentElementHandle(const UActorComponent* Component, const bool bAllowCreate)
{
	return EngineElementsLibraryUtil::AcquireEditorTypedElementHandle<UActorComponent, FComponentElementData>(Component, GComponentElementOwnerStore, &UEngineElementsLibrary::CreateComponentElement, bAllowCreate);
}
#endif

TArray<FTypedElementHandle> UEngineElementsLibrary::DuplicateElements(const TArray<FTypedElementHandle>& ElementHandles, UWorld* World, bool bOffsetLocations)
{
	return DuplicateElements(MakeArrayView(ElementHandles), World, bOffsetLocations);
}

TArray<FTypedElementHandle> UEngineElementsLibrary::DuplicateElements(TArrayView<const FTypedElementHandle> ElementHandles, UWorld* World, bool bOffsetLocations)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementHandles, ElementsToDuplicateByType);

	TArray<FTypedElementHandle> NewElements;
	NewElements.Reserve(ElementHandles.Num());

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		if (UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key))
		{
			WorldInterface->DuplicateElements(ElementsByTypePair.Value, World, bOffsetLocations, NewElements);
		}
	}

	return NewElements;
}

TArray<FTypedElementHandle> UEngineElementsLibrary::DuplicateElements(const UTypedElementList* ElementList, UWorld* World, bool bOffsetLocations)
{
	TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>> ElementsToDuplicateByType;
	TypedElementUtil::BatchElementsByType(ElementList, ElementsToDuplicateByType);

	TArray<FTypedElementHandle> NewElements;
	NewElements.Reserve(ElementList->Num());

	UTypedElementRegistry* Registry = UTypedElementRegistry::GetInstance();
	for (const auto& ElementsByTypePair : ElementsToDuplicateByType)
	{
		if (UTypedElementWorldInterface* WorldInterface = Registry->GetElementInterface<UTypedElementWorldInterface>(ElementsByTypePair.Key))
		{
			WorldInterface->DuplicateElements(ElementsByTypePair.Value, World, bOffsetLocations, NewElements);
		}
	}

	return NewElements;
}
