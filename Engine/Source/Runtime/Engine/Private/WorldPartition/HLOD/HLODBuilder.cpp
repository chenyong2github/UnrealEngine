// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODBuilder.h"

#include "AssetCompilingManager.h"

#include "Engine/HLODProxy.h"

DEFINE_LOG_CATEGORY(LogHLODBuilder);


UHLODBuilder::UHLODBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderSettings::UHLODBuilderSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR

TSubclassOf<UHLODBuilderSettings> UHLODBuilder::GetSettingsClass() const
{
	return UHLODBuilderSettings::StaticClass();
}

void UHLODBuilder::SetHLODBuilderSettings(const UHLODBuilderSettings* InHLODBuilderSettings)
{
	check(InHLODBuilderSettings->IsA(GetSettingsClass()));
	HLODBuilderSettings = InHLODBuilderSettings;
}

bool UHLODBuilder::RequiresCompiledAssets() const
{
	return true;
}

bool UHLODBuilder::RequiresWarmup() const
{
	return true;
}

static TArray<UActorComponent*> GatherHLODRelevantComponents(const TArray<AActor*>& InSourceActors)
{
	TArray<UActorComponent*> HLODRelevantComponents;

	for (AActor* Actor : InSourceActors)
	{
		if (!Actor->IsHLODRelevant())
		{
			continue;
		}

		for (UActorComponent* SubComponent : Actor->GetComponents())
		{
			if (SubComponent && SubComponent->IsHLODRelevant())
			{
				HLODRelevantComponents.Add(SubComponent);
			}
		}
	}

	return HLODRelevantComponents;
}

uint32 UHLODBuilder::ComputeHLODHash(const UActorComponent* InSourceComponent) const
{
	uint32 ComponentCRC = 0;

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(const_cast<UActorComponent*>(InSourceComponent)))
	{
		UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - Component \'%s\' from actor \'%s\'"), *StaticMeshComponent->GetName(), *StaticMeshComponent->GetOwner()->GetName());

		// CRC component
		ComponentCRC = UHLODProxy::GetCRC(StaticMeshComponent);
		UE_LOG(LogHLODBuilder, VeryVerbose, TEXT("     - StaticMeshComponent (%s) = %x"), *StaticMeshComponent->GetName(), ComponentCRC);

		// CRC static mesh
		if (UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh())
		{
			ComponentCRC = UHLODProxy::GetCRC(StaticMesh, ComponentCRC);
		}

		// CRC materials
		const int32 NumMaterials = StaticMeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
		{
			UMaterialInterface* MaterialInterface = StaticMeshComponent->GetMaterial(MaterialIndex);
			if (MaterialInterface)
			{
				ComponentCRC = UHLODProxy::GetCRC(MaterialInterface, ComponentCRC);

				TArray<UTexture*> Textures;
				MaterialInterface->GetUsedTextures(Textures, EMaterialQualityLevel::High, true, ERHIFeatureLevel::SM5, true);
				for (UTexture* Texture : Textures)
				{
					ComponentCRC = UHLODProxy::GetCRC(Texture, ComponentCRC);
				}
			}
		}
	}
	else
	{
		ComponentCRC = FMath::Rand();
		UE_LOG(LogHLODBuilder, Warning, TEXT("Can't compute HLOD hash for component of type %s, assuming it is dirty."), *InSourceComponent->GetClass()->GetName());
		
	}

	return ComponentCRC;
}

uint32 UHLODBuilder::ComputeHLODHash(const TArray<AActor*>& InSourceActors)
{
	// We get the CRC of each component
	TArray<uint32> ComponentsCRCs;

	for (UActorComponent* SourceComponent : GatherHLODRelevantComponents(InSourceActors))
	{
		TSubclassOf<UHLODBuilder> HLODBuilderClass = SourceComponent->GetCustomHLODBuilderClass();
		if (!HLODBuilderClass)
		{
			HLODBuilderClass = UHLODBuilder::StaticClass();
		}

		uint32 ComponentHash = HLODBuilderClass->GetDefaultObject<UHLODBuilder>()->ComputeHLODHash(SourceComponent);
		ComponentsCRCs.Add(ComponentHash);
	}

	// Sort the components CRCs to ensure the order of components won't have an impact on the final CRC
	ComponentsCRCs.Sort();

	return FCrc::MemCrc32(ComponentsCRCs.GetData(), ComponentsCRCs.Num() * ComponentsCRCs.GetTypeSize());
}

TArray<UActorComponent*> UHLODBuilder::Build(const FHLODBuildContext& InHLODBuildContext, const TArray<AActor*>& InSourceActors) const
{
	TMap<TSubclassOf<UHLODBuilder>, TArray<UActorComponent*>> HLODBuildersForComponents;

	// Gather custom HLOD builders, and regroup all components by builders
	for (UActorComponent* SourceComponent : GatherHLODRelevantComponents(InSourceActors))
	{
		TSubclassOf<UHLODBuilder> HLODBuilderClass = SourceComponent->GetCustomHLODBuilderClass();
		HLODBuildersForComponents.FindOrAdd(HLODBuilderClass).Add(SourceComponent);
	}

	// If any of the builders requires it, wait for assets compilation to finish
	for (const auto& HLODBuilderPair : HLODBuildersForComponents)
	{
		const UHLODBuilder* HLODBuilder = HLODBuilderPair.Key ? HLODBuilderPair.Key->GetDefaultObject<UHLODBuilder>() : this;
		if (HLODBuilder->RequiresCompiledAssets())
		{
			FAssetCompilingManager::Get().FinishAllCompilation();
			break;
		}
	}	
	
	// Build HLOD components by sending source components to the individual builders, in batch
	TArray<UActorComponent*> HLODComponents;
	for (const auto& HLODBuilderPair : HLODBuildersForComponents)
	{
		// If no custom HLOD builder is provided, use this current builder.
		const UHLODBuilder* HLODBuilder = HLODBuilderPair.Key ? HLODBuilderPair.Key->GetDefaultObject<UHLODBuilder>() : this;
		const TArray<UActorComponent*>& SourceComponents = HLODBuilderPair.Value;

		TArray<UActorComponent*> NewComponents = HLODBuilder->Build(InHLODBuildContext, SourceComponents);
		HLODComponents.Append(NewComponents);
	}

	// In case a builder returned null entries, clean the array.
	HLODComponents.RemoveSwap(nullptr);

	return HLODComponents;
}

#endif // WITH_EDITOR
