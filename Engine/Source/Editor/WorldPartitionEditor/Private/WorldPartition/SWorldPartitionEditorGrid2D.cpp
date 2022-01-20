// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/SWorldPartitionEditorGrid2D.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Selection.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionEditorCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "Editor/GroupActor.h"
#include "Editor/EditorEngine.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "WorldBrowserModule.h"
#include "LevelEditorViewport.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

class FWorldPartitionActorDescViewBoundsProxy : public FWorldPartitionActorDescView
{
public:
	FWorldPartitionActorDescViewBoundsProxy(const FWorldPartitionActorDesc* InActorDesc)
	: FWorldPartitionActorDescView(InActorDesc)
	{}

	FBox GetBounds() const
	{
		if (AActor* Actor = ActorDesc->GetActor())
		{
			return Actor->GetStreamingBounds();
		}

		return ActorDesc->GetBounds();
	}
};

SWorldPartitionEditorGrid2D::FEditorCommands::FEditorCommands()
	: TCommands<FEditorCommands>
(
	"WorldPartitionEditor",
	NSLOCTEXT("Contexts", "WorldPartition", "World Partition"),
	NAME_None,
	FEditorStyle::GetStyleSetName()
)
{}

void SWorldPartitionEditorGrid2D::FEditorCommands::RegisterCommands()
{
	UI_COMMAND(LoadSelectedCells, "Load Selected Cells", "Load the selected cells.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnloadSelectedCells, "Unload Selected Cells", "Unload the selected cells.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnloadAllCells, "Unload All Cells", "Unload all cells.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(MoveCameraHere, "Move Camera Here", "Move the camera to the selected position.", EUserInterfaceActionType::Button, FInputChord());
}

SWorldPartitionEditorGrid2D::SWorldPartitionEditorGrid2D()
	: CommandList(MakeShareable(new FUICommandList))
	, ChildSlot(this)
	, Scale(0.001f)
	, Trans(ForceInit)
	, ScreenRect(ForceInit)
	, bIsSelecting(false)
	, bIsDragging(false)
	, bShowActors(false)
	, SelectBox(ForceInit)
{
	FEditorCommands::Register();
	
	FWorldBrowserModule& WorldBrowserModule = FModuleManager::LoadModuleChecked<FWorldBrowserModule>("WorldBrowser");
	WorldBrowserModule.OnShutdown().AddLambda([](){ FEditorCommands::Unregister(); });
}

SWorldPartitionEditorGrid2D::~SWorldPartitionEditorGrid2D()
{
}

void SWorldPartitionEditorGrid2D::Construct(const FArguments& InArgs)
{
	SWorldPartitionEditorGrid::Construct(SWorldPartitionEditorGrid::FArguments().InWorld(InArgs._InWorld));

	// Defaults
	Trans = FVector2D(0, 0);
	Scale = 0.00133333332;

	// UI
	ChildSlot
	[
		SNew(SOverlay)

		// Top status bar
		+SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
			[
				SNew(SVerticalBox)

				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked(bShowActors ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.IsEnabled(true)
						.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([=](ECheckBoxState State) { bShowActors = !bShowActors; }))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.IsEnabled(true)
						.Text(LOCTEXT("ShowActors", "Show Actors"))
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked(GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetBugItGoLoadCells() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.IsEnabled(true)
						.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([=](ECheckBoxState State) { GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetBugItGoLoadCells(State == ECheckBoxState::Checked); }))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.IsEnabled(true)
						.Text(LOCTEXT("BugItGoLoadCells", "BugItGo Load Cells"))
					]
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked(GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetShowCellCoords() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.IsEnabled(true)
						.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([=](ECheckBoxState State) { GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetShowCellCoords(State == ECheckBoxState::Checked); }))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.IsEnabled(true)
						.Text(LOCTEXT("ShowCellCoords", "Show Cell Coords"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("FocusSelection", "Focus Selection"))
						.OnClicked(this, &SWorldPartitionEditorGrid2D::FocusSelection)
					]
				]
			]
		]
	];

	SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);

	// Bind commands
	const FEditorCommands& Commands = FEditorCommands::Get();
	FUICommandList& ActionList = *CommandList;

	auto CanLoadOrUnloadCells = [this]()
	{
		return WorldPartition && SelectBox.IsValid && (WorldPartition->EditorHash->ForEachIntersectingCell(SelectBox, [&](UWorldPartitionEditorCell*){}) > 0);
	};

	ActionList.MapAction(Commands.LoadSelectedCells, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::LoadSelectedCells), FCanExecuteAction::CreateLambda(CanLoadOrUnloadCells));
	ActionList.MapAction(Commands.UnloadSelectedCells, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::UnloadSelectedCells), FCanExecuteAction::CreateLambda(CanLoadOrUnloadCells));
	ActionList.MapAction(Commands.UnloadAllCells, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::UnloadAllCells));
	ActionList.MapAction(Commands.MoveCameraHere, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::MoveCameraHere));
}

