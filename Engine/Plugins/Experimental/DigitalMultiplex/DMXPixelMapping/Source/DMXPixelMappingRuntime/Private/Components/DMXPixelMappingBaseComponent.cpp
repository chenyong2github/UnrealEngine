// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DMXPixelMappingBaseComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "DMXPixelMapping.h"
#include "DMXPixelMappingRuntimeCommon.h"

UDMXPixelMappingBaseComponent::UDMXPixelMappingBaseComponent()
	: ChildIndex(INDEX_NONE)
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
	if (UDMXPixelMappingRootComponent* RootComponent = GetRootComponent())
	{
		return Cast<UDMXPixelMapping>(RootComponent->GetOuter());
	}

	return nullptr;
}

UDMXPixelMappingRootComponent* UDMXPixelMappingBaseComponent::GetRootComponent()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	// If this is UDMXPixelMappingRootComponent
	if (UDMXPixelMappingRootComponent* ThisRootComponent = Cast<UDMXPixelMappingRootComponent>(this))
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

int32 UDMXPixelMappingBaseComponent::AddChild(UDMXPixelMappingBaseComponent* InComponent)
{
	InComponent->Parent = this;

	InComponent->SetChildIndex(Children.Add(InComponent));

	return InComponent->ChildIndex;
}

void UDMXPixelMappingBaseComponent::SetChildIndex(int32 InIndex)
{
	ChildIndex = InIndex;
}

bool UDMXPixelMappingBaseComponent::RemoveChildAt(int32 InIndex)
{
	if (InIndex < 0 || InIndex >= Children.Num())
	{
		return false;
	}

	UDMXPixelMappingBaseComponent* ChildComponent = Children[InIndex];
	Children.RemoveAt(InIndex);
	ChildComponent->Parent = nullptr;

	// Reset indexes for all components
	for (int32 ChildIdx = 0; ChildIdx < GetChildrenCount(); ChildIdx++)
	{
		if (Children[ChildIdx])
		{
			Children[ChildIdx]->SetChildIndex(ChildIdx);
		}
	}

	return true;
}

bool UDMXPixelMappingBaseComponent::RemoveChild(UDMXPixelMappingBaseComponent* InComponent)
{
	if (InComponent->Parent == nullptr)
	{
		return false;
	}

	int32 ChildIdx = InComponent->GetChildIndex();
	if (ChildIdx != -1)
	{
		return RemoveChildAt(ChildIdx);
	}

	return false;
}

void UDMXPixelMappingBaseComponent::ClearChildren()
{
	for (int32 ChildIdx = 0; ChildIdx < GetChildrenCount(); ChildIdx++)
	{
		UDMXPixelMappingBaseComponent* ChildComponent = Children[ChildIdx];
		if (ChildComponent)
		{
			Children.RemoveAt(ChildIdx);
			ChildComponent->Parent = nullptr;
		}
	}
}

void UDMXPixelMappingBaseComponent::GetChildComponentsRecursively(TArray<UDMXPixelMappingBaseComponent*>& Components)
{
	ForComponentAndChildren(this, [&Components](UDMXPixelMappingBaseComponent* InComponent) {
		Components.Add(InComponent);
	});
}
