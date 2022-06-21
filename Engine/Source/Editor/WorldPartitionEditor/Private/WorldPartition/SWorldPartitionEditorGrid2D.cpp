// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/SWorldPartitionEditorGrid2D.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/Selection.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Editor/GroupActor.h"
#include "Editor/EditorEngine.h"
#include "Fonts/FontMeasure.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Rendering/SlateRenderer.h"
#include "WorldBrowserModule.h"
#include "LevelEditorViewport.h"
#include "LocationVolume.h"
#include "Algo/Transform.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

static bool IsBoundsSelected(const FBox& SelectBox, const FBox& Bounds)
{
	return SelectBox.IsValid && Bounds.IntersectXY(SelectBox) && !Bounds.IsInsideXY(SelectBox);
}

static bool IsBoundsHovered(FVector2D Point, const FBox2D& Bounds)
{
	const float DistanceToPoint = FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Point));
	return DistanceToPoint > UE_KINDA_SMALL_NUMBER && DistanceToPoint < 10.0f;
}

static bool IsActorBoundsHovered(const FBox& ActorBounds, const FTransform2d& WorldToScreen, const FVector2D& MouseCursorPos)
{
	FVector Origin, Extent;
	ActorBounds.GetCenterAndExtents(Origin, Extent);

	const FVector2D TopLeftW = FVector2D(Origin - Extent);
	const FVector2D BottomRightW = FVector2D(Origin + Extent);
	const FVector2D TopLeft = WorldToScreen.TransformPoint(TopLeftW);
	const FVector2D BottomRight = WorldToScreen.TransformPoint(BottomRightW);
	const FBox2D ActorViewBox(TopLeft, BottomRight);

	return IsBoundsHovered(MouseCursorPos, ActorViewBox);
}

template <class T>
void ForEachHoveredLoaderAdapters(const UWorldPartition* WorldPartition, const FTransform2d& WorldToScreen, const FVector2D& MouseCursorPos, T Func)
{
	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
	{
		if (LoaderAdapter->GetBoundingBox().IsSet())
		{
			if (IsActorBoundsHovered(*LoaderAdapter->GetBoundingBox(), WorldToScreen, MouseCursorPos))
			{
				if (!Func(LoaderAdapter))
				{
					return;
				}
			}
		}
	}
}

template <class T>
void ForEachSelectedEditorLoaderAdapters(const UWorldPartition* WorldPartition, const FBox& SelectBox, T Func)
{
	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
	{
		if (LoaderAdapter->GetBoundingBox().IsSet())
		{
			if (IsBoundsSelected(SelectBox, *LoaderAdapter->GetBoundingBox()))
			{
				if (!Func(LoaderAdapter))
				{
					return;
				}
			}
		}
	}
};

template <class T>
void ForEachSelectedWorldLoaderAdapters(const UWorldPartition* WorldPartition, const FBox& SelectBox, T Func)
{
	WorldPartition->EditorHash->ForEachIntersectingActor(SelectBox, [&](FWorldPartitionActorDesc* ActorDesc)
	{
		if (AActor* Actor = ActorDesc->GetActor())
		{
			if (ActorDesc->GetActorNativeClass()->ImplementsInterface(UWorldPartitionActorLoaderInterface::StaticClass()))
			{
				if (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter = Cast<IWorldPartitionActorLoaderInterface>(Actor)->GetLoaderAdapter())
				{
					if (IsBoundsSelected(SelectBox, ActorDesc->GetBounds()))
					{
						if (!Func(LoaderAdapter))
						{
							return;
						}
					}
				}
			}
		}
	});
}

template <class T>
void ForEachSelectedLoaderAdapters(const UWorldPartition* WorldPartition, const FBox& SelectBox, T Func)
{
	ForEachSelectedEditorLoaderAdapters<T>(WorldPartition, SelectBox, Func);
	ForEachSelectedWorldLoaderAdapters<T>(WorldPartition, SelectBox, Func);
}

class FWorldPartitionActorDescViewBoundsProxy : public FWorldPartitionActorDescView
{
public:
	FWorldPartitionActorDescViewBoundsProxy(const FWorldPartitionActorDesc* InActorDesc)
		: FWorldPartitionActorDescView(InActorDesc)
	{}

