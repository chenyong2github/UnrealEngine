// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyCustomActor.h"

#include "WaterBodyCustomComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

AWaterBodyCustom::AWaterBodyCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyType = EWaterBodyType::Transition;

#if WITH_EDITOR
	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyCustomSprite"));
#endif

#if WITH_EDITORONLY_DATA
	bAffectsLandscape_DEPRECATED = false;
#endif
}

void AWaterBodyCustom::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::WaterBodyComponentRefactor)
	{
		UWaterBodyCustomComponent* CustomComponent = CastChecked<UWaterBodyCustomComponent>(WaterBodyComponent);
		if (CustomGenerator_DEPRECATED)
		{
			CustomComponent->MeshComp =	CustomGenerator_DEPRECATED->MeshComp;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
bool AWaterBodyCustom::IsIconVisible() const
{
	return (WaterBodyComponent->GetWaterMeshOverride() == nullptr);
}
#endif // WITH_EDITOR

// ----------------------------------------------------------------------------------

UDEPRECATED_CustomMeshGenerator::UDEPRECATED_CustomMeshGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}