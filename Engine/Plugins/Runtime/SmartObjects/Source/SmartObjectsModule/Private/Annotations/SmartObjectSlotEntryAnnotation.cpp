// Copyright Epic Games, Inc. All Rights Reserved.

#include "Annotations/SmartObjectSlotEntryAnnotation.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectVisualizationContext.h"
#include "SceneManagement.h" // FPrimitiveDrawInterface
#include "NavigationSystem.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategory.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmartObjectSlotEntryAnnotation)

namespace UE::SmartObject::Annotations
{
	inline bool IsPointInBox(const FVector Point, const FVector BoxCenter, const FVector BoxExtents)
	{
		const FVector AbsDiff = (Point - BoxCenter).GetAbs();
		return AbsDiff.X <= BoxExtents.X && AbsDiff.Y <= BoxExtents.Y && AbsDiff.Z <= BoxExtents.Z; 
	}

	const ANavigationData* GetDefaultNavData(const UWorld& World)
	{
		const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
		if (!NavSys)
		{
			return nullptr;
		}
		return NavSys->GetDefaultNavDataInstance();
	}

	FNavLocation FindNearestNavigableLocation(const UWorld& World, const FVector Location, const FVector SearchExtents)
	{
		// Find navigation data.
		const ANavigationData* NavData = GetDefaultNavData(World);
		if (!NavData)
		{
			return {};
		}

		FNavLocation NavLocation;
		if (NavData->ProjectPoint(Location, NavLocation, SearchExtents, nullptr, nullptr)
			&& NavLocation.HasNodeRef()
			&& IsPointInBox(NavLocation.Location, Location, SearchExtents))
		{
			return NavLocation;
		}

		return {};
	}

	static constexpr FColor EntryColor(0, 64, 192);
	static constexpr FColor InvalidEntryColor(192, 32, 16);

};

#if WITH_EDITOR

void FSmartObjectSlotEntryAnnotation::DrawVisualization(FSmartObjectVisualizationContext& VisContext) const
{
	constexpr FVector::FReal MarkerRadius = 20.0;
	constexpr FVector::FReal TickSize = 5.0;
	constexpr FVector::FReal MinArrowDrawDistance = 20.0;

	const FSmartObjectSlotIndex SlotIndex(VisContext.SlotIndex);
	const TOptional<FTransform> SlotTransform = VisContext.Definition.GetSlotTransform(VisContext.OwnerLocalToWorld, SlotIndex);

	const bool bHasNavData = UE::SmartObject::Annotations::GetDefaultNavData(VisContext.World) != nullptr;
	
	if (SlotTransform.IsSet())
	{
		const TOptional<FTransform> AnnotationTransform = GetWorldTransform(*SlotTransform);
		if (AnnotationTransform.IsSet())
		{
			const FVector SlotWorldLocation = SlotTransform->GetTranslation();
			const FVector EntryWorldLocation = AnnotationTransform->GetTranslation();
			const FVector AxisX = AnnotationTransform->GetUnitAxis(EAxis::X);
			const FVector AxisY = AnnotationTransform->GetUnitAxis(EAxis::Y);

			FLinearColor Color = UE::SmartObject::Annotations::EntryColor;
			
			if (bHasNavData)
			{
				const FNavLocation NavLocation = UE::SmartObject::Annotations::FindNearestNavigableLocation(VisContext.World, EntryWorldLocation, FVector(FSmartObjectSlotEntryRequest::DefaultValidationExtents));

				if (NavLocation.HasNodeRef())
				{
					FVector TickStart = NavLocation.Location;
					FVector TickEnd = NavLocation.Location;
					TickStart.Z = FMath::Min(NavLocation.Location.Z, EntryWorldLocation.Z) - TickSize;
					TickEnd.Z = FMath::Max(NavLocation.Location.Z, EntryWorldLocation.Z) + TickSize;
					VisContext.PDI->DrawTranslucentLine(TickStart, TickEnd, Color, SDPG_World, 1.0f);
				}
				else
				{
					Color = UE::SmartObject::Annotations::InvalidEntryColor;
				}
			}
			if (VisContext.bIsAnnotationSelected)
			{
				Color = VisContext.SelectedColor; 
			}

			if (bIsEntry)
			{
				const FVector V0 = EntryWorldLocation + AxisX * MarkerRadius;
				const FVector V1 = EntryWorldLocation + AxisX * MarkerRadius * 0.25 + AxisY * MarkerRadius;
				const FVector V2 = EntryWorldLocation + AxisX * MarkerRadius * 0.25 - AxisY * MarkerRadius;
				const FVector V3 = EntryWorldLocation + AxisY * MarkerRadius;
				const FVector V4 = EntryWorldLocation - AxisY * MarkerRadius;
				VisContext.PDI->DrawTranslucentLine(V0, V1, Color, SDPG_World, 2.0f);
				VisContext.PDI->DrawTranslucentLine(V0, V2, Color, SDPG_World, 2.0f);
				VisContext.PDI->DrawTranslucentLine(V1, V3, Color, SDPG_World, 2.0f);
				VisContext.PDI->DrawTranslucentLine(V2, V4, Color, SDPG_World, 2.0f);
			}

			if (bIsExit)
			{
				const FVector V1 = EntryWorldLocation - AxisX * MarkerRadius * 0.75 + AxisY * MarkerRadius;
				const FVector V2 = EntryWorldLocation - AxisX * MarkerRadius * 0.75 - AxisY * MarkerRadius;
				const FVector V3 = EntryWorldLocation + AxisY * MarkerRadius;
				const FVector V4 = EntryWorldLocation - AxisY * MarkerRadius;
				VisContext.PDI->DrawTranslucentLine(V1, V2, Color, SDPG_World, 2.0f);
				VisContext.PDI->DrawTranslucentLine(V1, V3, Color, SDPG_World, 2.0f);
				VisContext.PDI->DrawTranslucentLine(V2, V4, Color, SDPG_World, 2.0f);
			}

			// Tick at the center.
			VisContext.PDI->DrawTranslucentLine(EntryWorldLocation - AxisX * TickSize, EntryWorldLocation + AxisX * TickSize, Color, SDPG_World, 1.0f);
			VisContext.PDI->DrawTranslucentLine(EntryWorldLocation - AxisY * TickSize, EntryWorldLocation + AxisY * TickSize, Color, SDPG_World, 1.0f);

			// Arrow pointing at the the slot, if far enough from the slot.
			if (FVector::DistSquared(EntryWorldLocation, SlotWorldLocation) > FMath::Square(MinArrowDrawDistance))
			{
				VisContext.DrawArrow(EntryWorldLocation, SlotWorldLocation, Color, 15.0f, 15.0f, /*DepthPrioGroup*/0, /*Thickness*/1.0f, /*DepthBias*/2.0);
			}
		}
	}
}