	FBox GetBounds() const
	{
		if (AActor* Actor = GetActor())
		{
			return Actor->GetStreamingBounds();
		}

		return ActorDesc->GetBounds();
	}

	AActor* GetActor() const
	{
		return ActorDesc->GetActor(false);
	}
};

SWorldPartitionEditorGrid2D::FEditorCommands::FEditorCommands()
	: TCommands<FEditorCommands>
(
	"WorldPartitionEditor",
	NSLOCTEXT("Contexts", "WorldPartition", "World Partition"),
	NAME_None,
	FAppStyle::GetAppStyleSetName()
)
{}

void SWorldPartitionEditorGrid2D::FEditorCommands::RegisterCommands()
{
	UI_COMMAND(CreateRegionFromSelection, "Create Loading Region From Selection", "Create a loading region from the selection.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(LoadSelectedRegions, "Load Selected Regions", "Load the selected regions.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnloadSelectedRegions, "Unload Selected Regions", "Unload the selected regions.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(UnloadHoveredRegion, "Unload Hovered Region", "Unload the hovered region.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ConvertSelectedRegionsToActors, "Convert Selected Regions To Actors", "Convert the selected regions to actors.", EUserInterfaceActionType::Button, FInputChord());
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
{}

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
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.TitleBackground")))
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
						.IsChecked(GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetBugItGoLoadRegion() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
						.IsEnabled(true)
						.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([=](ECheckBoxState State) { GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->SetBugItGoLoadRegion(State == ECheckBoxState::Checked); }))
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.IsEnabled(true)
						.Text(LOCTEXT("BugItGoLoadRegion", "BugItGo Load Region"))
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

	auto CanCreateRegionFromSelection = [this]()
	{
		return !!SelectBox.IsValid;
	};

	auto CanLoadUnloadSelectedRegions = [this](bool bLoad)
	{
		bool bResult = false;
		ForEachSelectedLoaderAdapters(WorldPartition, SelectBox, [&bResult, bLoad](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
		{
			if (bLoad != LoaderAdapter->IsLoaded())
			{
				bResult = true;
				return false;
			}
			return true;
		});
		return bResult;
	};

	auto CanConvertSelectedRegionsToActors = [this]()
	{
		bool bResult = false;
		ForEachSelectedEditorLoaderAdapters(WorldPartition, SelectBox, [&bResult](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
		{
			bResult = true;
			return false;
		});
		return bResult;
	};

	auto CanLoadSelectedRegions = [this, CanLoadUnloadSelectedRegions]() { return CanLoadUnloadSelectedRegions(true); };
	auto CanUnloadSelectedRegions = [this, CanLoadUnloadSelectedRegions]() { return CanLoadUnloadSelectedRegions(false); };
	
	auto CanUnloadHoveredRegion = [this]()
	{
		bool bResult = false;
		ForEachHoveredLoaderAdapters(WorldPartition, WorldToScreen, MouseCursorPos, [&bResult](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
		{
			if (LoaderAdapter->IsLoaded())
			{
				bResult = true;
				return false;
			}
			return true;
		});
		return bResult;
	};

	ActionList.MapAction(Commands.CreateRegionFromSelection, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::CreateRegionFromSelection), FCanExecuteAction::CreateLambda(CanCreateRegionFromSelection));
	ActionList.MapAction(Commands.LoadSelectedRegions, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::LoadSelectedRegions), FCanExecuteAction::CreateLambda(CanLoadSelectedRegions));
	ActionList.MapAction(Commands.UnloadSelectedRegions, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::UnloadSelectedRegions), FCanExecuteAction::CreateLambda(CanUnloadSelectedRegions));
	ActionList.MapAction(Commands.UnloadHoveredRegion, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::UnloadHoveredRegion), FCanExecuteAction::CreateLambda(CanUnloadHoveredRegion));
	ActionList.MapAction(Commands.ConvertSelectedRegionsToActors, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::ConvertSelectedRegionsToActors), FCanExecuteAction::CreateLambda(CanConvertSelectedRegionsToActors));
	ActionList.MapAction(Commands.MoveCameraHere, FExecuteAction::CreateSP(this, &SWorldPartitionEditorGrid2D::MoveCameraHere));
}

