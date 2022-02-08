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

TSubclassOf<UHLODBuilderSettings> UHLODBuilderMeshSimplify::GetSettingsClass() const
{
	return UHLODBuilderMeshSimplifySettings::StaticClass();
}

TArray<UActorComponent*> UHLODBuilderMeshSimplify::Build(const FHLODBuildContext& InHLODBuildContext, const TArray<UActorComponent*>& InSourceComponents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderMeshSimplifySettings::Build);

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	TArray<UActorComponent*> InstancedComponents;

	// Filter the input components
	for (UStaticMeshComponent* SubComponent : FilterComponents<UStaticMeshComponent>(InSourceComponents))
	{
		switch (SubComponent->HLODBatchingPolicy)
		{
		case EHLODBatchingPolicy::None:
			StaticMeshComponents.Add(SubComponent);
			break;
		case EHLODBatchingPolicy::Instancing:
			InstancedComponents.Add(SubComponent);
			break;
		case EHLODBatchingPolicy::MeshSection:
			InstancedComponents.Add(SubComponent);
			UE_LOG(LogHLODBuilder, Warning, TEXT("EHLODBatchingPolicy::MeshSection is not yet supported by the UHLODBuilderMeshSimplify builder."));
			break;
		}
	}

	const UHLODBuilderMeshSimplifySettings* MeshSimplifySettings = CastChecked<UHLODBuilderMeshSimplifySettings>(HLODBuilderSettings);
	const FMeshProxySettings& UseSettings = MeshSimplifySettings->MeshSimplifySettings;
	UMaterial* HLODMaterial = MeshSimplifySettings->HLODMaterial.LoadSynchronous();

	TArray<UObject*> Assets;
	FCreateProxyDelegate ProxyDelegate;
	ProxyDelegate.BindLambda([&Assets](const FGuid Guid, TArray<UObject*>& InAssetsCreated) { Assets = InAssetsCreated; });

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
	MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, UseSettings, HLODMaterial, InHLODBuildContext.AssetsOuter->GetPackage(), InHLODBuildContext.AssetsBaseName, FGuid::NewGuid(), ProxyDelegate, true);

	UStaticMeshComponent* Component = nullptr;
	Algo::ForEach(Assets, [this, &Component](UObject* Asset)
	{
		Asset->ClearFlags(RF_Public | RF_Standalone);

		if (Cast<UStaticMesh>(Asset))
		{
			Component = NewObject<UStaticMeshComponent>();
			Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
		}
	});

	TArray<UActorComponent*> Components;
	Components.Add(Component);

	// Batch instances
	if (InstancedComponents.Num())
	{
		UHLODBuilderInstancing* InstancingHLODBuilder = NewObject<UHLODBuilderInstancing>();
		Components.Append(InstancingHLODBuilder->Build(InHLODBuildContext, InstancedComponents));
	}

	return Components;
}
