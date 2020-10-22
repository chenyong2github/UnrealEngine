// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingEditorUtils.h"
#include "DMXPixelMappingComponentReference.h"
#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "DMXPixelMappingEditorCommon.h"

#include "Framework/Commands/GenericCommands.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FDMXPixelMappingEditorUtils"

bool FDMXPixelMappingEditorUtils::VerifyComponentRename(TSharedRef<FDMXPixelMappingToolkit> InToolkit, const FDMXPixelMappingComponentReference& InComponent, const FText& NewName, FText& OutErrorMessage)
{
	if (!InComponent.IsValid())
	{
		OutErrorMessage = LOCTEXT("InvalidComponentReference", "Invalid Component Reference");
		return false;
	}

	if (NewName.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("EmptyComponentName", "Empty Component Name");
		return false;
	}

	const FString& NewNameString = NewName.ToString();

	if (NewNameString.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("ComponentNameTooLong", "Component Name is Too Long");
		return false;
	}

	UDMXPixelMappingBaseComponent* ComponentToRename = InComponent.GetComponent();
	if (!ComponentToRename)
	{
		// In certain situations, the template might be lost due to mid recompile with focus lost on the rename box at
		// during a strange moment.
		return false;
	}

	// Slug the new name down to a valid object name
	const FName NewNameSlug = MakeObjectNameFromDisplayLabel(NewNameString, ComponentToRename->GetFName());

	UDMXPixelMapping* DMXPixelMapping = InToolkit->GetDMXPixelMapping();
	UDMXPixelMappingBaseComponent* ExistingComponent = DMXPixelMapping->FindComponent(NewNameSlug);

	if (ExistingComponent != nullptr)
	{
		if (ComponentToRename != ExistingComponent)
		{
			OutErrorMessage = LOCTEXT("ExistingComponentName", "Existing Component Name");
			return false;
		}
	}
	else
	{
		// check for redirectors too
		if (FindObject<UObject>(ComponentToRename->GetOuter(), *NewNameSlug.ToString()))
		{
			OutErrorMessage = LOCTEXT("ExistingOldComponentName", "Existing Old Component Name");
			return false;
		}
	}

	return true;
}

void FDMXPixelMappingEditorUtils::RenameComponent(TSharedRef<FDMXPixelMappingToolkit> InToolkit, const FName& OldObjectName, const FString& NewDisplayName)
{
	UDMXPixelMapping* DMXPixelMapping = InToolkit->GetDMXPixelMapping();
	check(DMXPixelMapping);

	UDMXPixelMappingBaseComponent* ComponentToRename = DMXPixelMapping->FindComponent(OldObjectName);
	check(ComponentToRename);

	// Get the new FName slug from the given display name
	const FName NewFName = MakeObjectNameFromDisplayLabel(NewDisplayName, ComponentToRename->GetFName());
	const FString NewNameStr = NewFName.ToString();

	ComponentToRename->Rename(*NewNameStr);

	InToolkit->OnComponentRenamed(ComponentToRename);
}

void FDMXPixelMappingEditorUtils::DeleteComponents(TSharedRef<FDMXPixelMappingToolkit> InToolkit, UDMXPixelMapping* InDMXPixelMapping, const TSet<FDMXPixelMappingComponentReference>& InComponents, bool bCreateTransaction)
{
	if (InComponents.Num() > 0)
	{
		if (bCreateTransaction)
		{
			const FScopedTransaction Transaction(LOCTEXT("RemoveComponent", "Remove Component"));
			InDMXPixelMapping->SetFlags(RF_Transactional);
		}

		InDMXPixelMapping->Modify();

		bool bRemoved = false;
		for (const FDMXPixelMappingComponentReference& ComponentRef : InComponents)
		{
			if (UDMXPixelMappingBaseComponent* ComponentToRemove = ComponentRef.GetComponent())
			{
				ComponentToRemove->SetFlags(RF_Transactional);

				UDMXPixelMappingBaseComponent* ParentOfRemovedComponent = ComponentToRemove->Parent;
				if (ParentOfRemovedComponent)
				{
					ParentOfRemovedComponent->SetFlags(RF_Transactional);
					ParentOfRemovedComponent->Modify();
				}

				// Modify the component being removed.
				ComponentToRemove->Modify();

				bRemoved = InDMXPixelMapping->RemoveComponent(ComponentToRemove);

				// Rename the removed Component to the transient package so that it doesn't conflict with future Components sharing the same name.
				ComponentToRemove->Rename(nullptr, GetTransientPackage());

				// Rename all child Components as well, to the transient package so that they don't conflict with future Components sharing the same name.
				TArray<UDMXPixelMappingBaseComponent*> ChildComponents;
				ComponentToRemove->GetChildComponentsRecursively(ChildComponents);
				for (UDMXPixelMappingBaseComponent* Component : ChildComponents)
				{
					Component->SetFlags(RF_Transactional);
					Component->Rename(nullptr, GetTransientPackage());
				}
			}
		}

		if (bRemoved)
		{
			InToolkit->BroadcastPostChange(InDMXPixelMapping);
		}
	}
}

UDMXPixelMappingRendererComponent* FDMXPixelMappingEditorUtils::AddRenderer(UDMXPixelMapping* InPixelMapping)
{
	if (InPixelMapping == nullptr)
	{
		UE_LOG(LogDMXPixelMappingEditor, Warning, TEXT("%S: InPixelMapping is nullptr"), __FUNCTION__);

		return nullptr;
	}	
	
	if (InPixelMapping->RootComponent == nullptr)
	{
		UE_LOG(LogDMXPixelMappingEditor, Warning, TEXT("%S: InPixelMapping->RootComponent is nullptr"), __FUNCTION__);

		return nullptr;
	}

	// Get root componet
	UDMXPixelMappingBaseComponent* RootComponent = InPixelMapping->RootComponent;

	// Create renderer name
	UDMXPixelMappingBaseComponent* DefaultComponent = UDMXPixelMappingRendererComponent::StaticClass()->GetDefaultObject<UDMXPixelMappingRendererComponent>();
	FName UniqueName = MakeUniqueObjectName(RootComponent, UDMXPixelMappingRendererComponent::StaticClass(), FName(TEXT("OutputMapping")));

	// Create new renderer and add to Root
	UDMXPixelMappingRendererComponent* Component = NewObject<UDMXPixelMappingRendererComponent>(RootComponent, UDMXPixelMappingRendererComponent::StaticClass(), UniqueName, RF_Transactional);
	RootComponent->AddChild(Component);

	return Component;
}

void FDMXPixelMappingEditorUtils::CreateComponentContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FDMXPixelMappingToolkit> InToolkit)
{
	MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
	{
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();
}

#undef LOCTEXT_NAMESPACE