void SWorldPartitionEditorGrid2D::CreateRegionFromSelection()
{
	const FBox RegionBox(FVector(SelectBox.Min.X, SelectBox.Min.Y, -HALF_WORLD_MAX), FVector(SelectBox.Max.X, SelectBox.Max.Y, HALF_WORLD_MAX));
	FLoaderAdapterShape* LoaderAdapter = WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, RegionBox, TEXT("Loaded Region"));
	LoaderAdapter->SetUserCreated(true);
	LoaderAdapter->Load();

	SelectBox.Init();

	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::LoadSelectedRegions()
{
	check(SelectBox.IsValid);

	ForEachSelectedLoaderAdapters(WorldPartition, SelectBox, [](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
	{
		LoaderAdapter->Load();
		return true;
	});

	SelectBox.Init();

	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::UnloadSelectedRegions()
{
	check(SelectBox.IsValid);

	TArray<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> LoaderAdaptersToRelease;
	const TSet<IWorldPartitionActorLoaderInterface::ILoaderAdapter*>& RegisteredLoaderAdapters = WorldPartition->GetRegisteredEditorLoaderAdapters();

	ForEachSelectedLoaderAdapters(WorldPartition, SelectBox, [&LoaderAdaptersToRelease, &RegisteredLoaderAdapters](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
	{
		LoaderAdapter->Unload();

		if (RegisteredLoaderAdapters.Contains(LoaderAdapter))
		{
			LoaderAdaptersToRelease.Add(LoaderAdapter);
		}
		return true;
	});

	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapterToRelease : LoaderAdaptersToRelease)
	{
		WorldPartition->ReleaseEditorLoaderAdapter(LoaderAdapterToRelease);
	}

	SelectBox.Init();

	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::UnloadHoveredRegion()
{
	TArray<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> LoaderAdaptersToRelease;
	ForEachHoveredLoaderAdapters(WorldPartition, WorldToScreen, MouseCursorPos, [&LoaderAdaptersToRelease](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
	{
		if (LoaderAdapter->IsLoaded())
		{
			LoaderAdaptersToRelease.Add(LoaderAdapter);
		}
		return true;
	});

	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapterToRelease : LoaderAdaptersToRelease)
	{
		WorldPartition->ReleaseEditorLoaderAdapter(LoaderAdapterToRelease);
	}

	GEditor->RedrawLevelEditingViewports();
	Refresh();
}

void SWorldPartitionEditorGrid2D::ConvertSelectedRegionsToActors()
{
	TArray<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> LoaderAdaptersToRelease;
	ForEachSelectedEditorLoaderAdapters(WorldPartition, SelectBox, [this, &LoaderAdaptersToRelease](IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
	{
		const FBox LoaderVolumeBox(*LoaderAdapter->GetBoundingBox());

		ALocationVolume* LocationVolume = World->SpawnActor<ALocationVolume>(LoaderVolumeBox.GetCenter(), FRotator::ZeroRotator);

		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = 1.0f;
		Builder->Y = 1.0f;
		Builder->Z = 1.0f;
		UActorFactory::CreateBrushForVolumeActor(LocationVolume, Builder);

		LocationVolume->GetRootComponent()->SetWorldScale3D(LoaderVolumeBox.GetSize());

		LocationVolume->GetLoaderAdapter()->Load();
		
		LoaderAdapter->Unload();
		LoaderAdaptersToRelease.Add(LoaderAdapter);

		return true;
	});

	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapterToRelease : LoaderAdaptersToRelease)
	{
		WorldPartition->ReleaseEditorLoaderAdapter(LoaderAdapterToRelease);
	}
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
			SelectBox.Init();
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

			MenuBuilder.BeginSection(NAME_None, LOCTEXT("WorldPartitionSelection", "Selection"));
				MenuBuilder.AddMenuEntry(Commands.CreateRegionFromSelection);
				MenuBuilder.AddMenuSeparator();
				MenuBuilder.AddMenuEntry(Commands.LoadSelectedRegions);
				MenuBuilder.AddMenuEntry(Commands.UnloadSelectedRegions);
				MenuBuilder.AddMenuEntry(Commands.UnloadHoveredRegion);
				MenuBuilder.AddMenuSeparator();
				MenuBuilder.AddMenuEntry(Commands.ConvertSelectedRegionsToActors);
			MenuBuilder.EndSection();

			MenuBuilder.BeginSection(NAME_None, LOCTEXT("WorldPartitionMisc", "Misc"));
				MenuBuilder.AddMenuEntry(Commands.MoveCameraHere);
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
	Scale = FMath::Clamp(Scale * (MouseEvent.GetWheelDelta() > 0 ? Delta : (1.0f / Delta)), 0.00000001f, 10.0f);
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
	const FBox ViewRectWorld(FVector(WorldViewRect.Min.X, WorldViewRect.Min.Y, -HALF_WORLD_MAX), FVector(WorldViewRect.Max.X, WorldViewRect.Max.Y, HALF_WORLD_MAX));

	TSet<FWorldPartitionActorDescViewBoundsProxy> ActorDescList;
	
	TArray<IWorldPartitionActorLoaderInterface::ILoaderAdapter*> AllLoaderAdapters;
	for (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter : WorldPartition->GetRegisteredEditorLoaderAdapters())
	{
		check(LoaderAdapter);
		AllLoaderAdapters.Add(LoaderAdapter);
	}

	WorldPartition->EditorHash->ForEachIntersectingActor(ViewRectWorld, [&](FWorldPartitionActorDesc* ActorDesc)
	{
		FWorldPartitionActorDescViewBoundsProxy ActorDescViewProxy(ActorDesc);

		if (bShowActors)
		{
			if (ActorDescViewProxy.GetIsSpatiallyLoaded())
			{
				ActorDescList.Emplace(ActorDescViewProxy);
			}
		}
		
		if (ActorDesc->GetActorNativeClass()->ImplementsInterface(UWorldPartitionActorLoaderInterface::StaticClass()))
		{
			if (AActor* Actor = ActorDescViewProxy.GetActor())
			{
				if (IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter = Cast<IWorldPartitionActorLoaderInterface>(Actor)->GetLoaderAdapter())
				{
					AllLoaderAdapters.Add(LoaderAdapter);
				}
			}
		}
	});

	// Include selected actors
	for (FSelectionIterator It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(Actor->GetActorGuid()))
			{
				ActorDescList.Emplace(ActorDesc);
			}
		}
	}

	auto DrawActorLabel = [&OutDrawElements, &LayerId, &AllottedGeometry](const FString& ActorLabel, const FBox2D& ActorViewBox, const FPaintGeometry& ActorGeometry, const FLinearColor& Color, const FSlateFontInfo& Font)
	{
		const FVector2D LabelTextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(ActorLabel, Font);

		if (LabelTextSize.X > 0)
		{
			const FVector2D ActorViewBoxCenter = ActorViewBox.GetCenter();
			const FVector2D LabelTextPos = ActorViewBoxCenter - LabelTextSize * 0.5f;
			const float LabelColorGradient = FMath::Clamp(ActorGeometry.GetLocalSize().X / LabelTextSize.X - 1.0f, 0.0f, 1.0f);

			if (LabelColorGradient > 0.0f)
			{
				const FLinearColor LabelColor(Color.R, Color.G, Color.B, Color.A * LabelColorGradient);

				FSlateDrawElement::MakeText(
					OutDrawElements,
					++LayerId,
					AllottedGeometry.ToPaintGeometry(LabelTextPos, FVector2D(1,1)),
					ActorLabel,
					Font,
					ESlateDrawEffect::None,
					LabelColor
				);
			}
		}
	};

	if (AllLoaderAdapters.Num())
	{
		const FLinearColor LoadedActorColor(0.75f, 0.75f, 0.75f, 1.0f);
		const FLinearColor UnloadedActorColor(0.5f, 0.5f, 0.5f, 1.0f);	

		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(5);

		for (const IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter : AllLoaderAdapters)
		{
			if (LoaderAdapter->GetBoundingBox().IsSet())
			{
				const FBox ActorBounds = *LoaderAdapter->GetBoundingBox();

				FVector Origin, Extent;
				ActorBounds.GetCenterAndExtents(Origin, Extent);

				const FVector2D TopLeftW = FVector2D(Origin - Extent);
				const FVector2D BottomRightW = FVector2D(Origin + Extent);
				const FVector2D TopRightW = FVector2D(BottomRightW.X, TopLeftW.Y);
				const FVector2D BottomLeftW = FVector2D(TopLeftW.X, BottomRightW.Y);

				const FVector2D TopLeft = WorldToScreen.TransformPoint(TopLeftW);
				const FVector2D BottomRight = WorldToScreen.TransformPoint(BottomRightW);
				const FVector2D TopRight = WorldToScreen.TransformPoint(TopRightW);
				const FVector2D BottomLeft = WorldToScreen.TransformPoint(BottomLeftW);

				const FBox2D ActorViewBox(TopLeft, BottomRight);

				const float FullScreenColorGradient = FMath::Min(ViewRect.GetArea() / ActorViewBox.GetArea(), 1.0f);

				if (FullScreenColorGradient > 0.0f)
				{
					const float MinimumAreaCull = 32.0f;
					const float AreaFadeDistance = 128.0f;
					if ((Extent.Size2D() < KINDA_SMALL_NUMBER) || (ActorViewBox.GetArea() > MinimumAreaCull))
					{
						const FPaintGeometry ActorGeometry = AllottedGeometry.ToPaintGeometry(TopLeft, BottomRight - TopLeft);
						const float LoaderColorGradient = FMath::Min((ActorViewBox.GetArea() - MinimumAreaCull) / AreaFadeDistance, 1.0f);

						// Highlight
						{
							const FSlateColorBrush LoadedBrush(FLinearColor::White);
							const FSlateColorBrush UnloadedBrush(FLinearColor::Black);
							const FLinearColor LoadedColor(1.0f, 1.0f, 1.0f, 0.25f * LoaderColorGradient * FullScreenColorGradient);
							const FLinearColor UnloadedColor(0.0f, 0.0f, 0.0f, 0.25f * LoaderColorGradient * FullScreenColorGradient);

							FSlateDrawElement::MakeBox(
								OutDrawElements,
								++LayerId,
								ActorGeometry,
								LoaderAdapter->IsLoaded() ? &LoadedBrush : &UnloadedBrush,
								ESlateDrawEffect::None,
								LoaderAdapter->IsLoaded() ? LoadedColor : UnloadedColor
							);
						}

						// Outline
						{
							const FLinearColor LoaderColor = LoaderAdapter->GetColor().IsSet() ? *LoaderAdapter->GetColor() : FColor::White;
							const FLinearColor OutlineColor(LoaderColor.R, LoaderColor.G, LoaderColor.B, LoaderColorGradient * FullScreenColorGradient);
							const bool IsHighlighted = IsBoundsSelected(SelectBox, ActorBounds) || IsBoundsHovered(MouseCursorPos, ActorViewBox);

							LinePoints[0] = TopLeft;
							LinePoints[1] = TopRight;
							LinePoints[2] = BottomRight;
							LinePoints[3] = BottomLeft;
							LinePoints[4] = TopLeft;

							FSlateDrawElement::MakeLines
							(
								OutDrawElements,
								++LayerId,
								AllottedGeometry.ToPaintGeometry(),
								LinePoints,
								ESlateDrawEffect::None,
								OutlineColor,
								true,
								IsHighlighted ? 4.0f : 2.0f
							);
						}

						// Label
						{
							const FString ActorLabel = *LoaderAdapter->GetLabel();
							const FLinearColor LabelColor(1.0f, 1.0f, 1.0f, LoaderColorGradient * FullScreenColorGradient);
							DrawActorLabel(ActorLabel, ActorViewBox, ActorGeometry, LabelColor, SmallLayoutFont);
						}
					}
				}
			}
		}
	}

	if (ActorDescList.Num())
	{
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
			if ((Extent.Size2D() < KINDA_SMALL_NUMBER) || (ActorViewBox.GetArea() > MinimumAreaCull))
			{
				const FPaintGeometry ActorGeometry = AllottedGeometry.ToPaintGeometry(TopLeft, BottomRight - TopLeft);

				const float ActorColorGradient = FMath::Min((ActorViewBox.GetArea() - MinimumAreaCull) / AreaFadeDistance, 1.0f);
				const float ActorBrightness = ActorDescView.GetIsSpatiallyLoaded() ? 1.0f : 0.3f;
				FLinearColor ActorColor(ActorBrightness, ActorBrightness, ActorBrightness, ActorColorGradient);

				UClass* ActorClass = ActorDescView.GetActorNativeClass();
				const AActor* Actor = ActorDescView.GetActor();

				const bool bIsSelected = Actor ? Actor->IsSelected() : false;
				if (bIsSelected || IsBoundsHovered(MouseCursorPos, ActorViewBox))
				{
					ActorColor = FLinearColor::Yellow;

					const FName ActorLabel = ActorDescView.GetActorLabel();
					if (!ActorLabel.IsNone())
					{
						DrawActorLabel(ActorLabel.ToString(), ActorViewBox, ActorGeometry, ActorColor, SmallLayoutFont);
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
					FAppStyle::GetBrush(TEXT("Border")),
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
		FAppStyle::GetFontStyle("NormalFont"),
		ESlateDrawEffect::None,
		FLinearColor::White);

	// Show world bounds
	const FBox WorldBounds = WorldPartition->GetRuntimeWorldBounds();
	const FVector WorldBoundsExtentInKM = (WorldBounds.GetExtent() * 2.0f) / 100000.0f;
	RulerText = FString::Printf(TEXT("%.2fx%.2fx%.2f km"), WorldBoundsExtentInKM.X, WorldBoundsExtentInKM.Y, WorldBoundsExtentInKM.Z);
	
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToOffsetPaintGeometry(FVector2D(10, 67)),
		RulerText,
		FAppStyle::GetFontStyle("NormalFont"),
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
		const FSlateBrush* CameraImage = FAppStyle::GetBrush(TEXT("WorldPartition.SimulationViewPosition"));
	
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
		const FSlateBrush* CameraImage = FAppStyle::GetBrush(TEXT("WorldPartition.SimulationViewPosition"));
	
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
	if (SelectBox.IsValid)
	{
		TArray<FVector2D> LinePoints;
		LinePoints.SetNum(5);

		FVector2D TopLeftW = FVector2D(SelectBox.Min);
		FVector2D BottomRightW = FVector2D(SelectBox.Max);
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

		FSlateDrawElement::MakeLines(
			OutDrawElements, 
			LayerId, 
			AllottedGeometry.ToPaintGeometry(), 
			LinePoints, 
			ESlateDrawEffect::None, 
			FLinearColor::White, 
			true, 
			2.0f
		);
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
			FocusBox(WorldPartition->GetRuntimeWorldBounds());
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
				FAppStyle::GetBrush(TEXT("Graph.PlayInEditor"))
			);
		}
	}

	return SWorldPartitionEditorGrid::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

int32 SWorldPartitionEditorGrid2D::PaintSoftwareCursor(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (bIsDragging)
	{
		const FSlateBrush* Brush = FAppStyle::GetBrush(TEXT("SoftwareCursor_Grab"));

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
	FTransform2d T(1.0f, Trans);
	FTransform2d V(Scale, FVector2D(ScreenRect.GetSize().X * 0.5f, ScreenRect.GetSize().Y * 0.5f));
	WorldToScreen = T.Concatenate(V);
	ScreenToWorld = WorldToScreen.Inverse();
}

void SWorldPartitionEditorGrid2D::UpdateSelection()
{
	const FBox2D SelectBox2D(
		FVector2D::Min(SelectionStart, SelectionEnd), 
		FVector2D::Max(SelectionStart, SelectionEnd)
	);

	if (SelectBox2D.GetArea() > 0.0f)
	{
		SelectBox = FBox(
			FVector(SelectBox2D.Min.X, SelectBox2D.Min.Y, -HALF_WORLD_MAX),
			FVector(SelectBox2D.Max.X, SelectBox2D.Max.Y, HALF_WORLD_MAX)
		);
	}
}

#undef LOCTEXT_NAMESPACE