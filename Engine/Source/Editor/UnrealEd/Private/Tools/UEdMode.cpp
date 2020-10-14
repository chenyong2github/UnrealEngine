// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/UEdMode.h"
#include "EditorModeTools.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "CanvasItem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "StaticMeshResources.h"
#include "Toolkits/BaseToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "CanvasTypes.h"
#include "ScopedTransaction.h"
#include "Tools/EditorToolAssetAPI.h"
#include "Editor.h"
#include "Toolkits/ToolkitManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "LevelEditorViewport.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolObjects.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Actor.h"

/** Hit proxy used for editable properties */
struct HPropertyWidgetProxyTools : public HHitProxy
{
	DECLARE_HIT_PROXY();

	/** Name of property this is the widget for */
	FString	PropertyName;

	/** If the property is an array property, the index into that array that this widget is for */
	int32	PropertyIndex;

	/** This property is a transform */
	bool	bPropertyIsTransform;

	HPropertyWidgetProxyTools(FString InPropertyName, int32 InPropertyIndex, bool bInPropertyIsTransform)
		: HHitProxy(HPP_Foreground)
		, PropertyName(InPropertyName)
		, PropertyIndex(InPropertyIndex)
		, bPropertyIsTransform(bInPropertyIsTransform)
	{}

	/** Show cursor as cross when over this handle */
	virtual EMouseCursor::Type GetMouseCursor() override
	{
		return EMouseCursor::Crosshairs;
	}
};

IMPLEMENT_HIT_PROXY(HPropertyWidgetProxyTools, HHitProxy);



//////////////////////////////////
// UEdMode

UEdMode::UEdMode()
	: bPendingDeletion(false)
	, Owner(nullptr)
{
	bDrawKillZ = true;
	ToolsContext = nullptr;
	ToolsContextClass = UEdModeInteractiveToolsContext::StaticClass();
	ToolCommandList = MakeShareable(new FUICommandList);
}

void UEdMode::Initialize()
{
}

bool UEdMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	if (ToolsContext->MouseEnter(ViewportClient, Viewport, x, y))
	{
		return true;
	}

	return false;
}

bool UEdMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	if (ToolsContext->MouseLeave(ViewportClient, Viewport))
	{
		return true;
	}

	return false;
}

bool UEdMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	if (ToolsContext->MouseMove(ViewportClient, Viewport, x, y))
	{
		return true;
	}

	return false;
}

bool UEdMode::ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return false;
}

bool UEdMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return false;
}

bool UEdMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	// if alt is down we will not allow client to see this event
	if (ToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY))
	{
		return true;
	}

	return false;
}

bool UEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!Viewport)
	{
		return false;
	}

	if (ToolsContext->InputKey(ViewportClient, Viewport, Key, Event))
	{
		return true;
	}

	if (Event != IE_Released)
	{
		if (ToolCommandList->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
		{
			return true;
		}
	}
	// Next pass input to the mode toolkit
	if (Toolkit.IsValid() && ((Event == IE_Pressed) || (Event == IE_Repeat)))
	{
		if (Toolkit->GetToolkitCommands()->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), (Event == IE_Repeat)))
		{
			return true;
		}
	}

	// Finally, pass input up to selected actors if not in a tool mode
	TArray<AActor*> SelectedActors;
	Owner->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	for (TArray<AActor*>::TIterator It(SelectedActors); It; ++It)
	{
		// Tell the object we've had a key press
		(*It)->EditorKeyPressed(Key, Event);
	}

	return false;
}

bool UEdMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	return false;
}

bool UEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	return false;
}

void UEdMode::SelectNone()
{
	GEditor->SelectNone(true, true);
}

bool UEdMode::ProcessEditDelete()
{
	if (ToolsContext->ProcessEditDelete())
	{
		return true;
	}

	return false;
}

void UEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	// Tick the ToolsContext
	ToolsContext->Tick(ViewportClient, DeltaTime);
}


bool UEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	return false;
}

