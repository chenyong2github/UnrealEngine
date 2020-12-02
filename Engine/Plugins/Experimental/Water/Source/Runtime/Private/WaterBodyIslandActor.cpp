// Copyright Epic Games, Inc. All Rights Reserved.


#include "WaterBodyIslandActor.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "WaterSplineComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "WaterBodyActor.h"
#include "WaterRuntimeSettings.h"
#include "EngineUtils.h"

// ----------------------------------------------------------------------------------

#if WITH_EDITOR
#include "Components/BillboardComponent.h"
#include "Modules/ModuleManager.h"
#include "WaterIconHelper.h"
#include "WaterSubsystem.h"
#include "WaterModule.h"
#endif // WITH_EDITOR

// ----------------------------------------------------------------------------------

AWaterBodyIsland::AWaterBodyIsland(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SplineComp = CreateDefaultSubobject<UWaterSplineComponent>(TEXT("WaterSpline"));
	SplineComp->SetMobility(EComponentMobility::Static);
	SplineComp->SetClosedLoop(true);
	
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		SplineComp->OnSplineDataChanged().AddUObject(this, &AWaterBodyIsland::OnSplineDataChanged);
	}

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyIslandSprite"), NSLOCTEXT("Water", "WaterBodyIslandSpriteName", "Water Body Island"));
#endif

	RootComponent = SplineComp;
}

#if WITH_EDITOR
ETextureRenderTargetFormat AWaterBodyIsland::GetBrushRenderTargetFormat() const
{
	return ETextureRenderTargetFormat::RTF_RG16f;
}

void AWaterBodyIsland::GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const 
{
	for (const TPair<FName, FWaterBodyWeightmapSettings>& Pair : WaterWeightmapSettings)
	{
		if (Pair.Value.ModulationTexture)
		{
			OutDependencies.Add(Pair.Value.ModulationTexture);
		}
	}

	if (WaterHeightmapSettings.Effects.Displacement.Texture)
	{
		OutDependencies.Add(WaterHeightmapSettings.Effects.Displacement.Texture);
	}
}
#endif //WITH_EDITOR

void AWaterBodyIsland::UpdateHeight()
{
	const int32 NumSplinePoints = SplineComp->GetNumberOfSplinePoints();

	const float ActorZ = GetActorLocation().Z;

	for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
	{
		FVector WorldLoc = SplineComp->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);

		WorldLoc.Z = ActorZ;
		SplineComp->SetLocationAtSplinePoint(PointIndex, WorldLoc, ESplineCoordinateSpace::World);
	}
}

void AWaterBodyIsland::Destroyed()
{
	Super::Destroyed();

	// No need for water bodies to keep a pointer to ourselves, even if a lazy one :
	for (AWaterBody* WaterBody : TActorRange<AWaterBody>(GetWorld()))
	{
		WaterBody->RemoveIsland(this);
	}
}

#if WITH_EDITOR
void AWaterBodyIsland::UpdateOverlappingWaterBodies()
{
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	// Expand shape in Z to ensure we get overlaps for islands slighty above or below water level
	OverlapShape.SetBox(SplineComp->Bounds.BoxExtent+FVector(0,0,10000));
	GetWorld()->OverlapMultiByObjectType(Overlaps, SplineComp->Bounds.Origin, FQuat::Identity, FCollisionObjectQueryParams::AllObjects, OverlapShape);

	// Find any new overlapping bodies and notify them that this island influences them
	TSet<AWaterBody*> ExistingOverlappingBodies;
	TSet<TWeakObjectPtr<AWaterBody>> NewOverlappingBodies;

	TLazyObjectPtr<AWaterBodyIsland> LazyThis(this);

	// Fixup overlapping bodies 
	for (AWaterBody* WaterBody : TActorRange<AWaterBody>(GetWorld()))
	{
		if (WaterBody->ContainsIsland(LazyThis))
		{
			ExistingOverlappingBodies.Add(WaterBody);
		}
	}

	for (const FOverlapResult& Result : Overlaps)
	{
		AWaterBody* WaterBody = Cast<AWaterBody>(Result.Actor);
		if (WaterBody)
		{
			NewOverlappingBodies.Add(WaterBody);
			// If the water body is not already overlapping then notify
			if (!ExistingOverlappingBodies.Contains(WaterBody))
			{
				WaterBody->AddIsland(this);
			}
		}
	}

	// Find existing bodies that are no longer overlapping and remove them
	for (AWaterBody* ExistingBody : ExistingOverlappingBodies)
	{
		if (ExistingBody && !NewOverlappingBodies.Contains(ExistingBody))
		{
			ExistingBody->RemoveIsland(this);
		}
	}
}

void AWaterBodyIsland::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	UpdateAll();
}

void AWaterBodyIsland::PostEditUndo()
{
	Super::PostEditUndo();

	UpdateAll();
}

void AWaterBodyIsland::PostEditImport()
{
	Super::PostEditImport();

	UpdateAll();
}

void AWaterBodyIsland::UpdateAll()
{
	UpdateHeight();

	UpdateOverlappingWaterBodies();

	OnWaterBodyIslandChanged(/*bShapeOrPositionChanged*/true, /*bWeightmapSettingsChanged*/true);

	UpdateActorIcon();
}

void AWaterBodyIsland::UpdateActorIcon()
{
	if (ActorIcon && SplineComp && !bIsEditorPreviewActor)
	{
		UTexture2D* IconTexture = ActorIcon->Sprite;
		IWaterModuleInterface& WaterModule = FModuleManager::GetModuleChecked<IWaterModuleInterface>("Water");
		if (const IWaterEditorServices* WaterEditorServices = WaterModule.GetWaterEditorServices())
		{
			IconTexture = WaterEditorServices->GetWaterActorSprite(GetClass());
		}
		FWaterIconHelper::UpdateSpriteComponent(this, IconTexture);

		// Move the actor icon to the center of the island
		FVector ZOffset(0.0f, 0.0f, GetDefault<UWaterRuntimeSettings>()->WaterBodyIconWorldZOffset);
		ActorIcon->SetWorldLocation(SplineComp->Bounds.Origin + ZOffset);
	}
}

void AWaterBodyIsland::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	bool bWeightmapSettingsChanged = false;
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBody, LayerWeightmapSettings))
	{
		bWeightmapSettingsChanged = true;
	}

	OnWaterBodyIslandChanged(/*bShapeOrPositionChanged*/false, bWeightmapSettingsChanged);

	UpdateActorIcon();
}

void AWaterBodyIsland::OnSplineDataChanged()
{
	UpdateOverlappingWaterBodies();

	OnWaterBodyIslandChanged(/* bShapeOrPositionChanged = */true, /* bWeightmapSettingsChanged = */false);
}

void AWaterBodyIsland::OnWaterBodyIslandChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged)
{
#if WITH_EDITOR
	FWaterBrushActorChangedEventParams Params(this);
	Params.bShapeOrPositionChanged = bShapeOrPositionChanged;
	Params.bWeightmapSettingsChanged = bWeightmapSettingsChanged;
	BroadcastWaterBrushActorChangedEvent(Params);
#endif
}

#endif