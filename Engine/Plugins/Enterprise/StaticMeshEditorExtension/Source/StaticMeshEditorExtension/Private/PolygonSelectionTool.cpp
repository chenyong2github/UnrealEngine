// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PolygonSelectionTool.h"
#include "MeshEditingContext.h"
#include "MeshEditorUtils.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "EditableMesh.h"
#include "EditableMeshFactory.h"
#include "EditableMeshTypes.h"
#include "Classes/EditorStyleSettings.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "MeshEditorSelectionModifiers.h"
#include "MouseDeltaTracker.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "ViewportWorldInteraction.h"
#include "ViewportInteractor.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionToolbar"

const FEditorModeID FPolygonSelectionTool::EM_PolygonSelection(TEXT("EM_PolygonSelection"));

// @todo mesheditor extensibility: This should probably be removed after we've evicted all current mesh editing actions to another module
namespace EPolygonSelectionAction
{
	/** Selecting mesh elements by 'painting' over multiple elements */
	const FName SelectByPainting( "SelectByPainting" );

	/** Moving elements using a transform gizmo */
	const FName MoveUsingGizmo( "MoveUsingGizmo" );

	/** Moving selected mesh elements (vertices, edges or polygons) */
	const FName Move( "Move" );

	/** Freehand vertex drawing */
	const FName DrawVertices( "DrawVertices" );
}

namespace PolygonSelectionToolUtils
{
	void FocusViewportOnBox(FEditorViewportClient* ViewportClient, const FBox& BoundingBox, bool bInstant)
	{
		// Based on FEditorViewportClient::FocusViewportOnBox

		const FVector Position = BoundingBox.GetCenter();
		float Radius = BoundingBox.GetExtent().Size();

		float AspectToUse = ViewportClient->AspectRatio;
		FIntPoint ViewportSize = ViewportClient->Viewport->GetSizeXY();
		if (ViewportSize.X > 0 && ViewportSize.Y > 0)
		{
			AspectToUse = ViewportClient->Viewport->GetDesiredAspectRatio();
		}

		const bool bEnable = false;
		ViewportClient->ToggleOrbitCamera(bEnable);

		{
			FViewportCameraTransform& ViewTransform = ViewportClient->GetViewTransform();

			if (!ViewportClient->IsOrtho())
			{
				/**
				 * We need to make sure we are fitting the sphere into the viewport completely, so if the height of the viewport is less
				 * than the width of the viewport, we scale the radius by the aspect ratio in order to compensate for the fact that we have
				 * less visible vertically than horizontally.
				 */
				if (AspectToUse > 1.0f)
				{
					Radius *= AspectToUse;
				}

				/**
				 * Now that we have a adjusted radius, we are taking half of the viewport's FOV,
				 * converting it to radians, and then figuring out the camera's distance from the center
				 * of the bounding sphere using some simple trig.  Once we have the distance, we back up
				 * along the camera's forward vector from the center of the sphere, and set our new view location.
				 */

				const float HalfFOVRadians = FMath::DegreesToRadians(ViewportClient->ViewFOV / 2.0f);
				float DistanceFromSphere = Radius / FMath::Tan(HalfFOVRadians);

				// Clamp the distance to prevent getting passed the near clipping plane
				DistanceFromSphere = FMath::Max(DistanceFromSphere, Radius + ViewportClient->GetNearClipPlane());
				FVector CameraOffsetVector = ViewTransform.GetRotation().Vector() * -DistanceFromSphere;

				ViewTransform.SetLookAt(Position);
				ViewTransform.TransitionToLocation(Position + CameraOffsetVector, ViewportClient->GetEditorViewportWidget(), bInstant);
			}
			else
			{
				// For ortho viewports just set the camera position to the center of the bounding volume.
				//SetViewLocation( Position );
				ViewTransform.TransitionToLocation(Position, ViewportClient->GetEditorViewportWidget(), bInstant);

				if (!(ViewportClient->Viewport->KeyState(EKeys::LeftControl) || ViewportClient->Viewport->KeyState(EKeys::RightControl)))
				{
					/**
					 * We also need to zoom out till the entire volume is in view.  The following block of code first finds the minimum dimension
					 * size of the viewport.  It then calculates backwards from what the view size should be (The radius of the bounding volume),
					 * to find the new OrthoZoom value for the viewport. The 6.0f is a fudge factor (smaller value to zoom closer).
					 */
					float NewOrthoZoom;
					uint32 MinAxisSize = (AspectToUse > 1.0f) ? ViewportClient->Viewport->GetSizeXY().Y : ViewportClient->Viewport->GetSizeXY().X;
					float Zoom = Radius / (MinAxisSize / 2.0f);

					NewOrthoZoom = Zoom * (ViewportClient->Viewport->GetSizeXY().X*6.0f);
					ViewTransform.SetOrthoZoom(NewOrthoZoom);
				}
			}
		}

		// Tell the viewport to redraw itself.
		ViewportClient->Invalidate();
	}
}

