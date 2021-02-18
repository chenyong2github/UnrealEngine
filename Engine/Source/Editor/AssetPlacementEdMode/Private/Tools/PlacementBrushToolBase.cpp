// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementBrushToolBase.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "InstancedFoliageActor.h"
#include "FoliageHelper.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BrushComponent.h"
#include "Components/ModelComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "AssetPlacementSettings.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "AssetPlacementEdMode.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "Editor.h"
#include "Modes/PlacementModeSubsystem.h"

bool UPlacementToolBuilderBase::CanBuildTool(const FToolBuilderState& SceneState) const
{	
	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	return PlacementSettings.IsValid() && PlacementSettings->PaletteItems.Num();
}

UInteractiveTool* UPlacementToolBuilderBase::BuildTool(const FToolBuilderState& SceneState) const
{
	return FactoryToolInstance(SceneState.ToolManager);
}

bool UPlacementBrushToolBase::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	const FVector TraceStart(Ray.Origin);
	const FVector TraceEnd(Ray.Origin + Ray.Direction * HALF_WORLD_MAX);

	return FindHitResultWithStartAndEndTraceVectors(OutHit, TraceStart, TraceEnd);
}

bool UPlacementBrushToolBase::AreAllTargetsValid() const
{
	return Target ? Target->IsValid() : true;
}

double UPlacementBrushToolBase::EstimateMaximumTargetDimension()
{
	return 1000.0;
}

bool UPlacementBrushToolBase::FindHitResultWithStartAndEndTraceVectors(FHitResult& OutHit, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius)
{
	UWorld* EditingWorld = GetToolManager()->GetWorld();
	constexpr TCHAR NAME_PlacementBrushTool[] = TEXT("PlacementBrushTool");

	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();

	auto FilterFunc = [PlacementSettings](const UPrimitiveComponent* InComponent) {
		if (InComponent && PlacementSettings.IsValid())
		{
			bool bFoliageOwned = InComponent->GetOwner() && FFoliageHelper::IsOwnedByFoliage(InComponent->GetOwner());
			const bool bAllowLandscape = PlacementSettings->bLandscape;
			const bool bAllowStaticMesh = PlacementSettings->bStaticMeshes;
			const bool bAllowBSP = PlacementSettings->bBSP;
			const bool bAllowFoliage = PlacementSettings->bFoliage;
			const bool bAllowTranslucent = PlacementSettings->bTranslucent;

			// Whitelist
			bool bAllowed =
				(bAllowLandscape && InComponent->IsA(ULandscapeHeightfieldCollisionComponent::StaticClass())) ||
				(bAllowStaticMesh && InComponent->IsA(UStaticMeshComponent::StaticClass()) && !InComponent->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) && !bFoliageOwned) ||
				(bAllowBSP && (InComponent->IsA(UBrushComponent::StaticClass()) || InComponent->IsA(UModelComponent::StaticClass()))) ||
				(bAllowFoliage && (InComponent->IsA(UFoliageInstancedStaticMeshComponent::StaticClass()) || bFoliageOwned));

			// Blacklist
			bAllowed &=
				(bAllowTranslucent || !(InComponent->GetMaterial(0) && IsTranslucentBlendMode(InComponent->GetMaterial(0)->GetBlendMode())));

			return bAllowed;
		}

		return false; };

	return AInstancedFoliageActor::FoliageTrace(EditingWorld, OutHit, FDesiredFoliageInstance(TraceStart, TraceEnd, /* FoliageType= */ nullptr, TraceRadius), NAME_PlacementBrushTool, /* bReturnFaceIndex */ false, FilterFunc);
}

FTransform UPlacementBrushToolBase::GetFinalTransformFromHitLocationAndNormal(const FVector& InLocation, const FVector& InNormal)
{
	FTransform FinalizedTransform(InLocation);

	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (!PlacementSettings.IsValid())
	{
		return FinalizedTransform;
	}

	// Update the rotation if we need to
	FRotator FinalRotation = GetFinalRotation(FinalizedTransform);
	FinalizedTransform.SetRotation(FinalRotation.Quaternion());

	// Align to normal
	if (PlacementSettings->bAlignToNormal)
	{
		FRotator AlignRotation = InNormal.Rotation();
		// Static meshes are authored along the vertical axis rather than the X axis, so we add 90 degrees to the static mesh's Pitch.
		AlignRotation.Pitch -= 90.f;
		// Clamp its value inside +/- one rotation
		AlignRotation.Pitch = FRotator::NormalizeAxis(AlignRotation.Pitch);

		// limit the maximum pitch angle if it's > 0.
		// For now, just set the align max angle to a constant value, until it can be pulled from per object settings
		constexpr float AlignMaxAngle = 0.0f;
		if (AlignMaxAngle > 0.f)
		{
			int32 MaxPitch = AlignMaxAngle;
			if (AlignRotation.Pitch > MaxPitch)
			{
				AlignRotation.Pitch = MaxPitch;
			}
			else if (AlignRotation.Pitch < -MaxPitch)
			{
				AlignRotation.Pitch = -MaxPitch;
			}
		}

		FinalizedTransform.SetRotation(FQuat(AlignRotation) * FinalizedTransform.GetRotation());
	}

	if (PlacementSettings->bUseRandomScale)
	{
		// Until we have per object settings, just use a uniform scale, clamped from half to double size
		FFloatInterval ScaleRange(0.5f, 2.0f);
		FVector NewScale(ScaleRange.Interpolate(FMath::FRand()));
		FinalizedTransform.SetScale3D(NewScale);
	}

	return FinalizedTransform;
}