void SWorldPartitionEditorGrid2D::LoadSelectedCells()
{
	WorldPartition->LoadEditorCells(SelectBox, true);
	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::UnloadSelectedCells()
{
	WorldPartition->UnloadEditorCells(SelectBox, true);
	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::UnloadAllCells()
{
	const FBox AllCellsBox(FVector(-WORLDPARTITION_MAX, -WORLDPARTITION_MAX, -WORLDPARTITION_MAX), FVector(WORLDPARTITION_MAX, WORLDPARTITION_MAX, WORLDPARTITION_MAX));
	WorldPartition->UnloadEditorCells(AllCellsBox, true);
	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::MoveCameraHere()
{
	for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
	{
		const FVector WorldLocation = FVector(MouseCursorPosWorld, LevelVC->GetViewLocation().Z);
		LevelVC->SetViewLocation(WorldLocation);
		LevelVC->Invalidate();
		FEditorDelegates::OnEditorCameraMoved.Broadcast(WorldLocation, LevelVC->GetViewRotation(), LevelVC->ViewportType, LevelVC->ViewIndex);
	}
}

FReply SWorldPartitionEditorGrid2D::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bIsLeftMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bIsRightMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
	const bool bIsMiddleMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton;
	const bool bIsRightMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton);
	const bool bIsLeftMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton);
	const bool bIsMiddleMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton);

	if (bIsLeftMouseButtonEffecting || bIsRightMouseButtonEffecting)
	{
		FReply ReplyState = FReply::Handled();
		ReplyState.CaptureMouse(SharedThis(this));

		if (bIsLeftMouseButtonEffecting)
		{
			bIsSelecting = true;
			SelectionStart = MouseCursorPosWorld;
			SelectionEnd = SelectionStart;
			UpdateSelection();
		}

		return ReplyState;
	}

	return FReply::Unhandled();
}

FReply SWorldPartitionEditorGrid2D::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bIsLeftMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
	const bool bIsRightMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
	const bool bIsMiddleMouseButtonEffecting = MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton;
	const bool bIsRightMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton);
	const bool bIsLeftMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton);
	const bool bIsMiddleMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton);

	if (bIsLeftMouseButtonEffecting || bIsRightMouseButtonEffecting)
	{
		FReply ReplyState = FReply::Handled();

		if ((!bIsSelecting && !bIsDragging) && bIsRightMouseButtonEffecting)
		{
			FMenuBuilder MenuBuilder(true, CommandList);

			const FEditorCommands& Commands = FEditorCommands::Get();

			MenuBuilder.BeginSection(NAME_None, LOCTEXT("WorldPartition", "Selected Actors"));
			{
				MenuBuilder.AddMenuEntry(Commands.LoadSelectedCells);
				MenuBuilder.AddMenuEntry(Commands.UnloadSelectedCells);
				MenuBuilder.AddMenuEntry(Commands.UnloadAllCells);
				MenuBuilder.AddMenuEntry(Commands.MoveCameraHere);
			}
			MenuBuilder.EndSection();

			FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}

		const bool HasMouseCapture = bIsSelecting || bIsDragging;

		if (bIsLeftMouseButtonEffecting)
		{
			bIsSelecting = false;
		}

		if (bIsRightMouseButtonEffecting)
		{
			bIsDragging = false;
		}

		if (HasMouseCapture && !bIsSelecting && !bIsDragging)
		{
			ReplyState.ReleaseMouseCapture();
		}

		return ReplyState;
	}

	return FReply::Unhandled();
}

FReply SWorldPartitionEditorGrid2D::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	MoveCameraHere();
	return FReply::Handled();
}