void FSmartObjectSlotEntryAnnotation::DrawVisualizationHUD(FSmartObjectVisualizationContext& VisContext) const
{
	const FSmartObjectSlotIndex SlotIndex(VisContext.SlotIndex);
	const TOptional<FTransform> SlotTransform = VisContext.Definition.GetSlotTransform(VisContext.OwnerLocalToWorld, SlotIndex);
	
	if (SlotTransform.IsSet() && VisContext.bIsAnnotationSelected)
	{
		TOptional<FTransform> AnnotationTransform = GetWorldTransform(*SlotTransform);
		if (AnnotationTransform.IsSet())
		{
			const FVector EntryWorldLocation = AnnotationTransform->GetTranslation();
			FString Text(TEXT("Entry\n"));
			Text += Tag.ToString();
			VisContext.DrawString(EntryWorldLocation, *Text, FLinearColor::White);
		}
	}
}

void FSmartObjectSlotEntryAnnotation::AdjustWorldTransform(const FTransform& SlotTransform, const FVector& DeltaTranslation, const FRotator& DeltaRotation)
{
	if (!DeltaTranslation.IsZero())
	{
		const FVector LocalTranslation = SlotTransform.InverseTransformVector(DeltaTranslation);
		Offset += FVector3f(LocalTranslation);
	}

	if (!DeltaRotation.IsZero())
	{
		const FRotator3f LocalRotation = FRotator3f(SlotTransform.InverseTransformRotation(DeltaRotation.Quaternion()).Rotator());
		Rotation += LocalRotation;
		Rotation.Normalize();
	}
}

#endif // WITH_EDITOR


TOptional<FTransform> FSmartObjectSlotEntryAnnotation::GetWorldTransform(const FTransform& SlotTransform) const
{
	const FTransform LocalTransform = FTransform(FRotator(Rotation), FVector(Offset));
	return TOptional(LocalTransform * SlotTransform);
}

FVector FSmartObjectSlotEntryAnnotation::GetWorldLocation(const FTransform& SlotTransform) const
{
	return SlotTransform.TransformPosition(FVector(Offset));
}

