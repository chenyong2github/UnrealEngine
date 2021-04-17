// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterSceneComponent.h"

#include "Config/IPDisplayClusterConfigManager.h"

#include "DisplayClusterRootActor.h"
#include "Blueprints/DisplayClusterBlueprintGeneratedClass.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterTypesConverter.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

#if WITH_EDITOR
#include "Interfaces/IDisplayClusterConfiguratorBlueprintEditor.h"
#endif


UDisplayClusterSceneComponent::UDisplayClusterSceneComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UDisplayClusterSceneComponent::DoesComponentBelongToBlueprint() const
{
	bool bIsForBlueprint = false;
	if (ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOwner()))
	{
		if (RootActor->IsBlueprint())
		{
			bIsForBlueprint = true;
		}
	}
	else if (GetTypedOuter<UDisplayClusterBlueprintGeneratedClass>() != nullptr || GetTypedOuter<UDynamicClass>() != nullptr)
	{
		bIsForBlueprint = true;
	}
	
	return bIsForBlueprint;
}

void UDisplayClusterSceneComponent::ApplyConfigurationData()
{
	if (DoesComponentBelongToBlueprint())
	{
		/*
			Blueprint already contains component information, position, and heirarchy.
			When this isn't a blueprint (such as config data only or on initial import) we can apply config data.
		*/
		return;
	}
	
	if (ConfigData)
	{
		// Take place in hierarchy
		if (!ConfigData->ParentId.IsEmpty())
		{
			ADisplayClusterRootActor* const RootActor = Cast<ADisplayClusterRootActor>(GetOwner());
			if (RootActor)
			{
				UDisplayClusterSceneComponent* const ParentComp = RootActor->GetComponentById(ConfigData->ParentId);
				if (ParentComp)
				{
					UE_LOG(LogDisplayClusterGame, Log, TEXT("Attaching %s to %s"), *GetName(), *ParentComp->GetName());
					AttachToComponent(ParentComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
				}
				else
				{
					UE_LOG(LogDisplayClusterGame, Warning, TEXT("Couldn't attach %s to %s"), *GetName(), *ConfigData->ParentId);
				}
			}
		}

		// Set up location and rotation
		SetRelativeLocationAndRotation(ConfigData->Location, ConfigData->Rotation);
	}
}

#if WITH_EDITOR
UObject* UDisplayClusterSceneComponent::GetObject() const
{
	return ConfigData;
}

bool UDisplayClusterSceneComponent::IsSelected()
{
	ADisplayClusterRootActor* RootActor = Cast<ADisplayClusterRootActor>(GetOwner());
	if (RootActor)
	{
		// TODO: Refactor this. We shouldn't need to check the BlueprintEditor from the runtime module ever. -Patrick
		TSharedPtr<IDisplayClusterConfiguratorBlueprintEditor> Toolkit = RootActor->GetToolkit().Pin();
		if (Toolkit.IsValid())
		{
			const TArray<UObject*>& SelectedObjects = Toolkit->GetSelectedObjects();

			UObject* const* SelectedObject = SelectedObjects.FindByPredicate([this](const UObject* InObject)
			{
				return InObject == GetObject();
			});

			return SelectedObject != nullptr;
		}
	}

	return false;
}
#endif