FReply SWorldPartitionEditorGrid2D::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D CursorDelta = MouseEvent.GetCursorDelta();

	MouseCursorPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MouseCursorPosWorld = ScreenToWorld.TransformPoint(MouseCursorPos);

	if (HasMouseCapture())
	{
		const bool bIsRightMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton);
		const bool bIsLeftMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton);
		const bool bIsMiddleMouseButtonDown = MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton);

		if (bIsLeftMouseButtonDown)
		{
			if (bIsSelecting)
			{
				SelectionEnd = MouseCursorPosWorld;
				UpdateSelection();
			}
		}

		if (bIsDragging || (bIsRightMouseButtonDown && (CursorDelta.Size() > 1.0f)))
		{
			if (!bIsDragging)
			{
				LastMouseCursorPosWorldDrag = MouseCursorPosWorld;
				bIsDragging = true;
			}

			Trans += (MouseCursorPosWorld - LastMouseCursorPosWorldDrag);

			UpdateTransform();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SWorldPartitionEditorGrid2D::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D MousePosLocalSpace = MouseCursorPos - MyGeometry.GetLocalSize() * 0.5f;
	FVector2D P0 = MousePosLocalSpace / Scale;
	float Delta = 1.0f + FMath::Abs(MouseEvent.GetWheelDelta() / 4.0f);
	Scale = FMath::Clamp(Scale * (MouseEvent.GetWheelDelta() > 0 ? Delta : (1.0f / Delta)), 0.0001f, 10.0f);
	FVector2D P1 = MousePosLocalSpace / Scale;
	Trans += (P1 - P0);
	UpdateTransform();
	return FReply::Handled();
}

FCursorReply SWorldPartitionEditorGrid2D::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bIsDragging ? EMouseCursor::None : EMouseCursor::Default);
}

int32 SWorldPartitionEditorGrid2D::PaintGrid(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	// Draw grid lines
	TArray<FVector2D> LinePoints;
	LinePoints.SetNum(2);

	FVector2D ScreenWorldOrigin = WorldToScreen.TransformPoint(FVector2D(0, 0));
	
	// World Y-axis
	if (ScreenWorldOrigin.X > ScreenRect.Min.X && ScreenWorldOrigin.X < ScreenRect.Max.X)
	{
		LinePoints[0] = FVector2D(ScreenWorldOrigin.X, ScreenRect.Min.Y);
		LinePoints[1] = FVector2D(ScreenWorldOrigin.X, ScreenRect.Max.Y);

		FLinearColor YAxisColor = FLinearColor::Green;
		YAxisColor.A = 0.4f;
		
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, YAxisColor, true, 2.0f);
	}

	// World X-axis
	if (ScreenWorldOrigin.Y > ScreenRect.Min.Y && ScreenWorldOrigin.Y < ScreenRect.Max.Y)
	{
		LinePoints[0] = FVector2D(ScreenRect.Min.X, ScreenWorldOrigin.Y);
		LinePoints[1] = FVector2D(ScreenRect.Max.X, ScreenWorldOrigin.Y);

		FLinearColor XAxisColor = FLinearColor::Red;
		XAxisColor.A = 0.4f;
		
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, XAxisColor, true, 2.0f);
	}

	return LayerId + 1;
}

