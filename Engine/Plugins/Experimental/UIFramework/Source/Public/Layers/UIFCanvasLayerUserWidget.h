// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Layers/UIFCanvasLayer.h"

#include "UIFCanvasLayerUserWidget.generated.h"


/**
 *
 */
UCLASS(meta = (DisableNativeTick))
class UUIFCanvasLayerUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void AddWidget(UWidget* InWidget, const FUIFCanvasLayerSlot& InSlot)
	{
		if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(InWidget))
		{
			UpdateSlot(PanelSlot, InSlot);
		}
	}

	void UpdateWidget(UWidget* InWidget, const FUIFCanvasLayerSlot& InSlot)
	{
		for (const UPanelSlot* PanelSlot : Canvas->GetSlots())
		{
			check(PanelSlot);
			if (PanelSlot->Content == InWidget)
			{
				UpdateSlot(CastChecked<UCanvasPanelSlot>(Slot), InSlot);
				break;
			}
		}
	}

	void RemoveWidget(UWidget* InWidget)
	{
		Canvas->RemoveChild(InWidget);
	}

private:
	void UpdateSlot(UCanvasPanelSlot* InCanvasSlot, const FUIFCanvasLayerSlot& InSlot)
	{
		check(InCanvasSlot);
		FAnchorData AnchorData;
		AnchorData.Offsets = InSlot.Offsets;
		AnchorData.Anchors = InSlot.Anchors;
		AnchorData.Alignment = FVector2D(InSlot.Alignment);
		InCanvasSlot->SetLayout(AnchorData);
		InCanvasSlot->SetZOrder(InSlot.ZOrder);
		InCanvasSlot->SetAutoSize(InSlot.bSizeToContent);
	}

private:
	UPROPERTY(BlueprintReadOnly, Category="UIFramework", meta = (BindWidget, AllowPrivateAccess))
	UCanvasPanel* Canvas = nullptr;
};