void UEdMode::Enter()
{
	// Update components for selected actors, in case the mode we just exited
	// was hijacking selection events selection and not updating components.
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
	{
		AActor* SelectedActor = CastChecked<AActor>(*It);
		SelectedActor->MarkComponentsRenderStateDirty();
	}

	bPendingDeletion = false;

	// initialize the adapter that attaches the ToolsContext to this FEdMode
	ToolsContext = NewObject<UEdModeInteractiveToolsContext>(GetTransientPackage(), ToolsContextClass.Get(), TEXT("ToolsContext"), RF_Transient);
	ToolsContext->InitializeContextWithEditorModeManager(GetModeManager());


	GetToolManager()->OnToolStarted.AddUObject(this, &UEdMode::OnToolStarted);
	GetToolManager()->OnToolEnded.AddUObject(this, &UEdMode::OnToolEnded);

	// Now that the context is ready, make the toolkit
	CreateToolkit();

	FEditorDelegates::EditorModeIDEnter.Broadcast(GetID());

}

void UEdMode::RegisterTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
{
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
	CommandList->MapAction(UICommand,
		FExecuteAction::CreateLambda([this, ToolIdentifier]() { this->ToolsContext->StartTool(ToolIdentifier); }),
		FCanExecuteAction::CreateLambda([this, ToolIdentifier]() { return this->ToolsContext->ToolManager->CanActivateTool(EToolSide::Mouse, ToolIdentifier); }),
		FIsActionChecked::CreateLambda([this, Builder]() { return this->ToolsContext->IsToolBuilderActive(EToolSide::Mouse, Builder); }));
}

void UEdMode::Exit()
{
	GetToolManager()->OnToolStarted.RemoveAll(this);
	GetToolManager()->OnToolEnded.RemoveAll(this);

	ToolsContext->ShutdownContext();
	ToolsContext = nullptr;

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEditorDelegates::EditorModeIDExit.Broadcast(GetID());
}

UTexture2D* UEdMode::GetVertexTexture()
{
	return GEngine->DefaultBSPVertexTexture;
}

void UEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	// give ToolsContext a chance to render
	if (ToolsContext != nullptr)
	{
		ToolsContext->Render(View, Viewport, PDI);
	}
}

void UEdMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	// give ToolsContext a chance to draw in screen space
	if (ToolsContext != nullptr)
	{
		ToolsContext->DrawHUD(ViewportClient, Viewport, View, Canvas);
	}

	// Render the drag tool.
	ViewportClient->RenderDragTool(View, Canvas);

	if (ViewportClient->IsPerspective() && GetDefault<ULevelEditorViewportSettings>()->bHighlightWithBrackets)
	{
		DrawBrackets(ViewportClient, Viewport, View, Canvas);
	}

	// If this viewport doesn't show mode widgets, leave.
	if (!(ViewportClient->EngineShowFlags.ModeWidgets))
	{
		return;
	}

	// Clear Hit proxies
	const bool bIsHitTesting = Canvas->IsHitTesting();
	if (!bIsHitTesting)
	{
		Canvas->SetHitProxy(NULL);
	}

	// Draw vertices for selected BSP brushes and static meshes if the large vertices show flag is set.
	if (!ViewportClient->bDrawVertices)
	{
		return;
	}

	const bool bLargeVertices = View->Family->EngineShowFlags.LargeVertices;
	const bool bShowBrushes = View->Family->EngineShowFlags.Brushes;
	const bool bShowBSP = View->Family->EngineShowFlags.BSP;
	const bool bShowBuilderBrush = View->Family->EngineShowFlags.BuilderBrush != 0;

	UTexture2D* VertexTexture = GetVertexTexture();
	const float TextureSizeX = VertexTexture->GetSizeX() * (bLargeVertices ? 1.0f : 0.5f);
	const float TextureSizeY = VertexTexture->GetSizeY() * (bLargeVertices ? 1.0f : 0.5f);

	// Temporaries.
	TArray<FVector> Vertices;

	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
	{
		AActor* SelectedActor = static_cast<AActor*>(*It);
		checkSlow(SelectedActor->IsA(AActor::StaticClass()));

		if (bLargeVertices)
		{
			FCanvasItemTestbed::bTestState = !FCanvasItemTestbed::bTestState;

			// Static mesh vertices
			AStaticMeshActor* Actor = Cast<AStaticMeshActor>(SelectedActor);
			if (Actor && Actor->GetStaticMeshComponent() && Actor->GetStaticMeshComponent()->GetStaticMesh()
				&& Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData())
			{
				FTransform ActorToWorld = Actor->ActorToWorld();
				Vertices.Empty();
				const FPositionVertexBuffer& VertexBuffer = Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				for (uint32 i = 0; i < VertexBuffer.GetNumVertices(); i++)
				{
					Vertices.AddUnique(ActorToWorld.TransformPosition(VertexBuffer.VertexPosition(i)));
				}

				const float InvDpiScale = 1.0f / Canvas->GetDPIScale();

				FCanvasTileItem TileItem(FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.0f), FLinearColor::White);
				TileItem.BlendMode = SE_BLEND_Translucent;
				for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
				{
					const FVector& Vertex = Vertices[VertexIndex];
					FVector2D PixelLocation;
					if (View->ScreenToPixel(View->WorldToScreen(Vertex), PixelLocation))
					{
						PixelLocation *= InvDpiScale;

						const bool bOutside =
							PixelLocation.X < 0.0f || PixelLocation.X > View->UnscaledViewRect.Width()*InvDpiScale ||
							PixelLocation.Y < 0.0f || PixelLocation.Y > View->UnscaledViewRect.Height()*InvDpiScale;
						if (!bOutside)
						{
							const float X = PixelLocation.X - (TextureSizeX / 2);
							const float Y = PixelLocation.Y - (TextureSizeY / 2);
							if (bIsHitTesting)
							{
								Canvas->SetHitProxy(new HStaticMeshVert(Actor, Vertex));
							}
							TileItem.Texture = VertexTexture->Resource;

							TileItem.Size = FVector2D(TextureSizeX, TextureSizeY);
							Canvas->DrawItem(TileItem, FVector2D(X, Y));
							if (bIsHitTesting)
							{
								Canvas->SetHitProxy(NULL);
							}
						}
					}
				}
			}
		}
	}
}