uint32 SWorldPartitionEditorGrid2D::PaintActors(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const
{
	const FBox2D ViewRect(FVector2D(ForceInitToZero), AllottedGeometry.GetLocalSize());
	const FBox2D WorldViewRect(ScreenToWorld.TransformPoint(ViewRect.Min), ScreenToWorld.TransformPoint(ViewRect.Max));
	const FBox ViewRectWorld(FVector(WorldViewRect.Min.X, WorldViewRect.Min.Y, -WORLDPARTITION_MAX), FVector(WorldViewRect.Max.X, WorldViewRect.Max.Y, WORLDPARTITION_MAX));

	TSet<FWorldPartitionActorDescViewBoundsProxy> ActorDescList;

	// Include all actors if requested
	if (bShowActors)
	{
		WorldPartition->EditorHash->ForEachIntersectingActor(ViewRectWorld, [&](FWorldPartitionActorDesc* ActorDesc)
		{
			ActorDescList.Emplace(ActorDesc);
		});
	}

	// Include selected actors
	FWorldPartitionActorDesc::GlobalTag++;
	for (FSelectionIterator It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(Actor->GetActorGuid()))
			{
				ActorDescList.Emplace(ActorDesc);
				ActorDesc->Tag = FWorldPartitionActorDesc::GlobalTag;
			}
		}
	}

	if (ActorDescList.Num())
	{
		const FLinearColor LoadedActorColor(0.75f, 0.75f, 0.75f, 1.0f);
		const FLinearColor UnloadedActorColor(0.5f, 0.5f, 0.5f, 1.0f);	
		const FLinearColor SelectedActorColor(1.0f, 1.0f, 1.0f, 1.0f);

		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(5);

		for (const FWorldPartitionActorDescViewBoundsProxy& ActorDescView: ActorDescList)
		{
			const FBox ActorBounds = ActorDescView.GetBounds();
			FVector Origin, Extent;
			ActorBounds.GetCenterAndExtents(Origin, Extent);

			FVector2D TopLeftW = FVector2D(Origin - Extent);
			FVector2D BottomRightW = FVector2D(Origin + Extent);
			FVector2D TopRightW = FVector2D(BottomRightW.X, TopLeftW.Y);
			FVector2D BottomLeftW = FVector2D(TopLeftW.X, BottomRightW.Y);

			FVector2D TopLeft = WorldToScreen.TransformPoint(TopLeftW);
			FVector2D BottomRight = WorldToScreen.TransformPoint(BottomRightW);
			FVector2D TopRight = WorldToScreen.TransformPoint(TopRightW);
			FVector2D BottomLeft = WorldToScreen.TransformPoint(BottomLeftW);

			FBox2D ActorViewBox(TopLeft, BottomRight);

			const float MinimumAreaCull = 32.0f;
			const float AreaFadeDistance = 128.0f;
			if (ActorViewBox.Intersect(ViewRect) && ((Extent.Size2D() < KINDA_SMALL_NUMBER) || ActorViewBox.GetArea() > MinimumAreaCull))
			{
				FPaintGeometry ActorGeometry = AllottedGeometry.ToPaintGeometry(TopLeft, BottomRight - TopLeft);
				float ActorColorGradient = FMath::Min((ActorViewBox.GetArea() - MinimumAreaCull) / AreaFadeDistance, 1.0f);
				float ActorBrightness = ActorDescView.GetIsSpatiallyLoaded() ? 1.0f : 0.3f;
				FLinearColor ActorColor(ActorBrightness, ActorBrightness, ActorBrightness, ActorColorGradient);

				UClass* ActorClass = ActorDescView.GetActorClass();

				const float SquaredDistanceToPoint = ActorViewBox.ComputeSquaredDistanceToPoint(MouseCursorPos);
				if ((ActorDescView.GetTag() == FWorldPartitionActorDesc::GlobalTag) || (SquaredDistanceToPoint > 0.0f && SquaredDistanceToPoint <= 2.0f))
				{
					ActorColor = FLinearColor::Yellow;

					FName ActorLabel = ActorDescView.GetActorLabel();
					if (!ActorLabel.IsNone())
					{
						FSlateDrawElement::MakeText(
							OutDrawElements,
							++LayerId,
							AllottedGeometry.ToPaintGeometry(TopLeft, FVector2D(1,1)),
							ActorLabel.ToString(),
							SmallLayoutFont,
							ESlateDrawEffect::None,
							ActorColor
						);
					}
				}
				else if ((SelectBox.GetVolume() > 0) && SelectBox.Intersect(ActorDescView.GetBounds()))
				{
					ActorColor = FLinearColor::White;
				}

				FSlateDrawElement::MakeBox(
					OutDrawElements,
					++LayerId,
					ActorGeometry,
					FEditorStyle::GetBrush(TEXT("Border")),
					ESlateDrawEffect::None,
					ActorColor
				);
			}
		};
	}

	return LayerId + 1;
}

