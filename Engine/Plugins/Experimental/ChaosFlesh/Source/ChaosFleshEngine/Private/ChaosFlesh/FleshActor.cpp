// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/FleshActor.h"

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "Chaos/TriangleMesh.h"

#include "Chaos/PBDBendingConstraints.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDVolumeConstraint.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDTriangleMeshCollisions.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "Chaos/XPBDVolumeConstraints.h"
#include "Chaos/XPBDCorotatedFiberConstraints.h"
#include "Chaos/PBDTetConstraints.h"
#include "Chaos/PBDAltitudeSpringConstraints.h"
#include "Chaos/Plane.h"
#include "Chaos/Utilities.h"
#include "Chaos/PBDEvolution.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
//#include "ChaosFlesh/PB.h"

#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include UE_INLINE_GENERATED_CPP_BY_NAME(FleshActor)

DEFINE_LOG_CATEGORY_STATIC(AFleshLogging, Log, All);

AFleshActor::AFleshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//UE_LOG(AFleshLogging, Verbose, TEXT("AFleshActor::AFleshActor()"));
	FleshComponent = CreateDefaultSubobject<UFleshComponent>(TEXT("FleshComponent0"));
	RootComponent = FleshComponent;
	PrimaryActorTick.bCanEverTick = true;
}

#if WITH_EDITOR
bool AFleshActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	Super::GetReferencedContentObjects(Objects);

	if (FleshComponent && FleshComponent->GetRestCollection())
	{
		Objects.Add(const_cast<UFleshAsset*>(FleshComponent->GetRestCollection()));
	}
	return true;
}
#endif

void AFleshActor::EnableSimulation(ADeformableSolverActor* InActor)
{
	if (InActor)
	{
		if (FleshComponent && FleshComponent->GetRestCollection())
		{
			FleshComponent->EnableSimulation(InActor);
		}
	}
}

