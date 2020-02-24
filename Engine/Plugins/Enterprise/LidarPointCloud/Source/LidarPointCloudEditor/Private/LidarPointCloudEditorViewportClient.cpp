// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEditorViewportClient.h"
#include "LidarPointCloudEditor.h"
#include "LidarPointCloudEditorViewport.h"
#include "LidarPointCloud.h"
#include "LidarPointCloudShared.h"
#include "LidarPointCloudComponent.h"

#include "ConvexVolume.h"
#include "CanvasTypes.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Classes/EditorStyleSettings.h"
#include "AssetViewerSettings.h"

#define LOCTEXT_NAMESPACE "FLidarPointCloudEditorViewportClient"

namespace {
	static const float LightRotSpeed = 0.22f;
	static const float StaticMeshEditor_RotateSpeed = 0.01f;
	static const float	StaticMeshEditor_TranslateSpeed = 0.25f;
	static const float GridSize = 2048.0f;
	static const int32 CellSize = 16;
	static const float AutoViewportOrbitCameraTranslate = 256.0f;

	static float AmbientCubemapIntensity = 0.4f;
}

FLidarPointCloudEditorViewportClient::FLidarPointCloudEditorViewportClient(TWeakPtr<FLidarPointCloudEditor> InPointCloudEditor, const TSharedRef<SLidarPointCloudEditorViewport>& InPointCloudEditorViewport, FAdvancedPreviewScene* InPreviewScene, ULidarPointCloud* InPreviewPointCloud, ULidarPointCloudComponent* InPreviewPointCloudComponent)
	: FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InPointCloudEditorViewport))
	, PointCloudComponent(InPreviewPointCloudComponent)
	, PointCloudEditorPtr(InPointCloudEditor)
	, PointCloudEditorViewportPtr(InPointCloudEditorViewport)
	, SelectionMode(ELidarPointCloudSelectionMode::None)
{
	// Setup defaults for the common draw helper.
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = true;
	DrawHelper.GridColorAxis = FColor(160, 160, 160);
	DrawHelper.GridColorMajor = FColor(144, 144, 144);
	DrawHelper.GridColorMinor = FColor(128, 128, 128);
	DrawHelper.PerspectiveGridSize = GridSize;
	DrawHelper.NumCells = DrawHelper.PerspectiveGridSize / (CellSize * 2);

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
	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FLidarPointCloudEditorViewportClient::MouseMove(FViewport* InViewport, int32 x, int32 y)
{
	FEditorViewportClient::MouseMove(InViewport, x, y);
}

bool FLidarPointCloudEditorViewportClient::InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool Gamepad)
{
	bool bHandled = FEditorViewportClient::InputKey(InViewport, ControllerId, Key, Event, AmountDepressed, false);

	// Handle viewport screenshot.
	bHandled |= InputTakeScreenshot(InViewport, Key, Event);

	bHandled |= AdvancedPreviewScene->HandleInputKey(InViewport, ControllerId, Key, Event, AmountDepressed, Gamepad);

	const bool bAltPressed = Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt);
	const bool bCtrlPressed = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
	const bool bShiftPressed = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);

	// Edit Mode
	if (PointCloudEditorPtr.Pin()->IsEditMode())
	{
		if (Event == IE_Pressed)
		{
			if (Key == EKeys::Delete)
			{
				if (bShiftPressed)
				{
					PointCloudEditorPtr.Pin()->DeletePoints();
				}
				else
				{
					PointCloudEditorPtr.Pin()->HidePoints();
				}

				bHandled = true;
			}
			else if (Key == EKeys::Escape)
			{
				PointCloudEditorPtr.Pin()->DeselectPoints();
				bHandled = true;
			}
		}

		// Store current selection mode
		ELidarPointCloudSelectionMode LastSelectionMode = SelectionMode;

		// Update selection status
		if (Key == EKeys::LeftMouseButton)
		{
			if (Viewport->KeyState(EKeys::LeftMouseButton))
			{
				if (bShiftPressed)
				{
					SelectionMode = ELidarPointCloudSelectionMode::Replace;
				}
				else if (bCtrlPressed)
				{
					SelectionMode = ELidarPointCloudSelectionMode::Add;
				}
				else if (bAltPressed)
				{
					SelectionMode = ELidarPointCloudSelectionMode::Subtract;
				}
				else
				{
					SelectionMode = ELidarPointCloudSelectionMode::None;
				}
			}
			else
			{
				SelectionMode = ELidarPointCloudSelectionMode::None;
			}

			bHandled = true;
		}

		// Check if selection mode has changed since the last frame
		if (SelectionMode != LastSelectionMode)
		{
			// Selection start
			if (SelectionMode != ELidarPointCloudSelectionMode::None)
			{
				// Mark the cursor location for selection start
				InViewport->GetMousePos(SelectionStartLocation);

				OnSelectionStart();
			}
			// Selection end
			else
			{
				FIntPoint SelectionEndLocation;

				// Mark the cursor location for selection end
				InViewport->GetMousePos(SelectionEndLocation);

				FIntVector4 SelectionArea;

				SelectionArea.X = FMath::Min(SelectionStartLocation.X, SelectionEndLocation.X);
				SelectionArea.Y = FMath::Min(SelectionStartLocation.Y, SelectionEndLocation.Y);
				SelectionArea.Z = FMath::Max(SelectionStartLocation.X, SelectionEndLocation.X);
				SelectionArea.W = FMath::Max(SelectionStartLocation.Y, SelectionEndLocation.Y);

				OnSelectionEnd(SelectionArea, LastSelectionMode);
			}
		}
	}
	// Navigation Mode
	else
	{

	}

	return bHandled;
}

