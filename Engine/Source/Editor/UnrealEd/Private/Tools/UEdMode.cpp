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
#include "EditorViewportClient.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolObjects.h"
#include "Editor/EditorEngine.h"
#include "GameFramework/Actor.h"

#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"

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
	ToolsContext = nullptr;
	ToolCommandList = MakeShareable(new FUICommandList);
}

void UEdMode::Initialize()
{
}

bool UEdMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	return false;
}

bool UEdMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return false;
}

bool UEdMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
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
	return false;
}

bool UEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!Viewport)
	{
		return false;
	}

	if (Event != IE_Released)
	{
		if (ToolsContext->ShouldIgnoreHotkeys() == false)		// allow the context to capture keyboard input if necessary
		{
			if (ToolCommandList->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
			{
				return true;
			}
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
	Owner->GetEditorSelectionSet()->ForEachSelectedObject<AActor>([Key, Event](AActor* ActorPtr)
		{
			ActorPtr->EditorKeyPressed(Key, Event);
			return true;
		});

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
}

bool UEdMode::ProcessEditDelete()
{
	return false;
}

void UEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
}


bool UEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	return false;
}

void UEdMode::Enter()
{
	// Update components for selected actors, in case the mode we just exited
	// was hijacking selection events selection and not updating components.
	Owner->GetEditorSelectionSet()->ForEachSelectedObject<AActor>([](AActor* ActorPtr)
		{
			ActorPtr->MarkComponentsRenderStateDirty();
			return true;
		});

	bPendingDeletion = false;

	ToolsContext = Owner->GetInteractiveToolsContext();
	check(ToolsContext.IsValid());

	GetToolManager()->OnToolStarted.AddUObject(this, &UEdMode::OnToolStarted);
	GetToolManager()->OnToolEnded.AddUObject(this, &UEdMode::OnToolEnded);

	// Create the settings object so that the toolkit has access to the object we are going to use at creation time
	if (SettingsClass.IsValid())
	{
		UClass* LoadedSettingsObject = SettingsClass.LoadSynchronous();
		SettingsObject = NewObject<UObject>(this, LoadedSettingsObject);
	}

	// Now that the context is ready, make the toolkit
	CreateToolkit();
	if (Toolkit.IsValid())
	{
		Toolkit->Init(Owner->GetToolkitHost(), this);
	}

	BindCommands();

	if (SettingsObject)
	{
		SettingsObject->LoadConfig();

		if (Toolkit.IsValid())
		{
			Toolkit->SetModeSettingsObject(SettingsObject);
		}
	}

	FEditorDelegates::EditorModeIDEnter.Broadcast(GetID());

}

void UEdMode::RegisterTool(TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
{
	if (!Toolkit.IsValid())
	{
		return;
	}

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
	CommandList->MapAction(UICommand,
		FExecuteAction::CreateUObject(ToolsContext.Get(), &UEdModeInteractiveToolsContext::StartTool, ToolIdentifier),
		FCanExecuteAction::CreateUObject(ToRawPtr(ToolsContext->ToolManager), &UInteractiveToolManager::CanActivateTool, EToolSide::Mouse, ToolIdentifier),
		FIsActionChecked::CreateUObject(ToolsContext.Get(), &UEdModeInteractiveToolsContext::IsToolActive, EToolSide::Mouse, ToolIdentifier),
		EUIActionRepeatMode::RepeatDisabled);

	RegisteredTools.Emplace(UICommand, ToolIdentifier);
}

void UEdMode::Exit()
{
	if (SettingsObject)
	{
		SettingsObject->SaveConfig();
	}

	if (Toolkit.IsValid())
	{
		const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
		for (auto& RegisteredTool : RegisteredTools)
		{
			CommandList->UnmapAction(RegisteredTool.Key);
			ToolsContext->ToolManager->UnregisterToolType(RegisteredTool.Value);
		}

		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}
	RegisteredTools.SetNum(0);

	GetToolManager()->OnToolStarted.RemoveAll(this);
	GetToolManager()->OnToolEnded.RemoveAll(this);

	ToolsContext = nullptr;

	FEditorDelegates::EditorModeIDExit.Broadcast(GetID());
}

UTexture2D* UEdMode::GetVertexTexture()
{
	return GEngine->DefaultBSPVertexTexture;
}

void UEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
}

void UEdMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
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
	if (!bLargeVertices)
	{
		return;
	}

	// Temporaries.
	UTexture2D* VertexTexture = GetVertexTexture();
	const float TextureSizeX = VertexTexture->GetSizeX() * (bLargeVertices ? 1.0f : 0.5f);
	const float TextureSizeY = VertexTexture->GetSizeY() * (bLargeVertices ? 1.0f : 0.5f);

	// Static mesh vertices
	Owner->GetEditorSelectionSet()->ForEachSelectedObject<AStaticMeshActor>([View, Canvas, VertexTexture, TextureSizeX, TextureSizeY, bIsHitTesting](AStaticMeshActor* Actor)
		{
			TArray<FVector> Vertices;
			FCanvasItemTestbed::bTestState = !FCanvasItemTestbed::bTestState;

			if (Actor->GetStaticMeshComponent() && Actor->GetStaticMeshComponent()->GetStaticMesh()
				&& Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData())
			{
				FTransform ActorToWorld = Actor->ActorToWorld();
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
							PixelLocation.X < 0.0f || PixelLocation.X > View->UnscaledViewRect.Width() * InvDpiScale ||
							PixelLocation.Y < 0.0f || PixelLocation.Y > View->UnscaledViewRect.Height() * InvDpiScale;
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
			return true;
		});
}

void UEdMode::DrawBrackets(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	Owner->GetEditorSelectionSet()->ForEachSelectedObject<AActor>([ViewportClient, Canvas, View, Viewport](AActor* SelectedActor)
		{
			// Draw a bracket for selected "paintable" static mesh actors
			const bool bIsValidActor = (Cast< AStaticMeshActor >(SelectedActor) != NULL);

			const FLinearColor SelectedActorBoxColor(0.6f, 0.6f, 1.0f);
			const bool bDrawBracket = bIsValidActor;
			ViewportClient->DrawActorScreenSpaceBoundingBox(Canvas, View, Viewport, SelectedActor, SelectedActorBoxColor, bDrawBracket);

			return true;
		});
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
	return false; 
}

bool UEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return false; 
}

AActor* UEdMode::GetFirstSelectedActorInstance() const
{
	return Owner->GetEditorSelectionSet()->GetTopSelectedObject<AActor>();
}

UInteractiveToolManager* UEdMode::GetToolManager() const
{
	if (ToolsContext.IsValid())
	{
		return ToolsContext->ToolManager;
	}

	return nullptr;
}

TWeakObjectPtr<UEdModeInteractiveToolsContext> UEdMode::GetInteractiveToolsContext() const
{
	return ToolsContext;
}

void UEdMode::CreateToolkit()
{
	if (!UsesToolkits())
	{
		return;
	}

	check(!Toolkit.IsValid())
	Toolkit = MakeShareable(new FModeToolkit);
}

bool UEdMode::IsSnapRotationEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->RotGridEnabled;
}