FPolygonSelectionTool::FPolygonSelectionTool()
	: ActiveAction(NAME_None)
	, StartPoint(FIntPoint::NoneValue)
	, EndPoint(FIntPoint::NoneValue)
	, bWindowSelectionEnabled(false)
	, bIncludeBackfaces(false)
{
	const TArray< UMeshEditorSelectionModifier* >& ModifierSet = MeshEditorSelectionModifiers::Get();
	for (UMeshEditorSelectionModifier* SelectionModifier : ModifierSet)
	{
		SelectionModifierMap.Add(SelectionModifier->GetSelectionModifierName(), SelectionModifier);
	}

	SelectionModeName = ModifierSet[0]->GetSelectionModifierName();
}

FPolygonSelectionTool::~FPolygonSelectionTool()
{
}

void FPolygonSelectionTool::Exit()
{
	if (EditingContext.IsValid())
	{
		EditingContext->ClearHoveredElements();
	}

	HoveredMeshElement = FMeshElement();

	FEdMode::Exit();
}

bool FPolygonSelectionTool::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	FInputEventState InputState(Viewport, Key, Event);

	if (InputState.IsAltButtonPressed())
	{
		if (StartPoint != FIntPoint::NoneValue)
		{
			StartPoint = FIntPoint::NoneValue;
			EndPoint = FIntPoint::NoneValue;
		}

		return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	}

	// Other keys without modifiers
	if (EditingContext->GetSelectedElements(EEditableMeshElementType::Any).Num() > 0 &&
		!(InputState.IsShiftButtonPressed() ||
		  InputState.IsCtrlButtonPressed() ||
		  Viewport->KeyState(EKeys::LeftCommand) || Viewport->KeyState(EKeys::RightCommand)))
	{
		// Focus on selected elements when pressing 'F' without any key modifiers
		if (Key == EKeys::F)
		{
			TArray<FMeshElement> SelectedMeshElements = EditingContext->GetSelectedElements(EEditableMeshElementType::Any);
			FBox BoundingBox = FMeshEditingUtils::GetElementsBoundingBox(SelectedMeshElements, EditingContext->GetEditableMesh());
			PolygonSelectionToolUtils::FocusViewportOnBox(ViewportClient, BoundingBox, false);
			return true;
		}
	}

	if (Key != EKeys::LeftMouseButton)
	{
		return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	}

	if (InputState.IsLeftMouseButtonPressed() && !InputState.IsMiddleMouseButtonPressed() && !InputState.IsRightMouseButtonPressed() && InputState.IsMouseButtonEvent() && EditingContext.IsValid())
	{
		bWindowSelectionEnabled = true;
		StartPoint.X = Viewport->GetMouseX();
		StartPoint.Y = Viewport->GetMouseY();
		EndPoint = StartPoint;

		FIntersectionData IntersectionData = BuildIntersectionData(ViewportClient, Viewport,  Viewport->GetMouseX(), Viewport->GetMouseY());

		FMeshElement MeshElement = FMeshEditingUtils::FindClosestMeshElement(EditingContext->GetStaticMeshComponent(), IntersectionData);

		if ( MeshElement.IsValidMeshElement() )
		{
			if (!Viewport->KeyState(EKeys::LeftControl) && !Viewport->KeyState(EKeys::RightControl))
			{
				bool bElementWasSelected = EditingContext->IsSelected(MeshElement);

				EditingContext->ClearSelectedElements();

				if (bElementWasSelected)
				{
					return true;
				}
			}

			TArray<FMeshElement> SelectedMeshElements = GetSelectedMeshElements(MeshElement);

			EditingContext->ToggleElementsSelection(SelectedMeshElements);

			if (HoveredMeshElement.IsValidMeshElement())
			{
				EditingContext->ClearHoveredElements();
				HoveredMeshElement = FMeshElement();
			}

			return true;
		}

		EditingContext->ClearSelectedElements();
	}

	// Handle polygon selection by click-dragging a rectangle around the desired area on left mouse button release
	if (!InputState.IsAnyMouseButtonDown() && InputState.IsMouseButtonEvent() && EditingContext.IsValid())
	{
		if (bWindowSelectionEnabled)
		{
			// Ensure the min point is always the top-left corner of the rectangle and max point, the bottom-right corner
			FIntPoint MinPoint = StartPoint.ComponentMin(EndPoint);
			FIntPoint MaxPoint = StartPoint.ComponentMax(EndPoint);

			// Don't handle point and zero-width lines
			if (MinPoint.X != MaxPoint.X && MinPoint.Y != MaxPoint.Y)
			{
				FQuadIntersectionData QuadIntersectionData = BuildQuadIntersectionData(ViewportClient, Viewport, MinPoint, MaxPoint);
				TArray<FMeshElement> MeshElements = FMeshEditingUtils::FindMeshElementsInVolume(EditingContext->GetStaticMeshComponent(), QuadIntersectionData);

				if (MeshElements.Num() > 0)
				{
					EditingContext->AddElementsToSelection(MeshElements);
				}
			}

			StartPoint = FIntPoint::NoneValue;
			EndPoint = FIntPoint::NoneValue;

			bWindowSelectionEnabled = false;
		}

		return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	}

	return true;
}

