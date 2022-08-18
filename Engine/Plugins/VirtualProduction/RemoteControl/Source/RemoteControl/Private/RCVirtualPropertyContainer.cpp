// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCVirtualPropertyContainer.h"

#include "UObject/StructOnScope.h"
#include "Templates/SubclassOf.h"

#include "RCVirtualProperty.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "URCVirtualPropertyInContainer"

URCVirtualPropertyInContainer* URCVirtualPropertyContainerBase::AddProperty(const FName& InPropertyName, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject)
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("AddProperty", "AddP roperty"));

	Modify();

	MarkPackageDirty();
#endif

	const FName PropertyName = GenerateUniquePropertyName(InPropertyName, InValueType, InValueTypeObject, this);
	Bag.AddProperty(PropertyName, InValueType, InValueTypeObject);

	const FPropertyBagPropertyDesc* BagPropertyDesc = Bag.FindPropertyDescByName(InPropertyName);
	if (!ensure(BagPropertyDesc))
	{
		return nullptr;
	}
	
	// Create Property in Container
	URCVirtualPropertyInContainer* VirtualPropertyInContainer = NewObject<URCVirtualPropertyInContainer>(this, InPropertyClass.Get());
	VirtualPropertyInContainer->PropertyName = PropertyName;
	VirtualPropertyInContainer->PresetWeakPtr = PresetWeakPtr;
	VirtualPropertyInContainer->ContainerWeakPtr = this;
	VirtualPropertyInContainer->Id = FGuid::NewGuid();
	
	// Add property to Set
	VirtualProperties.Add(VirtualPropertyInContainer);

	return VirtualPropertyInContainer;
}

URCVirtualPropertyInContainer* URCVirtualPropertyContainerBase::DuplicateProperty(const FName& InPropertyName, const FProperty* InSourceProperty, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass)
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("DuplicateProperty", "Duplicate Property"));

	Modify();

	MarkPackageDirty();
#endif


	const FPropertyBagPropertyDesc* BagPropertyDesc = Bag.FindPropertyDescByName(InPropertyName);
	if (ensure(!BagPropertyDesc))
	{
		return nullptr;
	}

	Bag.AddProperty(InPropertyName, InSourceProperty);
	
	URCVirtualPropertyInContainer* VirtualPropertyInContainer = NewObject<URCVirtualPropertyInContainer>(this, InPropertyClass.Get());
	VirtualPropertyInContainer->PropertyName = InPropertyName;
	VirtualPropertyInContainer->PresetWeakPtr = PresetWeakPtr;
	VirtualPropertyInContainer->ContainerWeakPtr = this;
	VirtualPropertyInContainer->Id = FGuid::NewGuid();

	// Add property to Set
	VirtualProperties.Add(VirtualPropertyInContainer);

	return VirtualPropertyInContainer;
}

URCVirtualPropertyInContainer* URCVirtualPropertyContainerBase::DuplicatePropertyWithCopy(const FName& InPropertyName, const FProperty* InSourceProperty, const uint8* InSourceContainerPtr, TSubclassOf<URCVirtualPropertyInContainer> InPropertyClass)
{
	if (InSourceContainerPtr == nullptr)
	{
		return nullptr;
	}
	
	URCVirtualPropertyInContainer* VirtualPropertyInContainer = DuplicateProperty(InPropertyName, InSourceProperty, InPropertyClass);
	if (VirtualPropertyInContainer == nullptr)
	{
		return nullptr;
	}

	const FPropertyBagPropertyDesc* BagPropertyDesc = Bag.FindPropertyDescByName(InPropertyName);
	check(BagPropertyDesc); // Property bag should be exists after DuplicateProperty()

	ensure(Bag.SetValue(InPropertyName, InSourceProperty, InSourceContainerPtr) == EPropertyBagResult::Success);

	return VirtualPropertyInContainer;
}

bool URCVirtualPropertyContainerBase::RemoveProperty(const FName& InPropertyName)
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("RemoveProperty", "Remove Property"));

	Modify();

	MarkPackageDirty();
