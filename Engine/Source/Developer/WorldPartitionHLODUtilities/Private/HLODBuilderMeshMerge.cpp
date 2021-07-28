// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLODBuilderMeshMerge.h"

#include "Algo/ForEach.h"
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


UHLODBuilderMeshMerge::UHLODBuilderMeshMerge(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UHLODBuilderMeshMergeSettings::UHLODBuilderMeshMergeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsTemplate())
	{
		HLODMaterial = GEngine->DefaultHLODFlattenMaterial;
	}
#endif
}

uint32 UHLODBuilderMeshMergeSettings::GetCRC() const
{
	UHLODBuilderMeshMergeSettings& This = *const_cast<UHLODBuilderMeshMergeSettings*>(this);

	FArchiveCrc32 Ar;

	Ar << This.MeshMergeSettings;
	UE_LOG(LogHLODBuilder, VeryVerbose, TEXT(" - MeshMergeSettings = %d"), Ar.GetCrc());

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

UHLODBuilderSettings* UHLODBuilderMeshMerge::CreateSettings(UHLODLayer* InHLODLayer) const
{
	UHLODBuilderMeshMergeSettings* HLODBuilderSettings = NewObject<UHLODBuilderMeshMergeSettings>(InHLODLayer);

	// If previous settings object is null, this means we have an older version of the object. Populate with the deprecated settings.
	if (InHLODLayer->GetHLODBuilderSettings() == nullptr)
	{
		HLODBuilderSettings->MeshMergeSettings = InHLODLayer->MeshMergeSettings_DEPRECATED;
		HLODBuilderSettings->HLODMaterial = InHLODLayer->HLODMaterial_DEPRECATED;
	}

	return HLODBuilderSettings;
}

TArray<UPrimitiveComponent*> UHLODBuilderMeshMerge::CreateComponents(AWorldPartitionHLOD* InHLODActor, const UHLODLayer* InHLODLayer, const TArray<UPrimitiveComponent*>& InSubComponents) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UHLODBuilderMeshMerge::CreateComponents);

	TArray<UObject*> Assets;
	FVector MergedActorLocation;

	const UHLODBuilderMeshMergeSettings* MeshMergeSettings = CastChecked<UHLODBuilderMeshMergeSettings>(InHLODLayer->GetHLODBuilderSettings());
	const FMeshMergingSettings& UseSettings = MeshMergeSettings->MeshMergeSettings;
	UMaterial* HLODMaterial = MeshMergeSettings->HLODMaterial.LoadSynchronous();

	const IMeshMergeUtilities& MeshMergeUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();
	MeshMergeUtilities.MergeComponentsToStaticMesh(InSubComponents, InHLODActor->GetWorld(), UseSettings, HLODMaterial, InHLODActor->GetPackage(), InHLODActor->GetActorLabel(), Assets, MergedActorLocation, 0.25f, false);

	UStaticMeshComponent* Component = nullptr;
	Algo::ForEach(Assets, [this, InHLODActor, &Component, &MergedActorLocation](UObject* Asset)
	{
		Asset->ClearFlags(RF_Public | RF_Standalone);

		if (Cast<UStaticMesh>(Asset))
		{
			Component = NewObject<UStaticMeshComponent>(InHLODActor);
			Component->SetStaticMesh(static_cast<UStaticMesh*>(Asset));
			Component->SetWorldLocation(MergedActorLocation);
		}
	});

	return TArray<UPrimitiveComponent*>({ Component });
}
