// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingDMXLibraryViewModel.h"

#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Toolkits/DMXPixelMappingToolkit.h"

#include "Editor.h"


UDMXPixelMappingDMXLibraryViewModel* UDMXPixelMappingDMXLibraryViewModel::CreateNew(const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit, UDMXPixelMappingFixtureGroupComponent* InFixtureGroup)
{
	UDMXPixelMappingDMXLibraryViewModel* NewModel = NewObject<UDMXPixelMappingDMXLibraryViewModel>(GetTransientPackage(), NAME_None, RF_Transactional);
	NewModel->WeakToolkit = InToolkit;
	NewModel->WeakFixtureGroupComponent = InFixtureGroup;
	NewModel->DMXLibrary = InFixtureGroup ? InFixtureGroup->DMXLibrary : nullptr;

	if (NewModel->WeakFixtureGroupComponent.IsValid())
	{
		NewModel->WeakFixtureGroupComponent->GetOnDMXLibraryChanged().AddUObject(NewModel, &UDMXPixelMappingDMXLibraryViewModel::UpdateDMXLibraryFromComponent);
	}

	return NewModel;
}

UDMXPixelMappingFixtureGroupComponent* UDMXPixelMappingDMXLibraryViewModel::GetFixtureGroupComponent() const
{
	return WeakFixtureGroupComponent.Get();
}

UDMXPixelMappingRootComponent* UDMXPixelMappingDMXLibraryViewModel::GetPixelMappingRootComponent() const
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.IsValid() ? WeakToolkit.Pin() : nullptr;
	UDMXPixelMapping* PixelMapping = Toolkit.IsValid() ? Toolkit->GetDMXPixelMapping() : nullptr;

	return PixelMapping ? PixelMapping->GetRootComponent() : nullptr;
}

bool UDMXPixelMappingDMXLibraryViewModel::IsFixtureGroupOrChildSelected() const
{
	if (!WeakFixtureGroupComponent.IsValid() || !WeakToolkit.IsValid())
	{
		return false;
	}

	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = WeakToolkit.Pin()->GetSelectedComponents();
	for (const FDMXPixelMappingComponentReference& ComponentReference : SelectedComponents)
	{
		UDMXPixelMappingBaseComponent* Component = ComponentReference.GetComponent();
		do
		{
			if (Component == WeakFixtureGroupComponent)
			{
				return true;
			}

			Component = Component->GetParent();
		} while (Component);
	}

	return false;
}

const TArray<FDMXEntityFixturePatchRef> UDMXPixelMappingDMXLibraryViewModel::GetFixturePatchesInUse() const
{
	TArray<FDMXEntityFixturePatchRef> FixturePatchesInUse;

	const TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents = GetFixtureGroupComponentsOfSameLibrary();
	for (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent : FixtureGroupComponents)
	{
		for (UDMXPixelMappingBaseComponent* Child : FixtureGroupComponent->GetChildren())
		{
			if (UDMXPixelMappingFixtureGroupItemComponent* GroupItem = Cast<UDMXPixelMappingFixtureGroupItemComponent>(Child))
			{
				if (GroupItem->FixturePatchRef.DMXLibrary == DMXLibrary)
				{
					FixturePatchesInUse.Add(GroupItem->FixturePatchRef);
				}
			}
			else if (UDMXPixelMappingMatrixComponent* MatrixComponent = Cast<UDMXPixelMappingMatrixComponent>(Child))
			{
				if (MatrixComponent->FixturePatchRef.DMXLibrary == DMXLibrary)
				{
					FixturePatchesInUse.Add(MatrixComponent->FixturePatchRef);
				}
			}
		}
	}

	return FixturePatchesInUse;
}

void UDMXPixelMappingDMXLibraryViewModel::SaveFixturePatchListDescriptor(const FDMXReadOnlyFixturePatchListDescriptor& NewDescriptor)
{
	FixturePatchListDescriptor = NewDescriptor;
	SaveConfig();
}

void UDMXPixelMappingDMXLibraryViewModel::PostUndo(bool bSuccess)
{
	UpdateDMXLibraryFromComponent();
}

void UDMXPixelMappingDMXLibraryViewModel::PostRedo(bool bSuccess)
{
	UpdateDMXLibraryFromComponent();
}

void UDMXPixelMappingDMXLibraryViewModel::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	TGuardValue<bool> ChangingPropertiesGuard(bChangingProperties, true);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDMXPixelMappingDMXLibraryViewModel, DMXLibrary))
	{
		if (WeakFixtureGroupComponent.IsValid())
		{
			WeakFixtureGroupComponent->PreEditChange(UDMXPixelMappingFixtureGroupComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, DMXLibrary)));
			WeakFixtureGroupComponent->DMXLibrary = DMXLibrary;
			WeakFixtureGroupComponent->PostEditChange();

			RemoveInvalidPatches();

			OnDMXLibraryChanged.Broadcast();
		}
	}
}

void UDMXPixelMappingDMXLibraryViewModel::UpdateDMXLibraryFromComponent()
{
	if (bChangingProperties)
	{
		return;
	}

	UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = WeakFixtureGroupComponent.Get();
	if (FixtureGroupComponent)
	{
		Modify();
		DMXLibrary = FixtureGroupComponent->DMXLibrary;
		RemoveInvalidPatches();
	}
}

void UDMXPixelMappingDMXLibraryViewModel::RemoveInvalidPatches()
{
	if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = WeakFixtureGroupComponent.Get())
	{
		TArray<UDMXPixelMappingBaseComponent*> CachedChildren(WeakFixtureGroupComponent->Children);
		for (UDMXPixelMappingBaseComponent* ChildComponent : CachedChildren)
		{
			if (UDMXPixelMappingFixtureGroupItemComponent* ChildGroupItem = Cast<UDMXPixelMappingFixtureGroupItemComponent>(ChildComponent))
			{
				if (ChildGroupItem->FixturePatchRef.GetFixturePatch() &&
					ChildGroupItem->FixturePatchRef.GetFixturePatch()->GetParentLibrary() != FixtureGroupComponent->DMXLibrary)
				{
					FixtureGroupComponent->RemoveChild(ChildGroupItem);
				}
			}
			else if (UDMXPixelMappingMatrixComponent* ChildMatrix = Cast<UDMXPixelMappingMatrixComponent>(ChildComponent))
			{
				if (ChildMatrix->FixturePatchRef.GetFixturePatch() &&
					ChildMatrix->FixturePatchRef.GetFixturePatch()->GetParentLibrary() != FixtureGroupComponent->DMXLibrary)
				{
					FixtureGroupComponent->RemoveChild(ChildMatrix);
				}
			}
		};
	}
}

TArray<UDMXPixelMappingFixtureGroupComponent*> UDMXPixelMappingDMXLibraryViewModel::GetFixtureGroupComponentsOfSameLibrary() const
{
	TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents;
	UDMXPixelMappingRendererComponent* RendererComponent = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetActiveRendererComponent() : nullptr;
	if (RendererComponent)
	{
		for (UDMXPixelMappingBaseComponent* Child : RendererComponent->GetChildren())
		{
			if (UDMXPixelMappingFixtureGroupComponent* GroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Child))
			{
				FixtureGroupComponents.Add(GroupComponent);
			}
		}
	}

	return FixtureGroupComponents;
}