FRotator FSmartObjectSlotEntryAnnotation::GetWorldRotation(const FTransform& SlotTransform) const
{
	return SlotTransform.TransformRotation(FQuat(Rotation.Quaternion())).Rotator();
}

#if WITH_GAMEPLAY_DEBUGGER
void FSmartObjectSlotEntryAnnotation::CollectDataForGameplayDebugger(FGameplayDebuggerCategory& Category, const FTransform& SlotTransform, const FVector ViewLocation, const FVector ViewDirection, AActor* DebugActor) const
{
	constexpr FVector::FReal MarkerRadius = 20.0;
	constexpr FVector::FReal TickSize = 5.0;
	constexpr FVector::FReal MinArrowDrawDistance = 20.0;

	const UWorld* World = Category.GetWorldFromReplicator();
	if (!World)
	{
		return;
	}
	
	const TOptional<FTransform> AnnotationTransform = GetWorldTransform(SlotTransform);
	if (!AnnotationTransform.IsSet())
	{
		return;
	}
	
	const FVector SlotWorldLocation = SlotTransform.GetTranslation();
	const FVector EntryWorldLocation = AnnotationTransform->GetTranslation();
	const FVector AxisX = AnnotationTransform->GetUnitAxis(EAxis::X);
	const FVector AxisY = AnnotationTransform->GetUnitAxis(EAxis::Y);
	const FVector EntryWorldLocationWithOffset = EntryWorldLocation + FVector(0, 0, 2.0);

	FColor Color = UE::SmartObject::Annotations::EntryColor;

	const FNavLocation NavLocation = UE::SmartObject::Annotations::FindNearestNavigableLocation(*World, EntryWorldLocation, FVector(FSmartObjectSlotEntryRequest::DefaultValidationExtents));

	if (NavLocation.HasNodeRef())
	{
		FVector TickStart = NavLocation.Location;
		FVector TickEnd = NavLocation.Location;
		TickStart.Z = FMath::Min(NavLocation.Location.Z, EntryWorldLocation.Z) - TickSize;
		TickEnd.Z = FMath::Max(NavLocation.Location.Z, EntryWorldLocation.Z) + TickSize;
		Category.AddShape(FGameplayDebuggerShape::MakeSegment(TickStart, TickEnd, 1.0f, Color));
	}
	else
	{
		Color = UE::SmartObject::Annotations::InvalidEntryColor;
	}

	TArray<FVector, TInlineAllocator<16>> Polyline;
	if (bIsEntry)
	{
		Category.AddShape(FGameplayDebuggerShape::MakePolyline({
				EntryWorldLocationWithOffset + AxisY * MarkerRadius,
				EntryWorldLocationWithOffset + AxisX * MarkerRadius * 0.25 + AxisY * MarkerRadius,
				EntryWorldLocationWithOffset + AxisX * MarkerRadius,
				EntryWorldLocationWithOffset + AxisX * MarkerRadius * 0.25 - AxisY * MarkerRadius,
				EntryWorldLocationWithOffset - AxisY * MarkerRadius
			}, 2.0f, Color));
	}

	if (bIsExit)
	{
		Category.AddShape(FGameplayDebuggerShape::MakePolyline({
				EntryWorldLocationWithOffset + AxisY * MarkerRadius,
				EntryWorldLocationWithOffset - AxisX * MarkerRadius * 0.5 + AxisY * MarkerRadius,
				EntryWorldLocationWithOffset - AxisX * MarkerRadius * 0.5 - AxisY * MarkerRadius,
				EntryWorldLocationWithOffset - AxisY * MarkerRadius
			}, 2.0f, Color));
	}

	// Tick at the center.
	Category.AddShape(FGameplayDebuggerShape::MakeSegmentList( {
			EntryWorldLocationWithOffset - AxisX * TickSize,
			EntryWorldLocationWithOffset + AxisX * TickSize,
			EntryWorldLocationWithOffset - AxisY * TickSize,
			EntryWorldLocationWithOffset + AxisY * TickSize
		}, 2.0f, Color));

	// Arrow pointing at the the slot, if far enough from the slot.
	if (FVector::DistSquared(EntryWorldLocation, SlotWorldLocation) > FMath::Square(MinArrowDrawDistance))
	{
		Category.AddShape(FGameplayDebuggerShape::MakeSegment(EntryWorldLocationWithOffset, SlotWorldLocation, 1.0f, Color));
	}
}
#endif // WITH_GAMEPLAY_DEBUGGER	
