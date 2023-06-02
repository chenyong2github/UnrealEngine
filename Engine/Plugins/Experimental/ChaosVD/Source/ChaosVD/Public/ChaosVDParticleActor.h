// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDRecording.h"
#include "Chaos/Core.h"
#include "GameFramework/Actor.h"

#include "ChaosVDParticleActor.generated.h"

class FChaosVDScene;
struct FChaosVDParticleDebugData;
class UMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;

/** Options flags to control how geometry is updated in a ChaosVDActor */
enum class EChaosVDActorGeometryUpdateFlags : int32
{
	None = 0,
	ForceUpdate = 1 << 0
};
ENUM_CLASS_FLAGS(EChaosVDActorGeometryUpdateFlags)

/** Actor used to represent a Chaos Particle in the Visual Debugger's world */
UCLASS()
class AChaosVDParticleActor : public AActor
{
	GENERATED_BODY()
public:
	AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer);

	void UpdateFromRecordedData(const FChaosVDParticleDebugData& InRecordedData, const Chaos::FRigidTransform3& SimulationTransform);
	
	const FChaosVDParticleDebugData& GetDebugData() const { return RecordedDebugData; }

	void UpdateGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject, EChaosVDActorGeometryUpdateFlags OptionFlags = EChaosVDActorGeometryUpdateFlags::None);

	void SetScene(const TSharedPtr<FChaosVDScene>& InScene);

	virtual void BeginDestroy() override;
	
#if WITH_EDITOR
	virtual bool IsSelectedInEditor() const override;
#endif

protected:

	UPROPERTY(VisibleAnywhere, Category="Chaos Visual Debugger Data")
	FChaosVDParticleDebugData RecordedDebugData;

	bool bIsGeometryDataGenerationStarted = false;

	TWeakPtr<FChaosVDScene> OwningScene;

	TArray<TWeakObjectPtr<UMeshComponent>> MeshComponents;

	FDelegateHandle GeometryUpdatedDelegate;
};