bool FLidarPointCloudEditorViewportClient::InputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	bool bResult = true;

	if (!bDisableInput)
	{
		if (SelectionMode != ELidarPointCloudSelectionMode::None)
		{
			// Do not move the camera while in selection
		}
		else
		{
			bResult = AdvancedPreviewScene->HandleViewportInput(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			if (bResult)
			{
				Invalidate();
			}
			else
			{
				bResult = FEditorViewportClient::InputAxis(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
			}
		}
	}

	return bResult;
}

void FLidarPointCloudEditorViewportClient::ProcessClick(FSceneView& InView, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) { }

void FLidarPointCloudEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
}

void FLidarPointCloudEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	TSharedPtr<FLidarPointCloudEditor> PointCloudEditor = PointCloudEditorPtr.Pin();
	TSharedPtr<SLidarPointCloudEditorViewport> PointCloudEditorViewport = PointCloudEditorViewportPtr.Pin();
	if (!PointCloudEditor.IsValid() || !PointCloudEditorViewport.IsValid())
	{
		return;
	}

	TArray<SLidarPointCloudEditorViewport::FOverlayTextItem> TextItems;

	if (PointCloudEditorPtr.Pin()->IsEditMode())
	{
		// Text entries
		{
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("[ESCAPE] to de-select")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("[SHIFT] + Mouse Drag to set selection")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("[CTRL] + Mouse Drag to add selection")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("[ALT] + Mouse Drag to subtract selection")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("Press [DELETE] to hide selected")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("Press [SHIFT] + [DELETE] to delete points")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::FromString("")));
			TextItems.Add(SLidarPointCloudEditorViewport::FOverlayTextItem(FText::Format(FText::FromString("Selected Points:  {0}"), FText::AsNumber(PointCloudEditorPtr.Pin()->GetSelectedPoints().Num()))));
		}

		// Selection box
		if (SelectionMode != ELidarPointCloudSelectionMode::None)
		{
			FIntPoint SelectionCurrentLocation;
			Viewport->GetMousePos(SelectionCurrentLocation);

			float X = FMath::Min(SelectionStartLocation.X, SelectionCurrentLocation.X);
			float Y = FMath::Min(SelectionStartLocation.Y, SelectionCurrentLocation.Y);
			float SizeX = FMath::Max(SelectionStartLocation.X, SelectionCurrentLocation.X) - X;
			float SizeY = FMath::Max(SelectionStartLocation.Y, SelectionCurrentLocation.Y) - Y;

			FLinearColor SelectionColor = GetDefault<UEditorStyleSettings>()->SelectionColor;
			SelectionColor.A = 0.35f;

			Canvas.DrawTile(X, Y, SizeX, SizeY, 0, 0, 0, 0, SelectionColor);
		}
	}

	PointCloudEditorViewport->PopulateOverlayText(TextItems);
}

