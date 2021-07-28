// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderMeshSimplify.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Components/StaticMeshComponent.h"

#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"

#include "IMeshMergeUtilities.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"

#include "Materials/Material.h"
#include "Engine/HLODProxy.h"
#include "Serialization/ArchiveCrc32.h"

#include "HLODBuilderInstancing.h"


UHLODBuilderMeshSimplify::UHLODBuilderMeshSimplify(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderMeshSimplifySettings::UHLODBuilderMeshSimplifySettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsTemplate())
	{
		HLODMaterial = GEngine->DefaultHLODFlattenMaterial;
	}
#endif
}

uint32 UHLODBuilderMeshSimplifySettings::GetCRC() const
{
	UHLODBuilderMeshSimplifySettings& This = *const_cast<UHLODBuilderMeshSimplifySettings*>(this);

	FArchiveCrc32 Ar;

	Ar << This.MeshSimplifySettings;
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - MeshSimplifySettings = %d"), Ar.GetCrc());

	uint32 Hash = Ar.GetCrc();

	if (!HLODMaterial.IsNull())
	{
		UMaterialInterface* Material = HLODMaterial.LoadSynchronous();
		if (Material)
		{
			uint32 MaterialCRC = UHLODProxy::GetCRC(Material);
			UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - Material = %d"), MaterialCRC);
			Hash = HashCombine(Hash, MaterialCRC);
		}
	}

	return Hash;
}

UHLODBuilderSettings* UHLODBuilderMeshSimplify::CreateSettings(UHLODLayer* InHLODLayer) const
{
	UHLODBuilderMeshSimplifySettings* HLODBuilderSettings = NewObject<UHLODBuilderMeshSimplifySettings>(InHLODLayer);

	// If previous settings object is null, this means we have an older version of the object. Populate with the deprecated settings.
	if (InHLODLayer->GetHLODBuilderSettings() == nullptr)
	{
		HLODBuilderSettings->MeshSimplifySettings = InHLODLayer->MeshSimplifySettings_DEPRECATED;
		HLODBuilderSettings->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED;
	}

	return HLODBuilderSettings;
}

TArray<UPrimitiveComponent*> UHLODBuilderMeshSimplify::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderMeshSimplifySettings::CreateComponents);

	TArray<UObject*> Assets;
	FCreateProxyDelegate ProxyDelegate;
	ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	TArray<UPrimitiveComponent*> InstancedComponents;

	// Filter the input components
	for (UPrimitiveComponent* SubComponent : InSubComponents)
	{
		if (!SubComponent)
		{
			continue;
		}

		switch (SubComponent->HLODBatchingPolicy)
		{
		case EHLODBatchingPolicy::None:
			if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(SubComponent))
			{
				StaticMeshComponents.Add(SMC);
			}
			break;
		case EHLODBatchingPolicy::Instancing:
			InstancedComponents.Add(SubComponent);
			break;
		case EHLODBatchingPolicy::MeshSection:
			InstancedComponents.Add(SubComponent);
			UE_LOG(LogHLODBuilder, Warning, TEXT("EHLODBatchingPolicy::MeshSection is not yet supported by the MeshApproximate builder."));
			break;
		}
	}

	const UHLODBuilderMeshSimplifySettings* MeshSimplifySettings = CastChecked<UHLODBuilderMeshSimplifySettings>(InHLODLayer->GetHLODBuilderSettings());
	const FMeshProxySettings& UseSettings = MeshSimplifySettings->MeshSimplifySettings;
	UMaterial* HLODMaterial = MeshSimplifySettings->HLODMaterial.LoadSynchronous();

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
	MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, UseSettings, HLODMaterial, InHLODActor->GetPackage(), InHLODActor->GetActorLabel(), FGuid::NewGuid(), ProxyDelegate, true);

	UStaticMeshComponent* Component = nullptr;
	Algo::ForEach(Assets, [this, InHLODActor, &Component](UObject* Asset)
	{
		Asset->ClearFlags(RF_Public | RF_Standalone);

		if (Cast<UStaticMesh>(Asset))
		{
			Component = NewObject<UStaticMeshComponent>(InHLODActor);
			Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
		}
	});

	TArray<UPrimitiveComponent*> Components;
	Components.Add(Component);

	// Batch instances
	if (InstancedComponents.Num())
	{
		UHLODBuilderInstancing* InstancingHLODBuilder = NewObject<UHLODBuilderInstancing>();
		Components.Append(InstancingHLODBuilder->CreateComponents(InHLODActor, nullptr, InstancedComponents));
	}

	return Components;
}
