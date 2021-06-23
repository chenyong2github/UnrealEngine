// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingBaseComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingRuntimeCommon.h"

#include "UObject/Package.h"


UDMXPixelMappingBaseComponent::UDMXPixelMappingBaseComponent()
{}

const FName& UDMXPixelMappingBaseComponent::GetNamePrefix()
{
	ensureMsgf(false, TEXT("You must implement GetNamePrefix() in your child class"));

	static FName NamePrefix = TEXT("");
	return NamePrefix;
}

TStatId UDMXPixelMappingBaseComponent::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDMXPixelMappingBaseComponent, STATGROUP_Tickables);
}

int32 UDMXPixelMappingBaseComponent::GetChildrenCount() const
{
	return Children.Num();
}

void UDMXPixelMappingBaseComponent::ForEachChild(TComponentPredicate Predicate, bool bIsRecursive)
{
	for (int32 ChildIdx = 0; ChildIdx < GetChildrenCount(); ChildIdx++)
	{
		if (UDMXPixelMappingBaseComponent* ChildComponent = GetChildAt(ChildIdx))
		{
			Predicate(ChildComponent);

			if (bIsRecursive)	
			{
				ForComponentAndChildren(ChildComponent, Predicate);
			}
		}
	}
}

UDMXPixelMapping* UDMXPixelMappingBaseComponent::GetPixelMapping()
{
	if (const UDMXPixelMappingRootComponent* RootComponent = GetRootComponent())
	{
		return Cast<UDMXPixelMapping>(RootComponent->GetOuter());
	}

	return nullptr;
}

const UDMXPixelMappingRootComponent* UDMXPixelMappingBaseComponent::GetRootComponent() const
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	// If this is the Root Component
	if (const UDMXPixelMappingRootComponent* ThisRootComponent = Cast<UDMXPixelMappingRootComponent>(this))
	{
		return ThisRootComponent;
	}
	// Try to get a root component from oobject owner
	else if(UDMXPixelMappingRootComponent* OuterRootComponent = Cast<UDMXPixelMappingRootComponent>(GetOuter()))
	{
		return OuterRootComponent;
	}
	else
	{
		UE_LOG(LogDMXPixelMappingRuntime, Warning, TEXT("Parent should be UDMXPixelMappingRootComponent!"));
		return nullptr;
	}
}

const UDMXPixelMappingRootComponent* UDMXPixelMappingBaseComponent::GetRootComponentChecked() const
{
	const UDMXPixelMappingRootComponent* RootComponent = GetRootComponent();
	check(RootComponent);
	return RootComponent;
}

UDMXPixelMappingRendererComponent* UDMXPixelMappingBaseComponent::GetRendererComponent()
{
	UDMXPixelMappingRendererComponent* RendererComponent = Cast<UDMXPixelMappingRendererComponent>(this);
	if (RendererComponent == nullptr)
	{
		RendererComponent = GetFirstParentByClass<UDMXPixelMappingRendererComponent>(this);
	}

	return RendererComponent;
}

void UDMXPixelMappingBaseComponent::ForComponentAndChildren(UDMXPixelMappingBaseComponent* Component, TComponentPredicate Predicate)
{
	if (Component != nullptr)
	{
		for (int32 ChildIdx = 0; ChildIdx < Component->GetChildrenCount(); ChildIdx++)
		{
			if (UDMXPixelMappingBaseComponent* ChildComponent = Component->GetChildAt(ChildIdx))
			{
				Predicate(ChildComponent);

				ForComponentAndChildren(ChildComponent, Predicate);
			}
		}
	}
}

#if WITH_EDITOR
FString UDMXPixelMappingBaseComponent::GetUserFriendlyName() const
{
	return GetName();
}
#endif // WITH_EDITOR

UDMXPixelMappingBaseComponent* UDMXPixelMappingBaseComponent::GetChildAt(int32 InIndex) const
{
	if (Children.IsValidIndex(InIndex))
	{
		return Children[InIndex];
	}

	return nullptr;
}

void UDMXPixelMappingBaseComponent::AddChild(UDMXPixelMappingBaseComponent* InComponent)
{
#if WITH_EDITOR
	ensureMsgf(InComponent, TEXT("Trying to add nullptr to %s"), *GetUserFriendlyName());
#endif 

	if (InComponent)
	{
#if WITH_EDITOR
		ensureMsgf(!Children.Contains(InComponent), TEXT("Trying to add %s to %s twice"), *InComponent->GetUserFriendlyName(), *GetUserFriendlyName());
#endif
		if (!Children.Contains(InComponent))
		{
			InComponent->Parent = this;

			Children.AddUnique(InComponent);

			InComponent->PostParentAssigned();
		}
	}
}


void UDMXPixelMappingBaseComponent::RemoveChild(UDMXPixelMappingBaseComponent* ChildComponent)
{
#if WITH_EDITOR
	ensureMsgf(ChildComponent || Children.Contains(ChildComponent), TEXT("Trying to remove child, but %s is not a child of %s."), *ChildComponent->GetUserFriendlyName(), *GetUserFriendlyName());
#endif

	if (ChildComponent)
	{
		ChildComponent->SetFlags(RF_Transactional);

		UDMXPixelMappingBaseComponent* ParentOfRemovedComponent = ChildComponent->Parent;
		if (ParentOfRemovedComponent)
		{
			ParentOfRemovedComponent->SetFlags(RF_Transactional);
			ParentOfRemovedComponent->Modify();
		}

		// Modify the component being removed.
		ChildComponent->Modify();

		// Rename the removed Component to the transient package so that it doesn't conflict with future Components sharing the same name.
		ChildComponent->Rename(nullptr, GetTransientPackage());

		// Remove childs recursively.
		TArray<UDMXPixelMappingBaseComponent*> ChildComponents;
		ChildComponent->GetChildComponentsRecursively(ChildComponents);
		for (UDMXPixelMappingBaseComponent* ChildOfChild : ChildComponents)
		{
			ChildComponent->RemoveChild(ChildOfChild);
		}

		Children.Remove(ChildComponent);
		ChildComponent->Parent = nullptr;

		ChildComponent->PostRemovedFromParent();
	}
}

void UDMXPixelMappingBaseComponent::ClearChildren()
{
	for (UDMXPixelMappingBaseComponent* Component : TArray<UDMXPixelMappingBaseComponent*>(Children))
	{
		RemoveChild(Component);
	}
}

void UDMXPixelMappingBaseComponent::GetChildComponentsRecursively(TArray<UDMXPixelMappingBaseComponent*>& Components)
{
	ForComponentAndChildren(this, [&Components](UDMXPixelMappingBaseComponent* InComponent) {
		Components.Add(InComponent);
	});
}
