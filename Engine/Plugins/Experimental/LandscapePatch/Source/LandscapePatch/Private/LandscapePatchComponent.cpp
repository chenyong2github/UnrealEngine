// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapePatchComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Landscape.h"
#include "LandscapePatchLogging.h"
#include "LandscapePatchManager.h"

#define LOCTEXT_NAMESPACE "LandscapePatch"

#if WITH_EDITOR
namespace LandscapePatchComponentLocals
{
	ALandscapePatchManager* CreateNewPatchManagerForLandscape(ALandscape* Landscape)
	{
		if (!ensure(Landscape->CanHaveLayersContent()))
		{
			return nullptr;
		}

		FString BrushActorString = FString::Format(TEXT("{0}_{1}"), { Landscape->GetActorLabel(), ALandscapePatchManager::StaticClass()->GetName() });
		FName BrushActorName = MakeUniqueObjectName(Landscape->GetOuter(), ALandscapePatchManager::StaticClass(), FName(BrushActorString));
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = BrushActorName;
		SpawnParams.bAllowDuringConstructionScript = true; // This can be called by construction script if the actor being added to the world is part of a blueprint

		ALandscapePatchManager* PatchManager = Landscape->GetWorld()->SpawnActor<ALandscapePatchManager>(ALandscapePatchManager::StaticClass(), SpawnParams);
		if (PatchManager)
		{
			PatchManager->SetActorLabel(BrushActorString);
			PatchManager->SetTargetLandscape(Landscape);
		}

		return PatchManager;
	}
}
#endif

// Note that this is not allowed to be editor-only
ULandscapePatchComponent::ULandscapePatchComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Causes OnUpdateTransform to be called when the parent is moved. Note that this is better to do here in the
	// constructor, otherwise we'd need to do it both in OnComponentCreated and PostLoad.
	// We could keep this false if we were to register to TransformUpdated, since that gets broadcast either way.
	// TODO: Currently, neither TransformUpdated nor OnUpdateTransform are triggered when parent's transform is changed
	bWantsOnUpdateTransform = true;
}

#if WITH_EDITOR
void ULandscapePatchComponent::OnComponentCreated()
{
	using namespace LandscapePatchComponentLocals;

	Super::OnComponentCreated();
		
	// Mark whether we're creating from scratch of from a copy
	bWasCopy = bPropertiesCopiedIndicator;
	bPropertiesCopiedIndicator = true;

	UWorld* World = GetWorld();
	AActor* OwningActor = GetOwner();

	if (PatchManager.IsValid())
	{
		// If we copied over a patch manager, presumably Landscape should be
		// copied over as well, but might as well do this to be safe.
		Landscape = PatchManager->GetOwningLandscape();
	}
	else if (World)
	{
		if (Landscape.IsValid())
		{
			// If we copied over a patch with a landscape but no manager, create manager in that landscape
			if (Landscape->CanHaveLayersContent())
			{
				PatchManager = CreateNewPatchManagerForLandscape(Landscape.Get());
			}
			else
			{
				UE_LOG(LogLandscapePatch, Warning, TEXT("Landscape target for height patch did not have edit layers enabled. Unable to create patch manager."));
				Landscape = nullptr;
			}
		}
		else
		{
			// Didn't copy over an existing manager or landscape.

			// See if the level has a height patch manager to which we can add ourselves
			TActorIterator<ALandscapePatchManager> ManagerIterator(World);
			if (!!ManagerIterator)
			{
				PatchManager = *ManagerIterator;
				Landscape = PatchManager->GetOwningLandscape();
			}
			else
			{
				// If no existing manager, find some landscape and add a new one.
				for (TActorIterator<ALandscape> LandscapeIterator(World); LandscapeIterator; ++LandscapeIterator)
				{
					if (LandscapeIterator->CanHaveLayersContent())
					{
						Landscape = *LandscapeIterator;
						PatchManager = CreateNewPatchManagerForLandscape(Landscape.Get());
						break;
					}
				}
				if (!PatchManager.IsValid())
				{
					UE_LOG(LogLandscapePatch, Warning, TEXT("Unable to find a landscape with edit layers enabled. Unable to create patch manager."));
				}
			}
		}
	}

	if (PatchManager.IsValid())
	{
		PatchManager->AddPatch(this);
	}
}

