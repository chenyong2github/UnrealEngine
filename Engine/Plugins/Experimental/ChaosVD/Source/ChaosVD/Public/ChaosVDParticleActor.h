// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/Core.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "DataWrappers/ChaosVDCollisionDataWrappers.h"
#include "GameFramework/Actor.h"

#include "ChaosVDParticleActor.generated.h"

class FChaosVDScene;
struct FChaosVDParticleDebugData;
class UMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;

namespace Chaos
{
	class FImplicitObject;
}

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

	void UpdateFromRecordedParticleData(const FChaosVDParticleDataWrapper& InRecordedData, const Chaos::FRigidTransform3& SimulationTransform);

	void UpdateCollisionData(const TArray<TSharedPtr<FChaosVDParticlePairMidPhase>>& InRecordedMidPhases);
	void UpdateCollisionData(const TArray<FChaosVDConstraint>& InRecordedConstraints);

	void UpdateGeometry(const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject, EChaosVDActorGeometryUpdateFlags OptionsFlags = EChaosVDActorGeometryUpdateFlags::None);
	void UpdateGeometry(uint32 NewGeometryHash, EChaosVDActorGeometryUpdateFlags OptionsFlags = EChaosVDActorGeometryUpdateFlags::None);

	void SetScene(const TSharedPtr<FChaosVDScene>& InScene);

	virtual void BeginDestroy() override;

	const FChaosVDParticleDataWrapper& GetParticleData() { return ParticleDataViewer; }
	
#if WITH_EDITOR
	virtual bool IsSelectedInEditor() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:

	UPROPERTY(EditAnywhere, Category="Particle Data")
	FChaosVDParticleDataWrapper ParticleDataViewer;

	bool bIsGeometryDataGenerationStarted = false;

	TWeakPtr<FChaosVDScene> OwningScene;

	TArray<TWeakObjectPtr<UMeshComponent>> MeshComponents;

	FDelegateHandle GeometryUpdatedDelegate;
};