uint32 SWorldPartitionEditorGrid2D::PaintScaleRuler(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const
{
	const float	ScaleRulerLength = 100.f; // pixels
	TArray<FVector2D> LinePoints;
	LinePoints.Add(FVector2D::ZeroVector);
	LinePoints.Add(FVector2D::ZeroVector + FVector2D(ScaleRulerLength, 0.f));
	
	FSlateDrawElement::MakeLines( 
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2D(10, 40)),
		LinePoints,
		ESlateDrawEffect::None,
		FLinearColor::White);

	const float UnitsInRuler = ScaleRulerLength/Scale + 0.05f;// Pixels to world units (+0.05f to accommodate for %.2f)
	const int32 UnitsInMeter = 100;
	const int32 UnitsInKilometer = UnitsInMeter*1000;
	
	FString RulerText;
	if (UnitsInRuler >= UnitsInKilometer) // in kilometers
	{
		RulerText = FString::Printf(TEXT("%.2f km"), UnitsInRuler/UnitsInKilometer);
	}
	else // in meters
	{
		RulerText = FString::Printf(TEXT("%.2f m"), UnitsInRuler/UnitsInMeter);
	}
	
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2D(10, 27)),
		RulerText,
		FEditorStyle::GetFontStyle("NormalFont"),
		ESlateDrawEffect::None,
		FLinearColor::White);

	// Show world bounds
	const FBox WorldBounds = WorldPartition->GetEditorWorldBounds();
	const FVector WorldBoundsExtentInKM = (WorldBounds.GetExtent() * 2.0f) / 100000.0f;
	RulerText = FString::Printf(TEXT("%.2fx%.2fx%.2f km"), WorldBoundsExtentInKM.X, WorldBoundsExtentInKM.Y, WorldBoundsExtentInKM.Z);
	
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2D(10, 67)),
		RulerText,
		FEditorStyle::GetFontStyle("NormalFont"),
		ESlateDrawEffect::None,
		FLinearColor::White);

		
	return LayerId + 1;
}

uint32 SWorldPartitionEditorGrid2D::PaintViewer(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const
{
	FVector ObserverPosition;
	FRotator ObserverRotation;
	if (GetObserverView(ObserverPosition, ObserverRotation))
	{
		FVector2D LocalViewLocation = WorldToScreen.TransformPoint(FVector2D(ObserverPosition));
		const FSlateBrush* CameraImage = FEditorStyle::GetBrush(TEXT("WorldPartition.SimulationViewPosition"));
	
		FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry(
			LocalViewLocation - CameraImage->ImageSize * 0.5f, 
			CameraImage->ImageSize
		);

		FSlateDrawElement::MakeRotatedBox(
			OutDrawElements,
			++LayerId,
			PaintGeometry,
			CameraImage,
			ESlateDrawEffect::None,
			FMath::DegreesToRadians(ObserverRotation.Yaw),
			CameraImage->ImageSize * 0.5f,
			FSlateDrawElement::RelativeToElement
		);
	}

	FVector PlayerPosition;
	FRotator PlayerRotation;
	if (GetPlayerView(PlayerPosition, PlayerRotation))
	{
		FVector2D LocalViewLocation = WorldToScreen.TransformPoint(FVector2D(PlayerPosition));
		const FSlateBrush* CameraImage = FEditorStyle::GetBrush(TEXT("WorldPartition.SimulationViewPosition"));
	
		FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry(
			LocalViewLocation - CameraImage->ImageSize * 0.5f, 
			CameraImage->ImageSize
		);

		FSlateDrawElement::MakeRotatedBox(
			OutDrawElements,
			++LayerId,
			PaintGeometry,
			CameraImage,
			ESlateDrawEffect::None,
			FMath::DegreesToRadians(PlayerRotation.Yaw),
			CameraImage->ImageSize * 0.5f,
			FSlateDrawElement::RelativeToElement,
			FLinearColor(FColorList::Orange)
		);
	}

	return LayerId + 1;
}

uint32 SWorldPartitionEditorGrid2D::PaintSelection(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, uint32 LayerId) const
{
	if (bIsSelecting)
	{
		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(5);

		FVector2D TopLeftW = SelectionStart;
		FVector2D BottomRightW = SelectionEnd;
		FVector2D TopRightW = FVector2D(BottomRightW.X, TopLeftW.Y);
		FVector2D BottomLeftW = FVector2D(TopLeftW.X, BottomRightW.Y);

		FVector2D TopLeft = WorldToScreen.TransformPoint(TopLeftW);
		FVector2D BottomRight = WorldToScreen.TransformPoint(BottomRightW);
		FVector2D TopRight = WorldToScreen.TransformPoint(TopRightW);
		FVector2D BottomLeft = WorldToScreen.TransformPoint(BottomLeftW);

		LinePoints[0] = TopLeft;
		LinePoints[1] = TopRight;
		LinePoints[2] = BottomRight;
		LinePoints[3] = BottomLeft;
		LinePoints[4] = TopLeft;

		{
			FSlateColorBrush CellBrush(FLinearColor::White);
			FLinearColor CellColor(FLinearColor(1, 1, 1, 0.25f));

			FPaintGeometry CellGeometry = AllottedGeometry.ToPaintGeometry(
				TopLeft,
				BottomRight - TopLeft
			);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				CellGeometry,
				&CellBrush,
				ESlateDrawEffect::None,
				CellColor
			);
		}

		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), LinePoints, ESlateDrawEffect::None, FLinearColor::White, false, 1.0f);
	}

	return LayerId + 1;
}