bool FLidarPointCloudEditorViewportClient::ShouldOrbitCamera() const
{
	if (GetDefault<ULevelEditorViewportSettings>()->bUseUE3OrbitControls)
	{
		// this editor orbits always if ue3 orbit controls are enabled
		return true;
	}

	return FEditorViewportClient::ShouldOrbitCamera();
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

void FLidarPointCloudEditorViewportClient::OnSelectionStart()
{
}

void FLidarPointCloudEditorViewportClient::OnSelectionEnd(const FIntVector4& SelectionArea, ELidarPointCloudSelectionMode Mode)
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
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
			
			const FMatrix InvViewProjectionMatrix = View->ViewMatrices.GetInvViewProjectionMatrix();

			/**
			 * Compute 4 rays from the corners of the selection area
			 *
			 * 0 -- 3
			 * |    |
			 * 1 -- 2
			 */
			FVector Origins[6];
			FVector Normals[6];
			FVector Directions[4];
			FSceneView::DeprojectScreenToWorld(FVector2D(SelectionArea.X, SelectionArea.Y), FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[0], Directions[0]);
			FSceneView::DeprojectScreenToWorld(FVector2D(SelectionArea.X, SelectionArea.W), FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[1], Directions[1]);
			FSceneView::DeprojectScreenToWorld(FVector2D(SelectionArea.Z, SelectionArea.W), FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[2], Directions[2]);
			FSceneView::DeprojectScreenToWorld(FVector2D(SelectionArea.Z, SelectionArea.Y), FIntRect(FIntPoint(0, 0), Viewport->GetSizeXY()), InvViewProjectionMatrix, Origins[3], Directions[3]);

			const FVector& ViewDirection = View->GetViewDirection();

			// Shared calculations
			Normals[4] = ViewDirection;
			Normals[5] = -ViewDirection;
			Origins[4] = Origins[0] + ViewDirection * 99999999;

			// Perspective View
			if (View->IsPerspectiveProjection())
			{
				Origins[5] = Origins[0];

				// Calculate plane normals
				Normals[0] = Directions[1] ^ Directions[0];
				Normals[1] = Directions[2] ^ Directions[1];
				Normals[2] = Directions[3] ^ Directions[2];
				Normals[3] = Directions[0] ^ Directions[3];

				Normals[0].Normalize();
				Normals[1].Normalize();
				Normals[2].Normalize();
				Normals[3].Normalize();
			}
			// Ortho Views
			else
			{
				// Adjust for the LocationOffset
				for (int32 i = 0; i < 5; ++i)
				{
					Origins[i] -= LocationOffset;
				}

				Origins[5] = -Origins[4];

				// Left / Right view
				if (ViewDirection.X != 0)
				{
					Normals[0] = -ViewDirection.X * FVector::RightVector;
					Normals[1] = -ViewDirection.X * FVector::UpVector;
					Normals[2] = ViewDirection.X * FVector::RightVector;
					Normals[3] = ViewDirection.X * FVector::UpVector;
				}
				// Front / Back view
				else if (ViewDirection.Y != 0)
				{
					Normals[0] = ViewDirection.Y * FVector::ForwardVector;
					Normals[1] = ViewDirection.Y * FVector::UpVector;
					Normals[2] = -ViewDirection.Y * FVector::ForwardVector;
					Normals[3] = -ViewDirection.Y * FVector::UpVector;
				}
				// Top / Bottom view
				else
				{
					Normals[0] = ViewDirection.Z * FVector::ForwardVector;
					Normals[1] = -ViewDirection.Z * FVector::RightVector;
					Normals[2] = -ViewDirection.Z * FVector::ForwardVector;
					Normals[3] = ViewDirection.Z * FVector::RightVector;
				}
			}

			// Compute selection frustum planes from the rays
			TArray<FPlane, TInlineAllocator<6>> Planes;

			// Left, Bottom, Right, Top, Forward, Backward
			for (int32 i = 0; i < 6; ++i)
			{
				Planes.Emplace(Origins[i], Normals[i]);
			}

			if (Mode == ELidarPointCloudSelectionMode::Subtract)
			{
				Editor->DeselectPointsByFrustum(FConvexVolume(Planes));
			}
			else
			{
				Editor->SelectPointsByFrustum(FConvexVolume(Planes), Mode == ELidarPointCloudSelectionMode::Add);
			}
		}
	}
}

void FLidarPointCloudEditorViewportClient::ResetCamera()
{
	if (ULidarPointCloudComponent* PointCloudComponentRawPtr = PointCloudComponent.Get())
	{
		FocusViewportOnBox(PointCloudComponentRawPtr->Bounds.GetBox());
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

#undef LOCTEXT_NAMESPACE 