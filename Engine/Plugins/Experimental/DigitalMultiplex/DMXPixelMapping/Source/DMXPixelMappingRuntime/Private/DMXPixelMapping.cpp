// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "UObject/LinkerLoad.h"

UDMXPixelMapping::~UDMXPixelMapping()
{
#if WITH_EDITOR
	OnEditorRebuildChildrenComponentsDelegate.Unbind();
#endif // WITH_EDITOR
}

void UDMXPixelMapping::PostLoad()
{
	Super::PostLoad();

	CreateOrLoadObjects();
}

void UDMXPixelMapping::PreloadWithChildren()
{
	if (HasAnyFlags(RF_NeedLoad))
	{
		GetLinker()->Preload(this);
	}

	ForEachComponent([this](UDMXPixelMappingBaseComponent* InComponent)
	{
		if ((InComponent != nullptr) && InComponent->HasAnyFlags(RF_NeedLoad))
		{
			InComponent->GetLinker()->Preload(InComponent);
		}
	});
}

void UDMXPixelMapping::CreateOrLoadObjects()
{
	// Create RootComponent if it doesn't exist.
	if (RootComponent == nullptr)
	{
		UDMXPixelMappingRootComponent* DefaultComponent = UDMXPixelMappingRootComponent::StaticClass()->GetDefaultObject<UDMXPixelMappingRootComponent>();
		FName UniqueName = MakeUniqueObjectName(this, UDMXPixelMappingRootComponent::StaticClass(), DefaultComponent->GetNamePrefix());

		RootComponent = NewObject<UDMXPixelMappingRootComponent>(this, UDMXPixelMappingRootComponent::StaticClass(), UniqueName, RF_Transactional);
	}
}

UDMXPixelMappingBaseComponent* UDMXPixelMapping::FindComponent(const FName& InName) const
{
	UDMXPixelMappingBaseComponent* FoundComponent = nullptr;

	ForEachComponent([&](UDMXPixelMappingBaseComponent* InComponent) {
		if (InComponent->GetFName() == InName)
		{
			FoundComponent = InComponent;
		}
	});

	return FoundComponent;
}

#if WITH_EDITOR
UDMXPixelMappingOutputComponent* UDMXPixelMapping::FindComponent(TSharedPtr<SWidget> InWidget) const
{
	UDMXPixelMappingOutputComponent* FoundComponent = nullptr;

	ForEachComponentOfClass<UDMXPixelMappingOutputComponent>([&](UDMXPixelMappingOutputComponent* InComponent) {
		if (InComponent->GetCachedWidget() == InWidget)
		{
			FoundComponent = InComponent;
		}
	});

	return FoundComponent;
}

#endif // WITH_EDITOR

bool UDMXPixelMapping::RemoveComponent(UDMXPixelMappingBaseComponent* InComponent)
{
	if (UDMXPixelMappingBaseComponent* Parent = InComponent->Parent)
	{
		if (InComponent != RootComponent &&Parent->RemoveChild(InComponent))
		{
			return true;
		}
	}

	return false;
}

void UDMXPixelMapping::ForEachComponent(TComponentPredicate Predicate) const
{
	if (RootComponent)
	{
		Predicate(RootComponent);

		UDMXPixelMappingBaseComponent::ForComponentAndChildren(RootComponent, Predicate);
	}
}
