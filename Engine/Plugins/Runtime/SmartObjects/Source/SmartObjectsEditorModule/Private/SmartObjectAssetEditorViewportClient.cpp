// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectAssetEditorViewportClient.h"
#include "ComponentVisualizer.h"
#include "SmartObjectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UnrealEdGlobals.h"
#include "SmartObjectAssetToolkit.h"
#include "SmartObjectAssetEditorSettings.h"
#include "SmartObjectComponentVisualizer.h"
#include "Editor/UnrealEdEngine.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SmartObjectAssetToolkit"

FSmartObjectAssetEditorViewportClient::FSmartObjectAssetEditorViewportClient(const TSharedRef<const FSmartObjectAssetToolkit>& InAssetEditorToolkit, FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(&InAssetEditorToolkit->GetEditorModeManager(), InPreviewScene, InEditorViewportWidget)
	, AssetEditorToolkit(InAssetEditorToolkit)
{
	EngineShowFlags.DisableAdvancedFeatures();
	bUsingOrbitCamera = true;

	// Set if the grid will be drawn
	DrawHelper.bDrawGrid = GetDefault<USmartObjectAssetEditorSettings>()->bShowGridByDefault;
}

FSmartObjectAssetEditorViewportClient::~FSmartObjectAssetEditorViewportClient()
{
	if (ScopedTransaction != nullptr)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

void FSmartObjectAssetEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (View != nullptr && PDI != nullptr)
	{
		// Draw slots and annotations.
		if (const USmartObjectDefinition* Definition = SmartObjectDefinition.Get())
		{
			UE::SmartObjects::Editor::Draw(*Definition, Selection, FTransform::Identity, *View, *PDI);
		}

		// Draw the object origin.
		DrawCoordinateSystem(PDI, FVector::ZeroVector, FRotator::ZeroRotator, 20.f, SDPG_World, 1.f);
	}
}


void FSmartObjectAssetEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	// Draw slots and annotations.
	if (const USmartObjectDefinition* Definition = SmartObjectDefinition.Get())
	{
		UE::SmartObjects::Editor::DrawCanvas(*Definition, Selection, FTransform::Identity, View, Canvas);
	}
}

void FSmartObjectAssetEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

	const FViewportClick Click(&View, this, Key, Event, HitX, HitY);
	bool bClickHandled = false;
	
	if (HitProxy && HitProxy->IsA(HSmartObjectSlotProxy::StaticGetType()))
	{
		const HSmartObjectSlotProxy* SlotProxy = static_cast<HSmartObjectSlotProxy*>(HitProxy);

		if (IsCtrlPressed())
		{
			// Toggle selection
			if (Selection.Contains(SlotProxy->SlotID))
			{
				Selection.Remove(SlotProxy->SlotID);
			}
			else
			{
				Selection.AddUnique(SlotProxy->SlotID);
			}
		}
		else
		{
			// Set selection
			Selection.Reset();
			Selection.Add(SlotProxy->SlotID);
		}

		bClickHandled = true;
	}

	if (!bClickHandled)
	{
		Selection.Reset();
	}
}

FVector FSmartObjectAssetEditorViewportClient::GetWidgetLocation() const
{
	const USmartObjectDefinition* Definition = SmartObjectDefinition.Get();
	if (Definition == nullptr)
	{
		return FVector::ZeroVector;
	}

	int32 NumSlots = 0;
	FVector AccumulatedSlotLocation = FVector::ZeroVector;
	
	const FTransform OwnerLocalToWorld = FTransform::Identity;
	for (int32 Index = 0; Index < Definition->GetSlots().Num(); ++Index)
	{
		const FSmartObjectSlotDefinition& Slot = Definition->GetSlots()[Index];

		if (!Selection.Contains(Slot.ID))
		{
			continue;
		}
		
		TOptional<FTransform> Transform = Definition->GetSlotTransform(OwnerLocalToWorld, FSmartObjectSlotIndex(Index));
		if (!Transform.IsSet())
		{
			continue;
		}

		AccumulatedSlotLocation += Transform.GetValue().GetLocation();
		NumSlots++;
	}

	if (NumSlots == 0)
	{
		return FVector::ZeroVector;
	}
	
	return AccumulatedSlotLocation / NumSlots;
}

FMatrix FSmartObjectAssetEditorViewportClient::GetWidgetCoordSystem() const
{
	return FMatrix::Identity;
}

ECoordSystem FSmartObjectAssetEditorViewportClient::GetWidgetCoordSystemSpace() const
{
	return WidgetCoordSystemSpace;
}

UE::Widget::EWidgetMode FSmartObjectAssetEditorViewportClient::GetWidgetMode() const
{
	bool bIsWidgetValid = false;

	const USmartObjectDefinition* Definition = SmartObjectDefinition.Get();
	if (Definition != nullptr)
	{
		const FTransform OwnerLocalToWorld = FTransform::Identity;
		for (int32 Index = 0; Index < Definition->GetSlots().Num(); ++Index)
		{
			const FSmartObjectSlotDefinition& Slot = Definition->GetSlots()[Index];

			if (!Selection.Contains(Slot.ID))
			{
				continue;
			}
			
			TOptional<FTransform> Transform = Definition->GetSlotTransform(OwnerLocalToWorld, FSmartObjectSlotIndex(Index));
			if (!Transform.IsSet())
			{
				continue;
			}

			bIsWidgetValid = true;
			break;
		}
	}
	
	return bIsWidgetValid ? WidgetMode : UE::Widget::EWidgetMode::WM_None;
}