void FPolygonSelectionTool::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	// Call parent implementation
	FEdMode::Tick(ViewportClient, DeltaTime);
}
bool FPolygonSelectionTool::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32, int32)
{
	Viewport->ShowCursor(true);
	return true;
}

bool FPolygonSelectionTool::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return true;
}

bool FPolygonSelectionTool::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	if (!EditingContext.IsValid())
	{
		return true;
	}

	if (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt))
	{
		return false;
	}

	FIntersectionData IntersectionData = BuildIntersectionData(ViewportClient, Viewport, MouseX, MouseY);

	FMeshElement MeshElement = FMeshEditingUtils::FindClosestMeshElement(EditingContext->GetStaticMeshComponent(), IntersectionData);
	if ( MeshElement.IsValidMeshElement() )
	{
		if (HoveredMeshElement.ElementAddress == MeshElement.ElementAddress)
		{
			return true;
		}

		if (HoveredMeshElement.IsValidMeshElement())
		{
			EditingContext->ClearHoveredElements();
		}

		HoveredMeshElement = MeshElement;
		EditingContext->AddHoveredElement(HoveredMeshElement);

		return true;
	}

	if (HoveredMeshElement.IsValidMeshElement())
	{
		EditingContext->ClearHoveredElements();
		HoveredMeshElement = FMeshElement();
	}

	return false;
}

bool FPolygonSelectionTool::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	if (bWindowSelectionEnabled)
	{
		EndPoint.X = InMouseX;
		EndPoint.Y = InMouseY;
	}
	return true;
}

void FPolygonSelectionTool::SetContext(const TSharedPtr<FMeshEditingUIContext>& InEditingContext)
{
	EditingContext = InEditingContext;
}

TArray<FMeshElement> FPolygonSelectionTool::GetSelectedMeshElements(const FMeshElement& MeshElement)
{
	if (!EditingContext.IsValid())
	{
		return TArray<FMeshElement>();
	}

	TArray<FMeshElement> SelectedMeshElements;

	SelectedMeshElements.Add(MeshElement);

	UMeshEditorSelectionModifier** SelectionModifierPtr = SelectionModifierMap.Find(SelectionModeName);
	if (SelectionModifierPtr != nullptr)
	{
		UEditableMesh* EditableMesh = EditingContext->GetEditableMesh();

		TMap< UEditableMesh*, TArray< FMeshElement > > InOutSelection;
		InOutSelection.Add(EditableMesh, SelectedMeshElements);

		if ((*SelectionModifierPtr)->ModifySelection(InOutSelection) && InOutSelection[EditableMesh].Num() > 0)
		{
			SelectedMeshElements = InOutSelection[EditableMesh];
		}
	}

	return SelectedMeshElements;
}

FIntersectionData FPolygonSelectionTool::BuildIntersectionData(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	if (!EditingContext.IsValid())
	{
		return FIntersectionData();
	}

	FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues( Viewport, ViewportClient->GetScene(), FEngineShowFlags(ESFIM_Editor) ));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportClick ViewportClick(View, ViewportClient, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, MouseX, MouseY);

	FIntersectionData IntersectionData;

	IntersectionData.LaserStart = ViewportClient->IsPerspective() ? FMath::RayPlaneIntersection(ViewportClick.GetOrigin(), ViewportClick.GetDirection(), View->NearClippingPlane) : ViewportClick.GetOrigin();
	IntersectionData.LaserEnd = ViewportClick.GetOrigin() + ViewportClick.GetDirection() * HALF_WORLD_MAX;
	IntersectionData.bUseGrabberSphere = false; // @todo: Revisit after understanding UMeshEditorSettings
	IntersectionData.bIsPerspectiveView = ViewportClient->IsPerspective();
	IntersectionData.CameraToWorld = FTransform( ViewportClient->GetViewTransform().GetRotation(), ViewportClient->GetViewTransform().GetLocation() );
	IntersectionData.EditingContext = EditingContext;
	IntersectionData.MeshElementSelectionMode = EEditableMeshElementType::Polygon;
	IntersectionData.WorldScaleFactor = ViewportClient->GetWorld()->GetWorldSettings()->WorldToMeters / 100.0f;
	IntersectionData.bIncludeBackfaces = bIncludeBackfaces;

	if (!IntersectionData.bIsPerspectiveView)
	{
		// In orthographic views, the camera doesn't have a height so its location is on a plane at 0 height and will give inaccurate results for intersection tests
		// However, we can give it a big offset such that the ray casted from the camera location to polygon centers are approximately parallel to the laser ray
		// See MeshEditingUtilsImpl::GetFilteredTriangleData
		FVector Offset = (IntersectionData.LaserStart - IntersectionData.LaserEnd) / 2.f;
		IntersectionData.LaserStart += Offset;
		IntersectionData.LaserEnd += Offset;
		IntersectionData.CameraToWorld = FTransform(ViewportClient->GetViewTransform().GetRotation(), ViewportClient->GetViewTransform().GetLocation() + Offset);
	}

	return IntersectionData;
}

