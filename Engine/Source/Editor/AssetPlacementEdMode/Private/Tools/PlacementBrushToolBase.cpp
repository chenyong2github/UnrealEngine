// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/PlacementBrushToolBase.h"
#include "UObject/Object.h"
#include "ToolContextInterfaces.h"
#include "InteractiveToolManager.h"
#include "InstancedFoliageActor.h"
#include "FoliageHelper.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BrushComponent.h"
#include "Components/ModelComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "AssetPlacementSettings.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Elements/Interfaces/TypedElementSelectionInterface.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "Editor.h"
#include "Modes/PlacementModeSubsystem.h"
#include "ActorFactories/ActorFactory.h"
#include "BaseGizmos/GizmoRenderingUtil.h"

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

void UPlacementBrushToolBase::Render(IToolsContextRenderAPI* RenderAPI)
{
	Super::Render(RenderAPI);

	// Transform the brush radius to standard pixel size
	LastBrushStampWorldToPixelScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(RenderAPI->GetSceneView(), LastBrushStamp.WorldPosition);
}

void UPlacementBrushToolBase::OnClickPress(const FInputDeviceRay& PressPos)
{
	LastDeviceInputRay = PressPos;
	Super::OnClickPress(PressPos);
}

void UPlacementBrushToolBase::OnClickDrag(const FInputDeviceRay& DragPos)
{
	LastDeviceInputRay = DragPos;
	GetToolManager()->PostInvalidation();
	Super::OnClickDrag(DragPos);
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

FTransform UPlacementBrushToolBase::GenerateTransformFromHitLocationAndNormal(const FVector& InLocation, const FVector& InNormal)
{
	const UAssetPlacementSettings* PlacementSettings = GEditor->GetEditorSubsystem<UPlacementModeSubsystem>()->GetModeSettingsObject();
	FTransform FinalizedTransform(GenerateRandomRotation(PlacementSettings), InLocation, GenerateRandomScale(PlacementSettings));
	return FinalizeTransform(FinalizedTransform, InNormal, PlacementSettings);
}

FQuat UPlacementBrushToolBase::GenerateRandomRotation(const UAssetPlacementSettings* PlacementSettings)
{
	if (!PlacementSettings)
	{
		return FQuat::Identity;
	}

	FRotator GeneratedRotation = FRotator::ZeroRotator;
	auto GetRandomSignedValueInRange = [](const FFloatInterval& Range, bool bAllowSigned) -> float
	{
		float Sign = (FMath::RandBool() && bAllowSigned) ? -1.0f : 1.0f;
		return Range.Interpolate(FMath::FRand()) * Sign;
	};

	if (PlacementSettings->bUseRandomRotationX)
	{
		GeneratedRotation.Roll = GetRandomSignedValueInRange(PlacementSettings->RandomRotationX, PlacementSettings->bAllowNegativeRotationX);
	}

	if (PlacementSettings->bUseRandomRotationY)
	{
		GeneratedRotation.Pitch = GetRandomSignedValueInRange(PlacementSettings->RandomRotationY, PlacementSettings->bAllowNegativeRotationY);
	}

	if (PlacementSettings->bUseRandomRotationZ)
	{
		GeneratedRotation.Yaw = GetRandomSignedValueInRange(PlacementSettings->RandomRotationZ, PlacementSettings->bAllowNegativeRotationZ);
	}

	return GeneratedRotation.Quaternion();
}

FVector UPlacementBrushToolBase::GenerateRandomScale(const UAssetPlacementSettings* PlacementSettings)
{
	FVector GeneratedScale(1.0f);
	if (!PlacementSettings || !PlacementSettings->bUseRandomScale)
	{
		return GeneratedScale;
	}

	auto GenerateRandomScaleComponent = [PlacementSettings]() -> float
	{
		float Sign = (FMath::RandBool() && PlacementSettings->bAllowNegativeScale) ? -1.0f : 1.0f;
		return PlacementSettings->ScaleRange.Interpolate(FMath::FRand()) * Sign;
	};

	switch (PlacementSettings->ScalingType)
	{
		case EFoliageScaling::Free:
		{
			GeneratedScale = FVector(GenerateRandomScaleComponent(), GenerateRandomScaleComponent(), GenerateRandomScaleComponent());
			break;
		}
		case EFoliageScaling::Uniform:
		{
			float ScaleComponent = GenerateRandomScaleComponent();
			GeneratedScale = FVector(ScaleComponent, ScaleComponent, ScaleComponent);
			break;
		}
		case EFoliageScaling::LockXY:
		{
			GeneratedScale.Z = GenerateRandomScaleComponent();
			break;
		}
		case EFoliageScaling::LockYZ:
		{
			GeneratedScale.X = GenerateRandomScaleComponent();
			break;
		}
		case EFoliageScaling::LockXZ:
		{
			GeneratedScale.Y = GenerateRandomScaleComponent();
			break;
		}
	}

	return GeneratedScale;
}

FQuat UPlacementBrushToolBase::AlignRotationWithNormal(const FQuat& InRotation, const FVector& InNormal, EAxis::Type InAlignmentAxis, bool bInvertAxis)
{
	FVector AlignmentVector = FVector::UpVector;
	switch (InAlignmentAxis)
	{
		case EAxis::Type::X:
		{
			AlignmentVector = bInvertAxis ? FVector::BackwardVector : FVector::ForwardVector;
		}
		break;

		case EAxis::Type::Y:
		{
			AlignmentVector = bInvertAxis ? FVector::LeftVector : FVector::RightVector;
		}
		break;

		case EAxis::Type::Z:
		{
			AlignmentVector = bInvertAxis ? FVector::DownVector : FVector::UpVector;
		}
		break;
	}

	return FindActorAlignmentRotation(InRotation, AlignmentVector, InNormal);
}

FTransform UPlacementBrushToolBase::FinalizeTransform(const FTransform& OriginalTransform, const FVector& InNormal, const UAssetPlacementSettings* PlacementSettings)
{
	if (!PlacementSettings)
	{
		return OriginalTransform;
	}

	FTransform FinalizedTransform(FQuat::Identity, OriginalTransform.GetTranslation(), OriginalTransform.GetScale3D());

	// Add the world offset.
	FVector WorldOffset = PlacementSettings->WorldLocationOffset;
	if (PlacementSettings->bScaleWorldLocationOffset)
	{
		WorldOffset *= OriginalTransform.GetScale3D();
	}
	FinalizedTransform.AddToTranslation(WorldOffset);

	// Align to normal.
	FQuat AdjustedRotation(OriginalTransform.GetRotation());
	if (PlacementSettings->bAlignToNormal)
	{
		AdjustedRotation = AlignRotationWithNormal(AdjustedRotation, InNormal, PlacementSettings->AxisToAlignWithNormal, PlacementSettings->bInvertNormalAxis);
	}
	AdjustedRotation.Normalize();
	FinalizedTransform.SetRotation(AdjustedRotation);

	// Add the relative offset.
	{
		FVector RelativeOffset = PlacementSettings->RelativeLocationOffset;
		if (PlacementSettings->bScaleRelativeLocationOffset)
		{
			RelativeOffset *= OriginalTransform.GetScale3D();
		}
		FinalizedTransform.SetTranslation(FinalizedTransform.TransformPosition(RelativeOffset));
	}

	return FinalizedTransform;
}

FTypedElementListRef UPlacementBrushToolBase::GetElementsInBrushRadius(const FInputDeviceRay& DragPos) const
{
	FTypedElementListRef ElementHandles = UTypedElementRegistry::GetInstance()->CreateElementList();

	// We need the 2D device screen space position to test against hit proxies.
	if (!DragPos.bHas2D)
	{
		return ElementHandles;
	}

	FViewport* Viewport = GetToolManager()->GetContextQueriesAPI()->GetFocusedViewport();
	if (!Viewport)
	{
		return ElementHandles;
	}

	FToolBuilderState SelectionState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
	if (!SelectionState.TypedElementSelectionSet.IsValid())
	{
		return ElementHandles;
	}
	UTypedElementSelectionSet* SelectionSet = SelectionState.TypedElementSelectionSet.Get();

	// Convert brush radius to screen space and gather hit elements within the brush radius.
	int32 HalfBrushRadius = FMath::CeilToFloat((LastBrushStamp.Radius * LastBrushStampWorldToPixelScale) / 2.0f);
	FIntRect AreaToCheck;
	FIntPoint ViewportSize = Viewport->GetSizeXY();
	AreaToCheck.Min.X = FMath::Max<int32>(0, DragPos.ScreenPosition.X - HalfBrushRadius);
	AreaToCheck.Min.Y = FMath::Max<int32>(0, DragPos.ScreenPosition.Y - HalfBrushRadius);
	AreaToCheck.Max.X = FMath::Min<int32>(ViewportSize.X, DragPos.ScreenPosition.X + HalfBrushRadius);
	AreaToCheck.Max.Y = FMath::Min<int32>(ViewportSize.Y, DragPos.ScreenPosition.Y + HalfBrushRadius);

	// Get the raw hit proxies within the rect
	FTypedElementListRef HitElementHandles = UTypedElementRegistry::GetInstance()->CreateElementList();
	Viewport->GetElementHandlesInRect(AreaToCheck, HitElementHandles);

	// Work out which elements to actually select in the viewport, and also verify that we're actually intersecting with the sphere from the brush in world space.
	FBoxSphereBounds WorldBrushSphereBounds(FSphere(LastBrushStamp.WorldPosition, LastBrushStamp.Radius));
	HitElementHandles->ForEachElementHandle([SelectionSet, ElementHandles, &WorldBrushSphereBounds](const FTypedElementHandle& HitHandle)
	{
		FTypedElementHandle ResolvedHandle = SelectionSet->GetSelectionElement(HitHandle, ETypedElementSelectionMethod::Primary);
		if (TTypedElement<UTypedElementWorldInterface> WorldInterface = SelectionSet->GetElementList()->GetElement<UTypedElementWorldInterface>(ResolvedHandle))
		{
			FBoxSphereBounds ElementBounds;
			WorldInterface.GetBounds(ElementBounds);
			if (ElementBounds.SpheresIntersect(ElementBounds, WorldBrushSphereBounds))
			{
				ElementHandles->Add(MoveTemp(ResolvedHandle));
			}
		}
		return true;
	});

	return ElementHandles;
}
