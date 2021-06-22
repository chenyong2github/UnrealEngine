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

#include "HLODBuilderInstancing.h"


TArray<UPrimitiveComponent*> FHLODBuilder_MeshSimplify::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHLODBuilder_MeshSimplify::CreateComponents);

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

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
	MeshMergeUtilities.CreateProxyMesh(StaticMeshComponents, InHLODLayer->GetMeshSimplifySettings(), InHLODLayer->GetHLODMaterial().LoadSynchronous(), InHLODActor->GetPackage(), InHLODActor->GetActorLabel(), FGuid::NewGuid(), ProxyDelegate, true);

	UStaticMeshComponent* Component = nullptr;
	Algo::ForEach(Assets, [this, InHLODActor, &Component](UObject* Asset)
	{
		Asset->ClearFlags(RF_Public | RF_Standalone);

		if (Cast<UStaticMesh>(Asset))
		{
			Component = NewObject<UStaticMeshComponent>(InHLODActor);
			Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
			DisableCollisions(Component);
		}
	});

	TArray<UPrimitiveComponent*> Components;
	Components.Add(Component);

	// Batch instances
	if (InstancedComponents.Num())
	{
		FHLODBuilder_Instancing InstancingHLODBuilder;
		Components.Append(InstancingHLODBuilder.CreateComponents(InHLODActor, nullptr, InstancedComponents));
	}

	return Components;
}
