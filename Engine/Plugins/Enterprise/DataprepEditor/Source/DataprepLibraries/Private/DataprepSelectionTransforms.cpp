// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepSelectionTransforms.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"

#define LOCTEXT_NAMESPACE "DataprepSelectionTransforms"

void UDataprepReferenceSelectionTransform::OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects)
{
	TSet<UObject*> Assets;

	for (UObject* Object : InObjects)
	{
		if (!ensure(Object) || Object->IsPendingKill())
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >(Object))
		{
			TArray<UActorComponent*> Components = Actor->GetComponents().Array();
			Components.Append(Actor->GetInstanceComponents());

			for (UActorComponent* Component : Components)
			{
				if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component))
				{
					if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
					{
						Assets.Add(StaticMesh);
					}

					for (UMaterialInterface* MaterialInterface : MeshComponent->OverrideMaterials)
					{
						Assets.Add(MaterialInterface);
					}
				}
			}
		}
		else if (UStaticMesh* StaticMesh = Cast< UStaticMesh >(Object))
		{
			for (FStaticMaterial& StaticMaterial : StaticMesh->StaticMaterials)
			{
				if (UMaterialInterface* MaterialInterface = StaticMaterial.MaterialInterface)
				{
					Assets.Add(MaterialInterface);
				}
			}

			if (bOutputCanIncludeInput)
			{
				Assets.Add(Object);
			}
		}
		else if (UMaterialInterface* MaterialInterface = Cast< UMaterialInterface >(Object))
		{
			Assets.Add(MaterialInterface);

			if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface))
			{
				if (MaterialInstance->Parent)
				{
					Assets.Add(MaterialInstance->Parent);
				}
			}

			// Collect textures
			TArray<UTexture*> Textures;
			MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::Num, true);
			for (UTexture* Texture : Textures)
			{
				Assets.Add(Texture);
			}

			if (bOutputCanIncludeInput)
			{
				Assets.Add(Object);
			}
		}
	}

	OutObjects.Append(Assets.Array());
}

void UDataprepHierarchySelectionTransform::OnExecution_Implementation(const TArray<UObject*>& InObjects, TArray<UObject*>& OutObjects)
{
	TArray<AActor*> ActorsToVisit;

	for (UObject* Object : InObjects)
	{
		if (!ensure(Object) || Object->IsPendingKill())
		{
			continue;
		}

		if (AActor* Actor = Cast< AActor >(Object))
		{
			TArray<AActor*> Children;
			Actor->GetAttachedActors( Children );

			ActorsToVisit.Append( Children );
		}
	}

	TSet<UObject*> NewSelection;

	while ( ActorsToVisit.Num() > 0)
	{
		AActor* VisitedActor = ActorsToVisit.Pop();
		if (VisitedActor == nullptr)
		{
			continue;
		}

		NewSelection.Add(VisitedActor);

		if(SelectionPolicy == EDataprepHierarchySelectionPolicy::AllDescendants)
		{
			// Continue with children
			TArray<AActor*> Children;
			VisitedActor->GetAttachedActors( Children );

			ActorsToVisit.Append( Children );
		}
	}

	OutObjects.Append(NewSelection.Array());

	if (bOutputCanIncludeInput)
	{
		OutObjects.Reserve( OutObjects.Num() + InObjects.Num());

		for (UObject* Object : InObjects)
		{
			if (!ensure(Object) || Object->IsPendingKill())
			{
				continue;
			}

			if (AActor* Actor = Cast< AActor >(Object))
			{
				OutObjects.Add(Object);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
