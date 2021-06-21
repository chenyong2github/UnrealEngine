// Copyright Epic Games, Inc. All Rights Reserved.

#include "DynamicMeshActor.h"
#include "ToolSetupUtil.h"


#define LOCTEXT_NAMESPACE "ADynamicMeshActor"

ADynamicMeshActor::ADynamicMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DynamicMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComponent"));
	DynamicMeshComponent->SetMobility(EComponentMobility::Movable);
	DynamicMeshComponent->SetGenerateOverlapEvents(false);
	DynamicMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	DynamicMeshComponent->CollisionType = ECollisionTraceFlag::CTF_UseDefault;

	DynamicMeshComponent->SetMaterial(0, ToolSetupUtil::GetDefaultMaterial());		// is this necessary?

	SetRootComponent(DynamicMeshComponent);
}







#undef LOCTEXT_NAMESPACE