int32 SWorldPartitionEditorGrid2D::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (WorldPartition)
	{
		const bool bResetView = !ScreenRect.bIsValid;

		ScreenRect = FBox2D(FVector2D(0, 0), AllottedGeometry.GetLocalSize());

		if (bResetView)
		{
			const FBox WorldBounds = WorldPartition->GetEditorWorldBounds();
			FocusBox(WorldBounds);
		}

		UpdateTransform();

		LayerId = PaintGrid(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		LayerId = PaintActors(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		LayerId = PaintScaleRuler(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		LayerId = PaintViewer(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		LayerId = PaintSelection(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		LayerId = PaintSoftwareCursor(AllottedGeometry, MyCullingRect, OutDrawElements, ++LayerId);
		
		// Draw a surrounding indicator when PIE is active
		if (UWorldPartition::IsSimulating() || !!GEditor->PlayWorld)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(),
				FEditorStyle::GetBrush(TEXT("Graph.PlayInEditor"))
			);
		}
	}

	return SWorldPartitionEditorGrid::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

int32 SWorldPartitionEditorGrid2D::PaintSoftwareCursor(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (bIsDragging)
	{
		const FSlateBrush* Brush = FEditorStyle::GetBrush(TEXT("SoftwareCursor_Grab"));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(MouseCursorPos - (Brush->ImageSize * 0.5f), Brush->ImageSize),
			Brush
		);
	}

	return LayerId + 1;
}

FReply SWorldPartitionEditorGrid2D::FocusSelection()
{
	FBox SelectionBox(ForceInit);

	USelection* SelectedActors = GEditor->GetSelectedActors();

	if (SelectedActors->Num())
	{
		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				SelectionBox += Actor->GetStreamingBounds();
			}
		}
	}
	else
	{
		SelectionBox = WorldPartition->GetEditorWorldBounds();
	}

	FocusBox(SelectionBox);
	return FReply::Handled();
}

void SWorldPartitionEditorGrid2D::FocusBox(const FBox& Box) const
{
	check(ScreenRect.bIsValid);

	const FBox2D Box2D(FVector2D(Box.Min), FVector2D(Box.Max));
	Trans = -FVector2D(Box2D.GetCenter());

	if (Box2D.GetArea() > 0.0f)
	{
		const FVector2D ScreenExtent = ScreenRect.GetExtent();
		const FVector2D SelectExtent = FVector2D(Box2D.GetExtent());
		Scale = (ScreenExtent / SelectExtent).GetMin() * 0.75f;
	}

	UpdateTransform();
}

void SWorldPartitionEditorGrid2D::UpdateTransform() const
{
	FTransform2D T(1.0f, Trans);
	FTransform2D V(Scale, FVector2D(ScreenRect.GetSize().X * 0.5f, ScreenRect.GetSize().Y * 0.5f));
	WorldToScreen = T.Concatenate(V);
	ScreenToWorld = WorldToScreen.Inverse();
}

void SWorldPartitionEditorGrid2D::UpdateSelection()
{
	SelectBox.Init();

	const FBox SelectionBox(
		FVector(FMath::Min(SelectionStart.X, SelectionEnd.X), FMath::Min(SelectionStart.Y, SelectionEnd.Y), -WORLDPARTITION_MAX),
		FVector(FMath::Max(SelectionStart.X, SelectionEnd.X), FMath::Max(SelectionStart.Y, SelectionEnd.Y), WORLDPARTITION_MAX)
	);

	SelectBox = SelectionBox;
}

#undef LOCTEXT_NAMESPACE