void ULandscapePatchComponent::PostLoad()
{
	Super::PostLoad();

	bPropertiesCopiedIndicator = true;
	bLoadedButNotYetRegistered = true;
}

void ULandscapePatchComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (PatchManager.IsValid())
	{
		PatchManager->RemovePatch(this);
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void ULandscapePatchComponent::OnRegister()
{
	Super::OnRegister();


	if (bLoadedButNotYetRegistered)
	{
		bLoadedButNotYetRegistered = false;
		return;
	}

	// TODO: Currently the main reason to invalidate the landscape on registration is to respond
	// to detail panel changes of the parent's transform. However we may be able to catch a wide
	// variety of changes this way, so we'll need to see if we can get rid of other invalidations.
	// Also, we should make the invalidation conditional on whether we actually modify any relevant
	// properties by having a virtual method that compares and updates a stored hash of them.
	RequestLandscapeUpdate();
}

void ULandscapePatchComponent::GetActorDescProperties(FPropertyPairsMap& PropertyPairsMap) const
{
	Super::GetActorDescProperties(PropertyPairsMap);

	if (Landscape.IsValid())
	{
		PropertyPairsMap.AddProperty(ALandscape::AffectsLandscapeActorDescProperty, *Landscape->GetLandscapeGuid().ToString());
	}
}

void ULandscapePatchComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	RequestLandscapeUpdate();
}

void ULandscapePatchComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	using namespace LandscapePatchComponentLocals;

	Super::PostEditChangeProperty(PropertyChangedEvent);

	// If we're changing the owning landscape, there's some work we need to do to remove/add ourselves from/to the proper
	// brush managers.
	if (PropertyChangedEvent.Property 
		&& (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULandscapePatchComponent, Landscape)))
	{
		if (PatchManager.IsValid())
		{
			PatchManager->RemovePatch(this);
		}
		PatchManager = nullptr;

		bool bAdded = !Landscape.IsValid();
		for (TActorIterator<ALandscapePatchManager> ManagersItr(GetWorld()); ManagersItr; ++ManagersItr)
		{
			// Exit early if found the relevant manager.
			if (bAdded)
			{
				break;
			}

			ALandscapePatchManager* Manager = *ManagersItr;
			if (!ensure(Manager->IsValidLowLevel()))
			{
				continue;
			}

			if (!bAdded && Manager->GetOwningLandscape() == Landscape)
			{
				Manager->AddPatch(this);
				PatchManager = Manager;
				bAdded = true;
			}
		}

		// If we had a non-null landscape and didn't add ourselves to its manager despite iterating through
		// all managers, then the landscape must not have had a manager. Create one for it.
		if (!bAdded)
		{
			if (Landscape->CanHaveLayersContent())
			{
				PatchManager = CreateNewPatchManagerForLandscape(Landscape.Get());
				PatchManager->AddPatch(this);
			}
			else
			{
				UE_LOG(LogLandscapePatch, Warning, TEXT("Landscape target for height patch did not have edit layers enabled. Unable to create patch manager."));
				Landscape = nullptr;
			}
		}
	}

	RequestLandscapeUpdate();
}

void ULandscapePatchComponent::RequestLandscapeUpdate()
{
	if(PatchManager.IsValid())
	{
		PatchManager->RequestLandscapeUpdate();
	}
}

FTransform ULandscapePatchComponent::GetLandscapeHeightmapCoordsToWorld() const
{
	if (PatchManager.IsValid())
	{
		return PatchManager->GetHeightmapCoordsToWorld();
	}
	return FTransform::Identity;
}

#endif

#undef LOCTEXT_NAMESPACE
