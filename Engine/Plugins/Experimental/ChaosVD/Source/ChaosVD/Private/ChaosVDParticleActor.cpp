// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleActor.h"

#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDScene.h"
#include "ChaosVDRecording.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

static FAutoConsoleVariable CVarChaosVDHideVolumeAndBrushesHack(
	TEXT("p.Chaos.VD.Tool.HideVolumeAndBrushesHack"),
	true,
	TEXT("If true, it will hide any geometry if its name contains Volume or Brush"));

AChaosVDParticleActor::AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));
}

void AChaosVDParticleActor::UpdateFromRecordedData(const FChaosVDParticleDebugData& InRecordedData, const Chaos::FRigidTransform3& SimulationTransform)
{
	SetActorLocationAndRotation(SimulationTransform.TransformPosition(InRecordedData.Position), SimulationTransform.GetRotation() * InRecordedData.Rotation, false);

	if (RecordedDebugData.ImplicitObjectHash != InRecordedData.ImplicitObjectHash)
	{
		if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
		{
			if (const TSharedPtr<const Chaos::FImplicitObject>* Geometry = ScenePtr->GetUpdatedGeometry(InRecordedData.ImplicitObjectHash))
			{
				UpdateGeometryData(*Geometry, EChaosVDActorGeometryUpdateFlags::ForceUpdate);
			}
		}
	}

	// TODO: We should store a ptr to the data and in our custom details panel draw it, but the current data format is just a test
	// So we should do this once it is final
	RecordedDebugData = InRecordedData;
}

void AChaosVDParticleActor::UpdateGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject, EChaosVDActorGeometryUpdateFlags OptionFlag)
{
	if (EnumHasAnyFlags(OptionFlag, EChaosVDActorGeometryUpdateFlags::ForceUpdate))
	{
		bIsGeometryDataGenerationStarted = false;

		for (TWeakObjectPtr<UMeshComponent>& MeshComponent : MeshComponents)
		{
			if (MeshComponent.IsValid())
			{
				MeshComponent->DestroyComponent();	
			}
		}

		MeshComponents.Reset();
	}

	if (bIsGeometryDataGenerationStarted)
	{
		return;
	}

	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		if (const TSharedPtr<FChaosVDGeometryBuilder>& GeometryGenerator = ScenePtr->GetGeometryGenerator())
		{
			TArray<TWeakObjectPtr<UMeshComponent>> OutGeneratedMeshComponents;
			Chaos::FRigidTransform3 Transform;

			// Heightfields need to be created as Static meshes and use normal Static Mesh components because we need LODs for them due to their high triangle count
			if (FChaosVDGeometryBuilder::DoesImplicitContainType(ImplicitObject.Get(), Chaos::ImplicitObjectType::HeightField))
			{
				constexpr int32 LODsToGenerateNum = 3;
				constexpr int32 StartingMeshComponentIndex = 0;
				GeometryGenerator->CreateMeshComponentsFromImplicit<UStaticMesh, UStaticMeshComponent>(ImplicitObject.Get(), this, OutGeneratedMeshComponents, Transform, StartingMeshComponentIndex, LODsToGenerateNum);
			}
			else
			{
				GeometryGenerator->CreateMeshComponentsFromImplicit<UStaticMesh, UInstancedStaticMeshComponent>(ImplicitObject.Get(), this, OutGeneratedMeshComponents, Transform);
			}

			if (OutGeneratedMeshComponents.Num() > 0)
			{
				MeshComponents.Append(OutGeneratedMeshComponents);

				if (CVarChaosVDHideVolumeAndBrushesHack->GetBool())
				{
					// This is a temp hack (and is not performant) we need until we have a proper way to filer out trigger volumes/brushes at will
					// Without this most maps will be covered in boxes
					if (RecordedDebugData.DebugName.Contains(TEXT("Brush")) || RecordedDebugData.DebugName.Contains(TEXT("Volume")))
					{
						for (TWeakObjectPtr<UMeshComponent> MeshComponent : OutGeneratedMeshComponents)
						{
							if (MeshComponent.IsValid())
							{
								MeshComponent->SetVisibility(false);
							}
						}
					}
				}

				bIsGeometryDataGenerationStarted = true;
			}
		}
	}
}

void AChaosVDParticleActor::SetScene(const TSharedPtr<FChaosVDScene>& InScene)
{
	OwningScene = InScene;

	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		GeometryUpdatedDelegate = ScenePtr->OnNewGeometryAvailable().AddWeakLambda(this, [this](const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject, const uint32 ID)
		{
			if (RecordedDebugData.ImplicitObjectHash == ID)
			{
				UpdateGeometryData(ImplicitObject);
			}
		});
	}
}

void AChaosVDParticleActor::BeginDestroy()
{
	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		ScenePtr->OnNewGeometryAvailable().Remove(GeometryUpdatedDelegate);
	}

	Super::BeginDestroy();
}

#if WITH_EDITOR
bool AChaosVDParticleActor::IsSelectedInEditor() const
{
	// The implementation of this method in UObject, used a global edit callback,
	// but as we don't use the global editor selection system, we need to re-route it.
	if (TSharedPtr<FChaosVDScene> ScenePtr = OwningScene.Pin())
	{
		return ScenePtr->IsObjectSelected(this);
	}

	return false;
}
#endif