FRotator UPlacementBrushToolBase::GetFinalRotation(const FTransform& InTransform)
{
	FRotator UpdatedRotation = InTransform.Rotator();

	TWeakObjectPtr<const UAssetPlacementSettings> PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	if (!PlacementSettings.IsValid())
	{
		return UpdatedRotation;
	}

	if (PlacementSettings->bUseRandomXRotation)
	{
		UpdatedRotation.Roll = PlacementSettings->RandomRotationX.Interpolate(FMath::FRand());
	}

	if (PlacementSettings->bUseRandomYRotation)
	{
		UpdatedRotation.Pitch = PlacementSettings->RandomRotationY.Interpolate(FMath::FRand());
	}

	if (PlacementSettings->bUseRandomZRotation)
	{
		UpdatedRotation.Yaw = PlacementSettings->RandomRotationZ.Interpolate(FMath::FRand());
	}

	return UpdatedRotation;
}

TArray<FTypedElementHandle> UPlacementBrushToolBase::GetElementsInBrushRadius() const
{
	TArray<FTypedElementHandle> ElementHandles;
	FCollisionQueryParams QueryParams(TEXT("PlacementBrushTool"), SCENE_QUERY_STAT_ONLY(IFA_FoliageTrace), true);
	QueryParams.bReturnFaceIndex = false;
	TArray<FHitResult> Hits;
	FCollisionShape BrushSphere;
	BrushSphere.SetSphere(LastBrushStamp.Radius);

	const FVector TraceStart(LastWorldRay.Origin);
	const FVector TraceEnd(LastWorldRay.Origin + LastWorldRay.Direction * HALF_WORLD_MAX);

	GetToolManager()->GetWorld()->SweepMultiByObjectType(Hits, TraceStart, TraceEnd, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects), BrushSphere, QueryParams);

	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* HitComponent = Hit.GetComponent();
		check(HitComponent);

		// In the editor traces can hit "No Collision" type actors, so ugh. (ignore these)
		if (!HitComponent->IsQueryCollisionEnabled() || HitComponent->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block)
		{
			continue;
		}

		// Don't place assets on invisible walls / triggers / volumes
		if (HitComponent->IsA<UBrushComponent>())
		{
			continue;
		}

		const AActor* HitActor = Hit.GetActor();
		if (HitActor)
		{
			FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(HitActor);
			if (GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(ActorHandle))
			{
				ElementHandles.Emplace(ActorHandle);
			}
		}
	}

	// Handle the IFA for the brush stroke level
	if (UActorPartitionSubsystem* PartitionSubsystem = UWorld::GetSubsystem<UActorPartitionSubsystem>(GEditor->GetEditorWorldContext().World()))
	{
		constexpr bool bCreatePartitionActorIfMissing = false;
		FActorPartitionGetParams PartitionActorFindParams(AInstancedFoliageActor::StaticClass(), bCreatePartitionActorIfMissing, GEditor->GetEditorWorldContext().World()->GetCurrentLevel(), LastBrushStamp.WorldPosition);
		if (AInstancedFoliageActor* FoliageActor = Cast<AInstancedFoliageActor>(PartitionSubsystem->GetActor(PartitionActorFindParams)))
		{
			for (const auto& FoliageInfo : FoliageActor->GetFoliageInfos())
			{
				FTypedElementHandle SourceObjectHandle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(FoliageInfo.Key->GetSource());
				if (GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->DoesCurrentPaletteSupportElement(SourceObjectHandle))
				{
					TArray<int32> Instances;
					FSphere SphereToCheck(LastBrushStamp.WorldPosition, LastBrushStamp.Radius);
					FoliageInfo.Value->GetInstancesInsideSphere(SphereToCheck, Instances);
					if (Instances.Num())
					{
						// For now, return the whole foliage actor, and allow the calling code to drill down, since we do not have element handles at the instance level just yet
						ElementHandles.Emplace(UEngineElementsLibrary::AcquireEditorActorElementHandle(FoliageActor));
						break;
					}
				}
			}
		}
	}

	return ElementHandles;
}
