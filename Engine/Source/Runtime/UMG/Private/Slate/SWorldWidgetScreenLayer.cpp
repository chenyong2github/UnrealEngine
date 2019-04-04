// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Slate/SWorldWidgetScreenLayer.h"
#include "Widgets/Layout/SBox.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Slate/SGameLayerManager.h"

SWorldWidgetScreenLayer::FComponentEntry::FComponentEntry()
	: Slot(nullptr)
{
}

SWorldWidgetScreenLayer::FComponentEntry::~FComponentEntry()
{
	Widget.Reset();
	ContainerWidget.Reset();
}

void SWorldWidgetScreenLayer::Construct(const FArguments& InArgs, const FLocalPlayerContext& InPlayerContext)
{
	PlayerContext = InPlayerContext;

	bCanSupportFocus = false;
	DrawSize = FVector2D(0, 0);
	Pivot = FVector2D(0.5f, 0.5f);

	ChildSlot
	[
		SAssignNew(Canvas, SConstraintCanvas)
	];
}

void SWorldWidgetScreenLayer::SetWidgetDrawSize(FVector2D InDrawSize)
{
	DrawSize = InDrawSize;
}

void SWorldWidgetScreenLayer::SetWidgetPivot(FVector2D InPivot)
{
	Pivot = InPivot;
}

void SWorldWidgetScreenLayer::AddComponent(USceneComponent* Component, TSharedRef<SWidget> Widget)
{
	if ( Component )
	{
		FComponentEntry& Entry = ComponentMap.FindOrAdd(FObjectKey(Component));
		Entry.Component = Component;
		Entry.WidgetComponent = Cast<UWidgetComponent>(Component);
		Entry.Widget = Widget;

		Canvas->AddSlot()
		.Expose(Entry.Slot)
		[
			SAssignNew(Entry.ContainerWidget, SBox)
			[
				Widget
			]
		];
	}
}

void SWorldWidgetScreenLayer::RemoveComponent(USceneComponent* Component)
{
	if (ensure(Component))
	{
		if (FComponentEntry* EntryPtr = ComponentMap.Find(Component))
		{
			if (!EntryPtr->bRemoving)
			{
				RemoveEntryFromCanvas(*EntryPtr);
				ComponentMap.Remove(Component);
			}
		}
	}
}

void SWorldWidgetScreenLayer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(SWorldWidgetScreenLayer_Tick);

	if ( APlayerController* PlayerController = PlayerContext.GetPlayerController() )
	{
		if ( UGameViewportClient* ViewportClient = PlayerController->GetWorld()->GetGameViewport() )
		{
			const FGeometry& ViewportGeometry = ViewportClient->GetGameLayerManager()->GetViewportWidgetHostGeometry();

			for ( auto It = ComponentMap.CreateIterator(); It; ++It )
			{
				FComponentEntry& Entry = It.Value();

				if ( USceneComponent* SceneComponent = Entry.Component.Get() )
				{
					FVector WorldLocation = SceneComponent->GetComponentLocation();

					//TODO NDarnell Perf This can be improved, if we get the projection matrix and do all of them in one go, instead of calling the library.

					FVector ViewportPosition;
					const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPositionWithDistance(PlayerController, WorldLocation, ViewportPosition);

					if ( bProjected )
					{
						Entry.ContainerWidget->SetVisibility(EVisibility::SelfHitTestInvisible);

						if ( SConstraintCanvas::FSlot* CanvasSlot = Entry.Slot )
						{
							FVector2D AbsoluteProjectedLocation = ViewportGeometry.LocalToAbsolute(FVector2D(ViewportPosition.X, ViewportPosition.Y));
							FVector2D LocalPosition = AllottedGeometry.AbsoluteToLocal(AbsoluteProjectedLocation);

							if ( Entry.WidgetComponent )
							{
								FVector2D ComponentDrawSize = Entry.WidgetComponent->GetDrawSize();
								FVector2D ComponentPivot = Entry.WidgetComponent->GetPivot();
								
								CanvasSlot->AutoSize(ComponentDrawSize.IsZero() || Entry.WidgetComponent->GetDrawAtDesiredSize());
								CanvasSlot->Offset(FMargin(LocalPosition.X, LocalPosition.Y, ComponentDrawSize.X, ComponentDrawSize.Y));
								CanvasSlot->Anchors(FAnchors(0, 0, 0, 0));
								CanvasSlot->Alignment(ComponentPivot);
								CanvasSlot->ZOrder(-ViewportPosition.Z);
							}
							else
							{
								CanvasSlot->AutoSize(DrawSize.IsZero());
								CanvasSlot->Offset(FMargin(LocalPosition.X, LocalPosition.Y, DrawSize.X, DrawSize.Y));
								CanvasSlot->Anchors(FAnchors(0, 0, 0, 0));
								CanvasSlot->Alignment(Pivot);
								CanvasSlot->ZOrder(-ViewportPosition.Z);
							}
						}
					}
					else
					{
						Entry.ContainerWidget->SetVisibility(EVisibility::Collapsed);
					}
				}
				else
				{
					RemoveEntryFromCanvas(Entry);
					It.RemoveCurrent();
					continue;
				}
			}
		}
	}
}

void SWorldWidgetScreenLayer::RemoveEntryFromCanvas(SWorldWidgetScreenLayer::FComponentEntry& Entry)
{
	// Mark the component was being removed, so we ignore any other remove requests for this component.
	Entry.bRemoving = true;

	if (TSharedPtr<SWidget> ContainerWidget = Entry.ContainerWidget)
	{
		Canvas->RemoveSlot(ContainerWidget.ToSharedRef());
	}
}