void UEdMode::DrawBrackets(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	USelection& SelectedActors = *Owner->GetSelectedActors();
	for (int32 CurSelectedActorIndex = 0; CurSelectedActorIndex < SelectedActors.Num(); ++CurSelectedActorIndex)
	{
		AActor* SelectedActor = Cast<AActor>(SelectedActors.GetSelectedObject(CurSelectedActorIndex));
		if (SelectedActor != NULL)
		{
			// Draw a bracket for selected "paintable" static mesh actors
			const bool bIsValidActor = (Cast< AStaticMeshActor >(SelectedActor) != NULL);

			const FLinearColor SelectedActorBoxColor(0.6f, 0.6f, 1.0f);
			const bool bDrawBracket = bIsValidActor;
			ViewportClient->DrawActorScreenSpaceBoundingBox(Canvas, View, Viewport, SelectedActor, SelectedActorBoxColor, bDrawBracket);
		}
	}
}

bool UEdMode::UsesToolkits() const
{
	return true;
}

UWorld* UEdMode::GetWorld() const
{
	return Owner->GetWorld();
}

class FEditorModeTools* UEdMode::GetModeManager() const
{
	return Owner;
}

bool UEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (ToolsContext->StartTracking(InViewportClient, InViewport))
	{
		return true;
	}

	return false; 
}

bool UEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (ToolsContext->EndTracking(InViewportClient, InViewport))
	{
		return true;
	}

	return false; 
}

AActor* UEdMode::GetFirstSelectedActorInstance() const
{
	return Owner->GetSelectedActors()->GetTop<AActor>();
}

UInteractiveToolManager* UEdMode::GetToolManager() const
{
	return ToolsContext->ToolManager;
}

void UEdMode::CreateToolkit()
{
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());


	}

	UClass* LoadedSettingsObject = SettingsClass.LoadSynchronous();
	SettingsObject = NewObject<UObject>(this, LoadedSettingsObject);
	if (SettingsObject)
	{
		Toolkit->SetModeSettingsObject(SettingsObject);
	}
}

bool UEdMode::IsSnapRotationEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->RotGridEnabled;
}