FQuadIntersectionData FPolygonSelectionTool::BuildQuadIntersectionData(FEditorViewportClient* ViewportClient, FViewport* Viewport, FIntPoint MinPoint, FIntPoint MaxPoint)
{
	if (!EditingContext.IsValid())
	{
		return FQuadIntersectionData();
	}

	// Build a QuadIntersectionData from the IntersectionData of the top-left corner of the rectangle
	FQuadIntersectionData QuadIntersectionData = BuildIntersectionData(ViewportClient, Viewport, MinPoint.X, MinPoint.Y);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, ViewportClient->GetScene(), FEngineShowFlags(ESFIM_Editor)));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);

	// Fill out the other rays with the coordinates for the top-right corner
	FViewportClick TopRightClick(View, ViewportClient, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, MaxPoint.X, MinPoint.Y);
	QuadIntersectionData.LaserStart2 = QuadIntersectionData.bIsPerspectiveView ? FMath::RayPlaneIntersection(TopRightClick.GetOrigin(), TopRightClick.GetDirection(), View->NearClippingPlane) : TopRightClick.GetOrigin();
	QuadIntersectionData.LaserEnd2 = TopRightClick.GetOrigin() + TopRightClick.GetDirection() * HALF_WORLD_MAX;

	// Bottom-left corner
	FViewportClick BottomLeftClick(View, ViewportClient, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, MinPoint.X, MaxPoint.Y);
	QuadIntersectionData.LaserStart3 = QuadIntersectionData.bIsPerspectiveView ? FMath::RayPlaneIntersection(BottomLeftClick.GetOrigin(), BottomLeftClick.GetDirection(), View->NearClippingPlane) : BottomLeftClick.GetOrigin();
	QuadIntersectionData.LaserEnd3 = BottomLeftClick.GetOrigin() + BottomLeftClick.GetDirection() * HALF_WORLD_MAX;

	// And bottom-right corner
	FViewportClick BottomRightClick(View, ViewportClient, EKeys::LeftMouseButton, EInputEvent::IE_Pressed, MaxPoint.X, MaxPoint.Y);
	QuadIntersectionData.LaserStart4 = QuadIntersectionData.bIsPerspectiveView ? FMath::RayPlaneIntersection(BottomRightClick.GetOrigin(), BottomRightClick.GetDirection(), View->NearClippingPlane) : BottomRightClick.GetOrigin();
	QuadIntersectionData.LaserEnd4 = BottomRightClick.GetOrigin() + BottomRightClick.GetDirection() * HALF_WORLD_MAX;

	return QuadIntersectionData;
}

void FPolygonSelectionTool::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* InViewport, const FSceneView* View, FCanvas* InCanvas)
{
	if (StartPoint != FIntPoint::NoneValue)
	{
		float DPIScale = InCanvas->GetDPIScale();
		FVector2D Origin = FVector2D(StartPoint) / DPIScale;
		FVector2D Size = FVector2D(EndPoint - StartPoint) / DPIScale;

		// Draw translucent white rectangle
		FCanvasTileItem BoxBackgroundTileItem(Origin, GWhiteTexture, Size, FLinearColor(1.f, 1.f, 1.f, 0.4f));
		BoxBackgroundTileItem.BlendMode = SE_BLEND_Translucent;
		InCanvas->DrawItem(BoxBackgroundTileItem);

		// Draw black border
		FCanvasBoxItem BoxItem(Origin, Size);
		BoxItem.SetColor(FLinearColor::Black);
		InCanvas->DrawItem(BoxItem);
	}
}

bool FPolygonSelectionTool::GetPivotForOrbit(FVector& Pivot) const
{
	TArray<FMeshElement> SelectedMeshElements = EditingContext->GetSelectedElements(EEditableMeshElementType::Any);
	if (SelectedMeshElements.Num() > 0)
	{
		Pivot = FMeshEditingUtils::GetElementsBoundingBox(SelectedMeshElements, EditingContext->GetEditableMesh()).GetCenter();
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE