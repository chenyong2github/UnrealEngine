// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditorViewportClient.h"
#include "LidarPointCloudEditor.h"
#include "LidarPointCloudEditorViewport.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudComponent.h"

#include "ConvexVolume.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Classes/EditorStyleSettings.h"
#include "AssetViewerSettings.h"

#include "GeomTools.h"

#define LOCTEXT_NAMESPACE "FLidarPointCloudEditorViewportClient"

namespace UE {
	namespace Lidar {
		namespace Private {
			namespace Editor
			{
				// Distance Square between the first and last points of the polygonal selection, where the shape will be considered as closed
				constexpr int32 PolySnapDistanceSq = 40;

				// Affects the frequency of new point injections when drawing lasso-based shapes
				constexpr int32 LassoSpacingSq = 400;

				// Affects the max depth delta when painting. Prevents the brush from "falling through" the gaps.
				constexpr float PaintMaxDeviation = 0.15f;

				// Defaults for the common draw helper
				constexpr float GridSize = 2048.0f;
				constexpr int32 CellSize = 16;
			}
		}
	}
}

TArray<FVector2D> ToVectorArray(const TArray<FIntPoint>& Points)
{
	TArray<FVector2D> VectorPoints;
	VectorPoints.Reserve(Points.Num());
	for (const FIntPoint& Point : Points)
	{
		VectorPoints.Add(Point);
	}
	return VectorPoints;
}

// Slow, O(n2), but sufficient for the current problem
bool IsPolygonSelfIntersecting(const TArray<FVector2D>& Points, bool bAllowLooping)
{
	const int32 MaxIndex = bAllowLooping ? Points.Num() : Points.Num() - 1;

	for (int32 i = 0; i < MaxIndex; ++i)
	{
		const int32 i1 = i < Points.Num() - 1 ? i + 1 : 0;

		const FVector2D P1 = Points[i];
		const FVector2D P2 = Points[i1];

		for (int32 j = 0; j < MaxIndex; ++j)
		{
			const int32 j1 = j < Points.Num() - 1 ? j + 1 : 0;

			if (j1 != i && j != i && j != i1)
			{
				// Modified, inlined FMath::SegmentIntersection2D
				const FVector2D SegmentStartA = P1;
				const FVector2D SegmentEndA = P2;
				const FVector2D SegmentStartB = Points[j];
				const FVector2D SegmentEndB = Points[j1];
				const FVector2D VectorA = P2 - SegmentStartA;
				const FVector2D VectorB = SegmentEndB - SegmentStartB;

				const float S = (-VectorA.Y * (SegmentStartA.X - SegmentStartB.X) + VectorA.X * (SegmentStartA.Y - SegmentStartB.Y)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);
				const float T = (VectorB.X * (SegmentStartA.Y - SegmentStartB.Y) - VectorB.Y * (SegmentStartA.X - SegmentStartB.X)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);

				if (S >= 0 && S <= 1 && T >= 0 && T <= 1)
				{
					return true;
				}
			}
		}
	}

	return false;
}


// Copied from GeomTools.cpp and converted to work with FIntPoint
bool IsPolygonConvex(const TArray<FIntPoint>& Points)
{
	const int32 PointCount = Points.Num();
	int32 Sign = 0;
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		const FIntPoint& A = Points[PointIndex];
		const FIntPoint& B = Points[(PointIndex + 1) % PointCount];
		const FIntPoint& C = Points[(PointIndex + 2) % PointCount];
		int32 Det = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
		int32 DetSign = FMath::Sign(Det);
		if (DetSign != 0)
		{
			if (Sign == 0)
			{
				Sign = DetSign;
			}
			else if (Sign != DetSign)
			{
				return false;
			}
		}
	}

	return true;
}

FLidarPointCloudEditorViewportClient::FLidarPointCloudEditorViewportClient(TWeakPtr<FLidarPointCloudEditor> InPointCloudEditor, const TSharedRef<SLidarPointCloudEditorViewport>& InPointCloudEditorViewport, FAdvancedPreviewScene* InPreviewScene, ULidarPointCloud* InPreviewPointCloud, ULidarPointCloudComponent* InPreviewPointCloudComponent)
	: FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InPointCloudEditorViewport))
	, PointCloudComponent(InPreviewPointCloudComponent)
	, PointCloudEditorPtr(InPointCloudEditor)
	, PointCloudEditorViewportPtr(InPointCloudEditorViewport)
	, SelectionMethod(ELidarPointCloudSelectionMethod::Box)
	, SelectionMode(ELidarPointCloudSelectionMode::None)
	, PaintingRadius(500)
{
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = true;
	DrawHelper.GridColorAxis = FColor(160, 160, 160);
	DrawHelper.GridColorMajor = FColor(144, 144, 144);
	DrawHelper.GridColorMinor = FColor(128, 128, 128);
	DrawHelper.PerspectiveGridSize = UE::Lidar::Private::Editor::GridSize;
	DrawHelper.NumCells = DrawHelper.PerspectiveGridSize / (UE::Lidar::Private::Editor::CellSize * 2);

	SetViewMode(VMI_Unlit);

	EngineShowFlags.SetSeparateTranslucency(true);
	EngineShowFlags.SetSnap(0);
	EngineShowFlags.SetCompositeEditorPrimitives(true);
	OverrideNearClipPlane(1.0f);
	bUsingOrbitCamera = true;

	AdvancedPreviewScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);

	// Register delegate to update the show flags when the post processing is turned on or off
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().AddRaw(this, &FLidarPointCloudEditorViewportClient::OnAssetViewerSettingsChanged);

	// Set correct flags according to current profile settings
	SetAdvancedShowFlagsForScene(UAssetViewerSettings::Get()->Profiles[GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex].bPostProcessingEnabled);
}

FLidarPointCloudEditorViewportClient::~FLidarPointCloudEditorViewportClient()
{
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().RemoveAll(this);
}

void FLidarPointCloudEditorViewportClient::Tick(float DeltaSeconds)
{
	using UE::Lidar::Private::Editor::PaintMaxDeviation;

	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}

	// Process line traces if in Paint mode
	if (SelectionMethod == ELidarPointCloudSelectionMethod::Paint)
	{
		if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
		{
			ULidarPointCloud* PC = Editor->GetPointCloudBeingEdited();

			if (Editor->IsEditMode())
			{
				const bool bPainting = Viewport->KeyState(EKeys::LeftMouseButton) && bLineTraceHit;
				const float TraceRadius = FMath::Max(PC->GetEstimatedPointSpacing(), 0.5f);

				const FLidarPointCloudRay Ray = DeprojectCurrentMousePosition();
				if (FLidarPointCloudPoint* Point = PC->LineTraceSingle(Ray, TraceRadius, true))
				{
					const float NewDistance = FVector::Dist(Point->Location, Ray.Origin);
					const float Deviation = (NewDistance - LineTraceDistance) / LineTraceDistance;

					// If painting, prevent large depth changes
					// If not, query larger trace radius - if it passes the deviation test, it was a gap
					if (Deviation > PaintMaxDeviation && (bPainting || (FVector::Dist(PC->LineTraceSingle(Ray, TraceRadius * 6, true)->Location, Ray.Origin) - LineTraceDistance) / LineTraceDistance <= PaintMaxDeviation))
					{
						LineTraceHitPoint = Ray.Origin + Ray.GetDirection() * LineTraceDistance;
					}
					else
					{
						LineTraceHitPoint = Point->Location;
						LineTraceDistance = NewDistance;
					}

					bLineTraceHit = true;
				}
				else if(!bPainting)
				{
					bLineTraceHit = false;
				}
			}
		}
	}
}

bool FLidarPointCloudEditorViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool Gamepad)
{
	using UE::Lidar::Private::Editor::PolySnapDistanceSq;

	bool bHandled = false;

	const bool bAlt = Key == EKeys::LeftAlt || Key == EKeys::RightAlt;
	const bool bCtrl = Key == EKeys::LeftControl || Key == EKeys::RightControl;

	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		// Edit Mode
		if (Editor->IsEditMode())
		{
			if (Event == IE_Pressed)
			{
				if (Key == EKeys::Delete)
				{
					if (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift))
					{
						Editor->DeletePoints();
					}
					else
					{
						Editor->HidePoints();
					}

					bHandled = true;
				}
				else if (Key == EKeys::Escape)
				{
					Editor->DeselectPoints();
					SelectionPoints.Empty();
					bHandled = true;
				}
				else if (Key == EKeys::Enter)
				{
					if (SelectionMethod == ELidarPointCloudSelectionMethod::Polygonal)
					{
						OnPolygonalSelectionEnd();
						SelectionPoints.Empty();
						bHandled = true;
					}
				}
				else if (bAlt)
				{
					bHandled = true;
					SelectionMode = ELidarPointCloudSelectionMode::Subtract;
				}
				else if (bCtrl)
				{
					bHandled = true;
					SelectionMode = ELidarPointCloudSelectionMode::Add;
				}
				else if (Key == EKeys::LeftMouseButton)
				{
					bHandled = true;

					// Selection start
					if (SelectionMethod == ELidarPointCloudSelectionMethod::Box)
					{
						// Mark the cursor location for selection start
						InViewport->GetMousePos(SelectionPoints[SelectionPoints.AddUninitialized()]);
					}
					else if (SelectionMethod == ELidarPointCloudSelectionMethod::Polygonal)
					{
						// Add new lasso point
						FIntPoint NewPoint;
						InViewport->GetMousePos(NewPoint);

						// Don't allow duplicates
						if (SelectionPoints.Num() == 0 || SelectionPoints.Last() != NewPoint)
						{
							TArray<FVector2D> VectorPoints = ToVectorArray(SelectionPoints);
							VectorPoints.Add(NewPoint);

							if (!IsPolygonSelfIntersecting(VectorPoints, false))
							{
								// Snap to first point
								if (SelectionPoints.Num() > 1 && (NewPoint - SelectionPoints[0]).SizeSquared() < PolySnapDistanceSq)
								{
									OnPolygonalSelectionEnd();
									SelectionPoints.Empty();
								}
								else
								{
									SelectionPoints.Add(NewPoint);
								}
							}
						}
					}
					else if (SelectionMethod == ELidarPointCloudSelectionMethod::Lasso)
					{
						InViewport->GetMousePos(SelectionPoints[SelectionPoints.AddUninitialized()]);
					}
					else if (SelectionMethod == ELidarPointCloudSelectionMethod::Paint)
					{
						OnPaintSelection();
					}
				}

				if (SelectionMethod == ELidarPointCloudSelectionMethod::Paint)
				{
					// Do not block ability to change camera speed
					if (!Viewport->KeyState(EKeys::RightMouseButton))
					{
						if (Key == EKeys::MouseScrollUp)
						{
							PaintingRadius *= 1.1f;
							bHandled = true;
						}
						else if (Key == EKeys::MouseScrollDown)
						{
							PaintingRadius /= 1.1f;
							bHandled = true;
						}
					}
				}
			}
			else if (Event == IE_Released)
			{
				if (bAlt || bCtrl)
				{
					SelectionMode = ELidarPointCloudSelectionMode::None;
					bHandled = true;
				}
				else if (Key == EKeys::LeftMouseButton)
				{
					bHandled = true;

					if (SelectionMethod == ELidarPointCloudSelectionMethod::Box)
					{
						// Mark the cursor location for selection end
						InViewport->GetMousePos(SelectionPoints[SelectionPoints.AddUninitialized()]);
						OnBoxSelectionEnd();
						SelectionPoints.Empty();
					}
					else if (SelectionMethod == ELidarPointCloudSelectionMethod::Lasso)
					{
						OnLassoSelectionEnd();
						SelectionPoints.Empty();
					}
				}
			}
		}
		// Navigation Mode
		else
		{

		}
	}

	if (!bHandled)
	{
		bHandled = FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, false);

		// Handle viewport screenshot.
		bHandled |= InputTakeScreenshot(InViewport, Key, Event);

		bHandled |= AdvancedPreviewScene->HandleInputKey(InViewport, ControllerId, Key, Event, AmountDepressed, Gamepad);

	}

	return bHandled;
}

bool FLidarPointCloudEditorViewportClient::InputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	using UE::Lidar::Private::Editor::LassoSpacingSq;

	bool bHandled = false;

	if (!bDisableInput)
	{
		if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
		{
			if (Editor->IsEditMode())
			{
				if (Viewport->KeyState(EKeys::LeftMouseButton))
				{
					if (SelectionMethod == ELidarPointCloudSelectionMethod::Lasso)
					{
						FIntPoint NewPoint;
						InViewport->GetMousePos(NewPoint);

						// Check if the spacing is sufficient
						if ((NewPoint - SelectionPoints.Last()).SizeSquared() > LassoSpacingSq)
						{
							SelectionPoints.Add(NewPoint);
						}

						bHandled = true;
					}
					else if (SelectionMethod == ELidarPointCloudSelectionMethod::Paint)
					{
						OnPaintSelection();
						bHandled = true;
					}
				}
			}
		}

		if(!bHandled)
		{
			bHandled = AdvancedPreviewScene->HandleViewportInput(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			if (bHandled)
			{
				Invalidate();
			}
			else
			{
				bHandled = FEditorViewportClient::InputAxis(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			}
		}
	}

	return bHandled;
}

void FLidarPointCloudEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	TArray<SLidarPointCloudEditorViewport::FOverlayTextItem> TextItems;

	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		if (Editor->IsEditMode())
		{
			TArray<FString> Labels;

			// Add mode-specific labels
			switch (SelectionMethod)
			{
			case ELidarPointCloudSelectionMethod::Box:
				DrawSelectionBox(Canvas);
				Labels.Append({
					"BOX SELECTION MODE",
					"",
					"Click + Drag to replace selection",
					"[CTRL] to add selection",
					"[ALT] to subtract selection"
				});
				break;

			case ELidarPointCloudSelectionMethod::Polygonal:
				DrawSelectionPolygonal(Canvas);
				Labels.Append({
					"POLYGONAL SELECTION MODE",
					"",
					"[CTRL] to add selection",
					"[ALT] to subtract selection"
					});
				break;

			case ELidarPointCloudSelectionMethod::Lasso:
				DrawSelectionLasso(Canvas);
				Labels.Append({
					"LASSO SELECTION MODE",
					"",
					"Click + Drag to paint selection shape",
					"[CTRL] to add selection",
					"[ALT] to subtract selection"
				});
				break;

			case ELidarPointCloudSelectionMethod::Paint:
				DrawSelectionPaint(Canvas);
				Labels.Append({
					"PAINT SELECTION MODE",
					"",
					"Click + Drag to paint selection",
					"[SCROLL] to change brush size",
					"[ALT] to subtract selection"
				});
				break;

			default:
				break;
			}

			// Add common labels
			Labels.Append({
				"",
				"[ESCAPE] to de-select all points",
				"[DELETE] to hide selected points",
				"[SHIFT] + [DELETE] to permanently delete selected points",
				"",
				FString::Printf(TEXT("Selected Points: %d"), Editor->GetSelectedPoints().Num())
			});

			// Convert to text entries
			TextItems.Reserve(Labels.Num());
			for (const FString& Label : Labels)
			{
				TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString(Label)));
			}
		}
	}

	if (TSharedPtr<SLidarPointCloudEditorViewport> PointCloudEditorViewport = PointCloudEditorViewportPtr.Pin())
	{
		PointCloudEditorViewport->PopulateOverlayText(TextItems);
	}
}

bool FLidarPointCloudEditorViewportClient::ShouldOrbitCamera() const
{
	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		if (Editor->IsEditMode())
		{
			return false;
		}
	}

	return GetDefault<ULevelEditorViewportSettings>()->bUseUE3OrbitControls || FEditorViewportClient::ShouldOrbitCamera();
}

void FLidarPointCloudEditorViewportClient::LostFocus(FViewport* InViewport)
{
	FEditorViewportClient::LostFocus(InViewport);

	// Cancel selection
	SelectionMode = ELidarPointCloudSelectionMode::None;
	SelectionPoints.Empty();
}

void FLidarPointCloudEditorViewportClient::ReceivedFocus(FViewport* InViewport)
{
	// This is needed if the user presses Alt / Ctrl / Shift before the client acquires focus
	if (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt))
	{
		SelectionMode = ELidarPointCloudSelectionMode::Subtract;
	}
	else if (Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl))
	{
		SelectionMode = ELidarPointCloudSelectionMode::Add;
	}
}

void FLidarPointCloudEditorViewportClient::PerspectiveCameraMoved()
{
	FEditorViewportClient::PerspectiveCameraMoved();

	// If in the process of transitioning to a new location, don't update the orbit camera position.
	// On the final update of the transition, we will get here with IsPlaying()==false, and the editor camera position will
	// be correctly updated.
	if (GetViewTransform().IsPlaying())
	{
		return;
	}

	ToggleOrbitCamera(bUsingOrbitCamera);
}

void FLidarPointCloudEditorViewportClient::OnAssetViewerSettingsChanged(const FName& InPropertyName)
{
	if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bPostProcessingEnabled) || InPropertyName == NAME_None)
	{
		UAssetViewerSettings* Settings = UAssetViewerSettings::Get();
		const int32 ProfileIndex = AdvancedPreviewScene->GetCurrentProfileIndex();
		if (Settings->Profiles.IsValidIndex(ProfileIndex))
		{
			SetAdvancedShowFlagsForScene(Settings->Profiles[ProfileIndex].bPostProcessingEnabled);
		}
	}
}

void FLidarPointCloudEditorViewportClient::SetAdvancedShowFlagsForScene(const bool bAdvancedShowFlags)
{
	if (bAdvancedShowFlags)
	{
		EngineShowFlags.EnableAdvancedFeatures();
	}
	else
	{
		EngineShowFlags.DisableAdvancedFeatures();
	}
}

FSceneView* FLidarPointCloudEditorViewportClient::GetView()
{
	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		// Compute a view.
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags).SetRealtimeUpdate(IsRealtime()));
		FSceneView* View = CalcSceneView(&ViewFamily);

		const FVector LocationOffset = Editor->GetPointCloudBeingEdited()->LocationOffset.ToVector();

		// Adjust for the LocationOffset
		if (View->IsPerspectiveProjection())
		{
			View->ViewLocation -= LocationOffset;
			View->UpdateViewMatrix();
		}

		return View;
	}

	return nullptr;
}

FLidarPointCloudRay FLidarPointCloudEditorViewportClient::DeprojectCurrentMousePosition()
{
	FSceneView* View = GetView();
	const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();

	FIntPoint CurrentMousePosition;
	Viewport->GetMousePos(CurrentMousePosition);

	FVector Origin, Direction;
	FSceneView::DeprojectScreenToWorld(FVector2D(CurrentMousePosition), FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origin, Direction);

	return FLidarPointCloudRay(Origin, Direction);
}

void FLidarPointCloudEditorViewportClient::OnBoxSelectionEnd()
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
		{
			if (SelectionPoints[0] == SelectionPoints.Last())
			{
				Editor->DeselectPoints();
				return;
			}

			FIntVector4 SelectionArea;
			SelectionArea.X = FMath::Min(SelectionPoints[0].X, SelectionPoints[1].X);
			SelectionArea.Y = FMath::Min(SelectionPoints[0].Y, SelectionPoints[1].Y);
			SelectionArea.Z = FMath::Max(SelectionPoints[0].X, SelectionPoints[1].X);
			SelectionArea.W = FMath::Max(SelectionPoints[0].Y, SelectionPoints[1].Y);

			const FConvexVolume ConvexVolume = BuildConvexVolumeForPoints(TArray<FVector2D>({
				FVector2D(SelectionArea.X, SelectionArea.Y),
				FVector2D(SelectionArea.X, SelectionArea.W),
				FVector2D(SelectionArea.Z, SelectionArea.W),
				FVector2D(SelectionArea.Z, SelectionArea.Y) }));

			if (SelectionMode == ELidarPointCloudSelectionMode::Subtract)
			{
				Editor->DeselectPointsByConvexVolume(ConvexVolume);
			}
			else
			{
				Editor->SelectPointsByConvexVolume(ConvexVolume, SelectionMode == ELidarPointCloudSelectionMode::Add);
			}
		}
	}
}

void FLidarPointCloudEditorViewportClient::OnPolygonalSelectionEnd()
{
	// Skip invalid selections
	if (SelectionPoints.Num() < 3)
	{
		return;
	}

	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
		{
			TArray<TArray<FVector2D>> ConvexShapes;
			TArray<FVector2D> VectorPoints = ToVectorArray(SelectionPoints);

			if (IsPolygonConvex(SelectionPoints))
			{
				ConvexShapes.Add(VectorPoints);
			}
			else
			{
				// Check for self-intersecting shape
				if (!IsPolygonSelfIntersecting(VectorPoints, true))
				{
					// The separation needs points in CCW order
					if (!FGeomTools2D::IsPolygonWindingCCW(VectorPoints))
					{
						Algo::Reverse(VectorPoints);
					}

					TArray<FVector2D> Triangles;
					FGeomTools2D::TriangulatePoly(Triangles, VectorPoints, false);
					FGeomTools2D::GenerateConvexPolygonsFromTriangles(ConvexShapes, Triangles);
				}
			}

			for (int32 i = 0; i < ConvexShapes.Num(); ++i)
			{
				const FConvexVolume ConvexVolume = BuildConvexVolumeForPoints(ConvexShapes[i]);

				if (SelectionMode == ELidarPointCloudSelectionMode::Subtract)
				{
					Editor->DeselectPointsByConvexVolume(ConvexVolume);
				}
				else
				{
					// Consecutive shapes need to be additive
					Editor->SelectPointsByConvexVolume(ConvexVolume, i > 0 || SelectionMode == ELidarPointCloudSelectionMode::Add);
				}
			}
		}
	}
}

void FLidarPointCloudEditorViewportClient::OnLassoSelectionEnd()
{
	OnPolygonalSelectionEnd();
}

void FLidarPointCloudEditorViewportClient::OnPaintSelection()
{
	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		if (bLineTraceHit)
		{
			if (SelectionMode == ELidarPointCloudSelectionMode::Subtract)
			{
				Editor->DeselectPointsBySphere(FSphere(LineTraceHitPoint, PaintingRadius));
			}
			else
			{
				Editor->SelectPointsBySphere(FSphere(LineTraceHitPoint, PaintingRadius));
			}
		}
	}
}

void FLidarPointCloudEditorViewportClient::DrawSelectionBox(FCanvas& Canvas)
{
	if (SelectionPoints.Num() == 0)
	{
		return;
	}

	const FIntPoint SelectionStartLocation = SelectionPoints[0];
	FIntPoint SelectionCurrentLocation;
	Viewport->GetMousePos(SelectionCurrentLocation);

	const float InvScale = 1.0f / Viewport->GetClient()->GetDPIScale();
	
	const float X = FMath::Min(SelectionStartLocation.X, SelectionCurrentLocation.X) * InvScale;
	const float Y = FMath::Min(SelectionStartLocation.Y, SelectionCurrentLocation.Y) * InvScale;
	const float SizeX = FMath::Max(SelectionStartLocation.X, SelectionCurrentLocation.X) * InvScale - X;
	const float SizeY = FMath::Max(SelectionStartLocation.Y, SelectionCurrentLocation.Y) * InvScale - Y;

	FLinearColor SelectionColor = GetDefault<UEditorStyleSettings>()->SelectionColor;
	SelectionColor.A = 0.35f;

	Canvas.DrawTile(X, Y, SizeX, SizeY, 0, 0, 0, 0, SelectionColor);

	// Selection Border
	{
		FCanvasLineItem Line;
		Line.SetColor(GetDefault<UEditorStyleSettings>()->SelectionColor);
		Line.LineThickness = 2;

		Line.Origin = FVector(X, Y, 0);
		Line.EndPos = FVector(X + SizeX, Y, 0);
		Canvas.DrawItem(Line);

		Line.Origin = Line.EndPos;
		Line.EndPos = FVector(X + SizeX, Y + SizeY, 0);
		Canvas.DrawItem(Line);

		Line.Origin = Line.EndPos;
		Line.EndPos = FVector(X, Y + SizeY, 0);
		Canvas.DrawItem(Line);

		Line.Origin = Line.EndPos;
		Line.EndPos = FVector(X, Y, 0);
		Canvas.DrawItem(Line);
	}
}

void FLidarPointCloudEditorViewportClient::DrawSelectionPolygonal(FCanvas& Canvas)
{
	using UE::Lidar::Private::Editor::PolySnapDistanceSq;

	if (SelectionPoints.Num() == 0)
	{
		return;
	}

	// Create a local copy of selection points, injecting the current mouse position at the end
	TArray<FIntPoint> DrawSelectionPoints = SelectionPoints;
	Viewport->GetMousePos(DrawSelectionPoints[DrawSelectionPoints.AddUninitialized()]);

	if (DrawSelectionPoints.Last() == DrawSelectionPoints.Last(1))
	{
		DrawSelectionPoints.RemoveAt(DrawSelectionPoints.Num() - 1, 1, false);
	}

	TArray<FVector2D> VectorPoints = ToVectorArray(DrawSelectionPoints);

	// Account for DPI
	const float InvScale = 1.0f / Viewport->GetClient()->GetDPIScale();
	for(FVector2D& DrawPoint : VectorPoints)
	{
		DrawPoint *= InvScale;
	}

	// Calculate visual indication of complete polygon for the user
	const bool bPolyComplete = DrawSelectionPoints.Num() > 2 && (DrawSelectionPoints.Last() - DrawSelectionPoints[0]).SizeSquared() < PolySnapDistanceSq;
	const bool bSelfIntersecting = DrawSelectionPoints.Num() > 2 && IsPolygonSelfIntersecting(VectorPoints, true);
	FLinearColor SelectionColor = bSelfIntersecting ? FLinearColor::Red : bPolyComplete ? FLinearColor::Green : GetDefault<UEditorStyleSettings>()->SelectionColor;

	// Selection Area
	if (VectorPoints.Num() > 2 && !bSelfIntersecting)
	{
		FCanvasUVTri Tri;
		Tri.V0_Pos = Tri.V1_Pos = Tri.V2_Pos = Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
		SelectionColor.A = 0.35f;
		Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = SelectionColor;

		TArray<FCanvasUVTri> TriangleList;
		TArray<TArray<FVector2D>> Polygons;

		if (IsPolygonConvex(DrawSelectionPoints))
		{
			Polygons.Add(VectorPoints);
			TriangleList.Reserve(VectorPoints.Num() - 2);
		}
		else
		{
			// The separation needs points in CCW order
			if (!FGeomTools2D::IsPolygonWindingCCW(VectorPoints))
			{
				Algo::Reverse(VectorPoints);
			}

			TArray<FVector2D> Triangles;
			if (FGeomTools2D::TriangulatePoly(Triangles, VectorPoints, false))
			{
				FGeomTools2D::GenerateConvexPolygonsFromTriangles(Polygons, Triangles);

				// Calculate the number of triangles and reserve space
				int32 NumTriangles = 0;
				for (const TArray<FVector2D>& Polygon : Polygons)
				{
					NumTriangles += Polygon.Num() - 2;
				}
				TriangleList.Reserve(NumTriangles);
			}
		}

		for (const TArray<FVector2D>& Polygon : Polygons)
		{
			for (int32 i = 2; i < Polygon.Num(); ++i)
			{
				Tri.V0_Pos = Polygon[i];
				Tri.V1_Pos = Polygon[0];
				Tri.V2_Pos = Polygon[i - 1];
				TriangleList.Add(Tri);
			}
		}

		if (TriangleList.Num() > 0)
		{
			FCanvasTriangleItem Selection(TriangleList, GWhiteTexture);
			Selection.BlendMode = SE_BLEND_AlphaBlend;
			Canvas.DrawItem(Selection);
		}
	}

	// Selection Border
	{
		FCanvasLineItem Line;
		Line.SetColor(SelectionColor);
		Line.LineThickness = 2;

		for (int32 i = 1; i < VectorPoints.Num(); ++i)
		{
			Line.Origin = FVector(VectorPoints[i], 0);
			Line.EndPos = FVector(VectorPoints[i - 1], 0);
			Canvas.DrawItem(Line);
		}
	}
}

void FLidarPointCloudEditorViewportClient::DrawSelectionLasso(FCanvas& Canvas)
{
	DrawSelectionPolygonal(Canvas);
}

void FLidarPointCloudEditorViewportClient::DrawSelectionPaint(FCanvas& Canvas)
{
	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		if (TSharedPtr<SLidarPointCloudEditorViewport> EditorViewport = PointCloudEditorViewportPtr.Pin())
		{
			EditorViewport->PaintBrush->SetVisibility(bLineTraceHit);
			EditorViewport->PaintBrush->SetWorldScale3D(FVector(PaintingRadius * 0.02f));
			EditorViewport->PaintBrush->SetWorldLocation(LineTraceHitPoint);
		}
	}
}

FConvexVolume FLidarPointCloudEditorViewportClient::BuildConvexVolumeForPoints(const TArray<FVector2D>& Points)
{
	FConvexVolume ConvexVolume;

	if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
	{
		FSceneView* View = GetView();
		const FVector LocationOffset = Editor->GetPointCloudBeingEdited()->LocationOffset.ToVector();
		const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();

		TArray<FVector> Origins; Origins.AddUninitialized(Points.Num() + 2);
		TArray<FVector> Normals; Normals.AddUninitialized(Points.Num() + 2);
		TArray<FVector> Directions; Directions.AddUninitialized(Points.Num());
		FVector MeanCenter = FVector::ZeroVector;

		for (int32 i = 0; i < Points.Num(); ++i)
		{
			FSceneView::DeprojectScreenToWorld(Points[i], FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[i], Directions[i]);
			MeanCenter += Origins[i];
		}

		MeanCenter /= Points.Num();

		const FVector& ViewDirection = View->GetViewDirection();

		// Shared calculations
		Normals.Last(1) = ViewDirection;
		Normals.Last() = -ViewDirection;
		Origins.Last(1) = Origins[0] + ViewDirection * 99999999.0f;

		// Calculate plane normals
		bool bFlipNormals = false;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			Normals[i] = ((Origins[(i + 1) % Points.Num()] - Origins[i]).GetSafeNormal() ^ Directions[i]).GetSafeNormal();

			if (i == 0)
			{
				bFlipNormals = FVector::DotProduct(Normals[i], (MeanCenter - Origins[i])) > 0;
			}

			if (bFlipNormals)
			{
				Normals[i] = -Normals[i];
			}
		}

		// Perspective View
		if (View->IsPerspectiveProjection())
		{
			Origins.Last() = Origins[0];
		}
		// Ortho Views
		else
		{
			// Adjust for the LocationOffset
			for (int32 i = 0; i < Points.Num(); ++i)
			{
				Origins[i] -= LocationOffset;
			}

			Origins.Last() = -Origins.Last(1);
		}

		for (int32 i = 0; i < Origins.Num(); ++i)
		{
			ConvexVolume.Planes.Emplace(Origins[i], Normals[i]);
		}

		ConvexVolume.Init();
	}

	return ConvexVolume;
}

void FLidarPointCloudEditorViewportClient::ResetCamera()
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		FBox FocusBounds(EForceInit::ForceInit);

		// Focus on selection, if possible
		if (TSharedPtr<FLidarPointCloudEditor> Editor = PointCloudEditorPtr.Pin())
		{
			for (FLidarPointCloudPoint** Point = Editor->GetSelectedPoints().GetData(), **DataEnd = Point + Editor->GetSelectedPoints().Num(); Point != DataEnd; ++Point)
			{
				FocusBounds += (*Point)->Location;
			}
		}

		// Fallback to the whole cloud's bounds
		if(!FocusBounds.IsValid)
		{
			FocusBounds = PointCloudComponentRawPtr->Bounds.GetBox();
		}

		FocusViewportOnBox(FocusBounds);
		Invalidate();
	}
}

void FLidarPointCloudEditorViewportClient::ToggleShowNodes()
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		PointCloudComponentRawPtr->bDrawNodeBounds = !PointCloudComponentRawPtr->bDrawNodeBounds;
		Invalidate();
	}
}

bool FLidarPointCloudEditorViewportClient::IsSetShowNodesChecked() const
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		return PointCloudComponentRawPtr->bDrawNodeBounds;
	}
	
	return false;
}

void FLidarPointCloudEditorViewportClient::SetSelectionMethod(ELidarPointCloudSelectionMethod NewSelectionMethod)
{
	SelectionMethod = NewSelectionMethod;

	if (TSharedPtr<SLidarPointCloudEditorViewport> EditorViewport = PointCloudEditorViewportPtr.Pin())
	{
		EditorViewport->PaintBrush->SetVisibility(SelectionMethod == ELidarPointCloudSelectionMethod::Paint);
	}
}

#undef LOCTEXT_NAMESPACE 