#endif

	Bag.RemovePropertyByName(InPropertyName);

	for (auto PropertiesIt = VirtualProperties.CreateIterator(); PropertiesIt; ++PropertiesIt)
	{
		if (const URCVirtualPropertyBase* VirtualProperty = *PropertiesIt)
		{
			if (VirtualProperty->PropertyName == InPropertyName)
			{
				PropertiesIt.RemoveCurrent();
				return true;
			}
		}
	}

	return false;
}

void URCVirtualPropertyContainerBase::Reset()
{
#if WITH_EDITOR
	const FScopedTransaction Transaction(LOCTEXT("EmptyProperties", "Empty Properties"));

	Modify();

	MarkPackageDirty();
#endif
	
	VirtualProperties.Empty();

	Bag.Reset();
}

URCVirtualPropertyBase* URCVirtualPropertyContainerBase::GetVirtualProperty(const FName InPropertyName) const
{
	for (URCVirtualPropertyBase* VirtualProperty : VirtualProperties)
	{
		if (VirtualProperty->PropertyName == InPropertyName)
		{
			return VirtualProperty;
		}
	}
	
	return nullptr;
}

URCVirtualPropertyBase* URCVirtualPropertyContainerBase::GetVirtualProperty(const FGuid& InId) const
{
	for (URCVirtualPropertyBase* VirtualProperty : VirtualProperties)
	{
		if (VirtualProperty->Id == InId)
		{
			return VirtualProperty;
		}
	}

	return nullptr;
}

URCVirtualPropertyBase* URCVirtualPropertyContainerBase::GetVirtualPropertyByDisplayName(const FName InDisplayName) const
{
	for (URCVirtualPropertyBase* VirtualProperty : VirtualProperties)
	{
		if (VirtualProperty->DisplayName == InDisplayName)
		{
			return VirtualProperty;
		}
	}

	return nullptr;
}

int32 URCVirtualPropertyContainerBase::GetNumVirtualProperties() const
{
	const int32 NumPropertiesInBag = Bag.GetNumPropertiesInBag();
	const int32 NumVirtualProperties = VirtualProperties.Num();

	check(NumPropertiesInBag == NumVirtualProperties);

	return NumPropertiesInBag;
}

TSharedPtr<FStructOnScope> URCVirtualPropertyContainerBase::CreateStructOnScope() const
{
	return MakeShared<FStructOnScope>(Bag.GetPropertyBagStruct(), Bag.GetMutableValue().GetMutableMemory());
}

FName URCVirtualPropertyContainerBase::GenerateUniquePropertyName(const FName& InPropertyName, const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject, const URCVirtualPropertyContainerBase* InContainer)
{
	auto GetFinalName = [](const FString& InPrefix, const int32 InIndex = 0)
	{
		FString FinalName = InPrefix;

		if (InIndex > 0)
		{
			FinalName += TEXT("_") + FString::FromInt(InIndex);
		}

		return FinalName;
	};


	FName DefaultName = URCVirtualPropertyBase::GetVirtualPropertyTypeDisplayName(InValueType, InValueTypeObject);
	
	int32 Index = 0;
	const FString InitialName = InPropertyName.IsNone() ? DefaultName.ToString() : InPropertyName.ToString();
	FString FinalName = InitialName;
	
	// Recursively search for an available name by incrementing suffix till we find one.
	const FPropertyBagPropertyDesc* PropertyDesc = InContainer->Bag.FindPropertyDescByName(*FinalName);
	while (PropertyDesc)
	{
		++Index;
		FinalName = GetFinalName(InitialName, Index);
		PropertyDesc = InContainer->Bag.FindPropertyDescByName(*FinalName);
	}

	return *FinalName;
}

#if WITH_EDITOR
void URCVirtualPropertyContainerBase::OnModifyPropertyValue(const FPropertyChangedEvent& PropertyChangedEvent)
{
	const FScopedTransaction Transaction(LOCTEXT("OnModifyPropertyValue", "On Modify Property Value"));

	Modify();

	MarkPackageDirty();
}
#endif

#undef LOCTEXT_NAMESPACE