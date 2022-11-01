// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectComponentVisualizer.h"
#include "SmartObjectComponent.h"
#include "SmartObjectAnnotation.h"
#include "DebugRenderSceneProxy.h"
#include "SmartObjectAssetToolkit.h"
#include "SmartObjectVisualizationContext.h"
#include "CanvasTypes.h"

IMPLEMENT_HIT_PROXY(HSmartObjectSlotProxy, HComponentVisProxy);


namespace UE::SmartObjects::Editor
{

void Draw(const USmartObjectDefinition& Definition, TConstArrayView<FGuid> Selection, const FTransform& OwnerLocalToWorld, const FSceneView& View, FPrimitiveDrawInterface& PDI)
{
	FSmartObjectVisualizationContext VisContext(Definition);
	VisContext.OwnerLocalToWorld = OwnerLocalToWorld;
	VisContext.View = &View;
	VisContext.PDI = &PDI;
	VisContext.Font = GEngine->GetSmallFont();

	if (!VisContext.IsValidForDraw())
	{
		return;
	}

	FColor Color = FColor::White;
	FGuid SlotID;
	bool bIsSelected = false;

	for (int32 Index = 0; Index < Definition.GetSlots().Num(); ++Index)
	{
		const FSmartObjectSlotDefinition& Slot = Definition.GetSlots()[Index];
		
		constexpr float DebugCylinderRadius = 40.f;
		TOptional<FTransform> Transform = Definition.GetSlotTransform(OwnerLocalToWorld, FSmartObjectSlotIndex(Index));
		if (!Transform.IsSet())
		{
			continue;
		}
		bIsSelected = false;
#if WITH_EDITORONLY_DATA
		Color = Slot.DEBUG_DrawColor;
		SlotID = Slot.ID;

		if (Selection.Contains(Slot.ID))
		{
			Color = FColor::Red;
			bIsSelected = true;
		}
#endif 

		const FVector Location = Transform.GetValue().GetLocation();

		PDI.SetHitProxy(new HSmartObjectSlotProxy(/*Component*/nullptr, SlotID));

		DrawDirectionalArrow(&PDI, Transform.GetValue().ToMatrixNoScale(), Color, 2.f*DebugCylinderRadius, /*ArrowSize*/5.f, SDPG_World, /*Thickness*/1.0f);
		DrawCircle(&PDI, Location, FVector::XAxisVector, FVector::YAxisVector, Color, DebugCylinderRadius, /*NumSides*/64, SDPG_World, /*Thickness*/2.f);

		PDI.SetHitProxy(nullptr);
		
		for (const FInstancedStruct& Data : Slot.Data)
		{
			if (const FSmartObjectSlotAnnotation* Annotation = Data.GetPtr<FSmartObjectSlotAnnotation>())
			{
				VisContext.SlotIndex = FSmartObjectSlotIndex(Index);
				VisContext.bIsSlotSelected = bIsSelected;

				Annotation->DrawVisualization(VisContext);
			}
		}
		
	}
}

void DrawCanvas(const USmartObjectDefinition& Definition, TConstArrayView<FGuid> Selection, const FTransform& OwnerLocalToWorld, const FSceneView& View, FCanvas& Canvas)
{
	FSmartObjectVisualizationContext VisContext(Definition);
	VisContext.OwnerLocalToWorld = OwnerLocalToWorld;
	VisContext.View = &View;
	VisContext.Canvas = &Canvas;
	VisContext.Font = GEngine->GetSmallFont();

	if (!VisContext.IsValidForDrawHUD())
	{
		return;
	}

	FColor Color = FColor::White;
	bool bIsSelected = false;

	for (int32 Index = 0; Index < Definition.GetSlots().Num(); ++Index)
	{
		const FSmartObjectSlotDefinition& Slot = Definition.GetSlots()[Index];
		
		TOptional<FTransform> Transform = Definition.GetSlotTransform(OwnerLocalToWorld, FSmartObjectSlotIndex(Index));
		if (!Transform.IsSet())
		{
			continue;
		}

		bIsSelected = false;
#if WITH_EDITORONLY_DATA
		Color = Slot.DEBUG_DrawColor;

		if (Selection.Contains(Slot.ID))
		{
			Color = FColor::Red;
			bIsSelected = true;
		}
#endif 

		// Slot name
		const FVector SlotLocation = Transform->GetLocation();
		VisContext.DrawString(SlotLocation, *Slot.Name.ToString(), Color);

		// Slot data annotations
		for (const FInstancedStruct& Data : Slot.Data)
		{
			if (const FSmartObjectSlotAnnotation* Annotation = Data.GetPtr<FSmartObjectSlotAnnotation>())
			{
				VisContext.SlotIndex = FSmartObjectSlotIndex(Index);
				VisContext.bIsSlotSelected = bIsSelected;
				
				Annotation->DrawVisualizationHUD(VisContext);
			}
		}
	}
}

}; // UE::SmartObjects::Editor

void FSmartObjectComponentVisualizer::DrawVisualization( const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI )
{
	if (View == nullptr || PDI == nullptr)
	{
		return;
	}
	
	const USmartObjectComponent* SOComp = Cast<const USmartObjectComponent>(Component);
	if (SOComp == nullptr)
	{
		return;
	}

	const USmartObjectDefinition* Definition = SOComp->GetDefinition();
	if (Definition == nullptr)
	{
		return;
	}

	const FTransform OwnerLocalToWorld = SOComp->GetComponentTransform();

	UE::SmartObjects::Editor::Draw(*Definition, TConstArrayView<FGuid>(), OwnerLocalToWorld, *View, *PDI);
}


void FSmartObjectComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (View == nullptr || View == nullptr || Canvas == nullptr)
	{
		return;
	}

	const USmartObjectComponent* SOComp = Cast<const USmartObjectComponent>(Component);
	if (SOComp == nullptr)
	{
		return;
	}

	const USmartObjectDefinition* Definition = SOComp->GetDefinition();
	if (Definition == nullptr)
	{
		return;
	}

	const FTransform OwnerLocalToWorld = SOComp->GetComponentTransform();

	UE::SmartObjects::Editor::DrawCanvas(*Definition, TConstArrayView<FGuid>(), OwnerLocalToWorld, *View, *Canvas);
}