bool FSmartObjectAssetEditorViewportClient::CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const
{
	return	NewMode == UE::Widget::EWidgetMode::WM_Translate
			|| NewMode == UE::Widget::EWidgetMode::WM_TranslateRotateZ
			|| NewMode == UE::Widget::EWidgetMode::WM_Rotate;
}

void FSmartObjectAssetEditorViewportClient::SetWidgetMode(UE::Widget::EWidgetMode NewMode)
{
	WidgetMode = NewMode;
}

void FSmartObjectAssetEditorViewportClient::SetWidgetCoordSystemSpace(ECoordSystem NewCoordSystem)
{
	WidgetCoordSystemSpace = NewCoordSystem;
}

void FSmartObjectAssetEditorViewportClient::BeginTransaction(FText Text)
{
	if (ScopedTransaction)
	{
		ScopedTransaction->Cancel();
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}

	ScopedTransaction = new FScopedTransaction(Text);
	check(ScopedTransaction);
}

void FSmartObjectAssetEditorViewportClient::EndTransaction()
{
	if (ScopedTransaction)
	{
		delete ScopedTransaction;
		ScopedTransaction = nullptr;
	}
}

void FSmartObjectAssetEditorViewportClient::TrackingStarted(const struct FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	if (!bIsManipulating && bIsDraggingWidget)
	{
		bIsManipulating = true;

		// Begin transaction
		BeginTransaction(LOCTEXT("ModifySlots", "Modify Slots(s)"));
		bIsManipulating = true;

	}
}

void FSmartObjectAssetEditorViewportClient::TrackingStopped()
{
	if (bIsManipulating)
	{
		// End transaction
		bIsManipulating = false;
		EndTransaction();
	}
}

bool FSmartObjectAssetEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	USmartObjectDefinition* Definition = SmartObjectDefinition.Get();
	if (Definition == nullptr)
	{
		return false;
	}

	Definition->SetFlags(RF_Transactional);
	Definition->Modify();

	if (bIsManipulating && CurrentAxis != EAxisList::None)
	{
		for (int32 Index = 0; Index < Definition->GetSlots().Num(); ++Index)
		{
			FSmartObjectSlotDefinition& Slot = Definition->GetMutableSlots()[Index];

			if (!Selection.Contains(Slot.ID))
			{
				continue;
			}

			if (!Drag.IsZero())
			{
				Slot.Offset += Drag;
			}

			if (!Rot.IsZero())
			{
				Slot.Rotation += Rot;
			}

			return true;
		}
	}
	
	return false;
}

void FSmartObjectAssetEditorViewportClient::SetSmartObjectDefinition(USmartObjectDefinition& InDefinition)
{
	SmartObjectDefinition = &InDefinition;
	if (Viewport)
	{
		FocusViewportOnBox(GetPreviewBounds());
	}
}

void FSmartObjectAssetEditorViewportClient::SetPreviewMesh(UStaticMesh* InStaticMesh)
{
	if (PreviewMeshComponent == nullptr)
	{
		PreviewMeshComponent = NewObject<UStaticMeshComponent>();
		ON_SCOPE_EXIT { PreviewScene->AddComponent(PreviewMeshComponent.Get(),FTransform::Identity); };
	}

	PreviewMeshComponent->SetStaticMesh(InStaticMesh);
	FocusViewportOnBox(GetPreviewBounds());
}

void FSmartObjectAssetEditorViewportClient::SetPreviewActor(AActor* InActor)
{
	if (AActor* Actor = PreviewActor.Get())
	{
		PreviewScene->GetWorld()->DestroyActor(Actor);
		PreviewActor.Reset();
	}

	if (InActor != nullptr)
	{
		PreviewActor = PreviewScene->GetWorld()->SpawnActor(InActor->GetClass());
	}

	FocusViewportOnBox(GetPreviewBounds());
}

void FSmartObjectAssetEditorViewportClient::SetPreviewActorClass(const UClass* ActorClass)
{
	if (AActor* Actor = PreviewActorFromClass.Get())
	{
		PreviewScene->GetWorld()->DestroyActor(Actor);
		PreviewActorFromClass.Reset();
	}

	if (ActorClass != nullptr)
	{
		PreviewActorFromClass = PreviewScene->GetWorld()->SpawnActor(const_cast<UClass*>(ActorClass));
	}

	FocusViewportOnBox(GetPreviewBounds());
}

FBox FSmartObjectAssetEditorViewportClient::GetPreviewBounds() const
{
	FBoxSphereBounds Bounds(FSphere(FVector::ZeroVector, 100.f));
	if (const AActor* Actor = PreviewActor.Get())
	{
		Bounds = Bounds+ Actor->GetComponentsBoundingBox();
	}

	if (const AActor* Actor = PreviewActorFromClass.Get())
	{
		Bounds = Bounds+ Actor->GetComponentsBoundingBox();
	}

	if (const UStaticMeshComponent* Component = PreviewMeshComponent.Get())
	{
		Bounds = Bounds + Component->CalcBounds(FTransform::Identity);
	}

	const TSharedRef<const FSmartObjectAssetToolkit> Toolkit = AssetEditorToolkit.Pin().ToSharedRef();
	const TArray< UObject* >* EditedObjects = Toolkit->GetObjectsCurrentlyBeingEdited();
	if (EditedObjects != nullptr)
	{
		for (const UObject* EditedObject : *EditedObjects)
		{
			const USmartObjectDefinition* Definition = Cast<USmartObjectDefinition>(EditedObject);
			if (IsValid(Definition))
			{
				Bounds = Bounds + Definition->GetBounds();
			}
		}
	}

	return Bounds.GetBox();
}

#undef LOCTEXT_NAMESPACE
