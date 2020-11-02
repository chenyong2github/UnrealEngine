// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdMode.h"
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

#include "CanvasTypes.h"

//////////////////////////////////
// FEdMode

FEdMode::FEdMode()
	: bPendingDeletion(false)
	, CurrentTool(nullptr)
{
	bDrawKillZ = true;
}

FEdMode::~FEdMode()
{
}

void FEdMode::OnModeUnregistered( FEditorModeID ModeID )
{
	if( ModeID == Info.ID )
	{
		// This should be synonymous with "delete this"
		Owner->DestroyMode(ModeID);
	}
}

bool FEdMode::MouseEnter( FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y )
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->MouseEnter( ViewportClient, Viewport, x, y );
	}

	return false;
}

bool FEdMode::MouseLeave( FEditorViewportClient* ViewportClient,FViewport* Viewport )
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->MouseLeave( ViewportClient, Viewport );
	}

	return false;
}

bool FEdMode::MouseMove(FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y)
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->MouseMove( ViewportClient, Viewport, x, y );
	}

	return false;
}

bool FEdMode::ReceivedFocus(FEditorViewportClient* ViewportClient,FViewport* Viewport)
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->ReceivedFocus( ViewportClient, Viewport );
	}

	return false;
}

bool FEdMode::LostFocus(FEditorViewportClient* ViewportClient,FViewport* Viewport)
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->LostFocus( ViewportClient, Viewport );
	}

	return false;
}

bool FEdMode::CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY )
{
	if( GetCurrentTool() )
	{
		return GetCurrentTool()->CapturedMouseMove( InViewportClient, InViewport, InMouseX, InMouseY );
	}

	return false;
}

bool FEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// First try the currently selected tool
	if ((GetCurrentTool() != nullptr) && GetCurrentTool()->InputKey(ViewportClient, Viewport, Key, Event))
	{
		return true;
	}
	else
	{
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
	}

	return false;
}

bool FEdMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	FModeTool* Tool = GetCurrentTool();
	if (Tool)
	{
		return Tool->InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
	}

	return false;
}

bool FEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{	
	if (FLegacyEdModeWidgetHelper::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale))
	{
		return true;
	}

	if (GetCurrentTool())
	{
		return GetCurrentTool()->InputDelta(InViewportClient,InViewport,InDrag,InRot,InScale);
	}

	return false;
}

bool FEdMode::UsesTransformWidget() const
{
	if (GetCurrentTool())
	{
		return GetCurrentTool()->UseWidget();
	}

	return true;
}

bool FEdMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return FLegacyEdModeWidgetHelper::UsesTransformWidget(CheckMode);
}

bool FEdMode::UsesPropertyWidgets() const
{
	return false;
}

bool FEdMode::BoxSelect( FBox& InBox, bool InSelect )
{
	bool bResult = false;
	if( GetCurrentTool() )
	{
		bResult = GetCurrentTool()->BoxSelect( InBox, InSelect );
	}
	return bResult;
}

bool FEdMode::FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect )
{
	bool bResult = false;
	if( GetCurrentTool() )
	{
		bResult = GetCurrentTool()->FrustumSelect( InFrustum, InViewportClient, InSelect );
	}
	return bResult;
}

void FEdMode::SelectNone()
{
	if( GetCurrentTool() )
	{
		GetCurrentTool()->SelectNone();
	}
}

void FEdMode::Tick(FEditorViewportClient* ViewportClient,float DeltaTime)
{
	if( GetCurrentTool() )
	{
		GetCurrentTool()->Tick(ViewportClient,DeltaTime);
	}
}

bool FEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy *HitProxy, const FViewportClick &Click)
{
	if (FLegacyEdModeWidgetHelper::HandleClick(InViewportClient, HitProxy, Click))
	{
		return true;
	}

	return false;
}

void FEdMode::Enter()
{
	// Update components for selected actors, in case the mode we just exited
	// was hijacking selection events selection and not updating components.
	for ( FSelectionIterator It( *Owner->GetSelectedActors() ) ; It ; ++It )
	{
		AActor* SelectedActor = CastChecked<AActor>( *It );
		SelectedActor->MarkComponentsRenderStateDirty();
	}

	bPendingDeletion = false;

	FEditorDelegates::EditorModeIDEnter.Broadcast( GetID() );
}

void FEdMode::Exit()
{
	FEditorDelegates::EditorModeIDExit.Broadcast(GetID());
}

UTexture2D* FEdMode::GetVertexTexture()
{
	return GEngine->DefaultBSPVertexTexture;
}

void FEdMode::SetCurrentTool( EModeTools InID )
{
	CurrentTool = FindTool( InID );
	check( CurrentTool );	// Tool not found!  This can't happen.

	CurrentToolChanged();
}

void FEdMode::SetCurrentTool( FModeTool* InModeTool )
{
	CurrentTool = InModeTool;
	check(CurrentTool);

	CurrentToolChanged();
}

FModeTool* FEdMode::FindTool( EModeTools InID )
{
	for( int32 x = 0 ; x < Tools.Num() ; ++x )
	{
		if( Tools[x]->GetID() == InID )
		{
			return Tools[x];
		}
	}

	UE_LOG(LogEditorModes, Fatal, TEXT("FEdMode::FindTool failed to find tool %d"), (int32)InID);
	return NULL;
}

void FEdMode::Render(const FSceneView* View,FViewport* Viewport,FPrimitiveDrawInterface* PDI)
{
	// Let the current mode tool render if it wants to
	if (FModeTool* Tool = GetCurrentTool())
	{
		Tool->Render( View, Viewport, PDI );
	}

	FLegacyEdModeWidgetHelper::Render(View, Viewport, PDI);
}

void FEdMode::DrawHUD(FEditorViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas)
{
	// Render the drag tool.
	ViewportClient->RenderDragTool( View, Canvas );

	// Let the current mode tool draw a HUD if it wants to
	FModeTool* tool = GetCurrentTool();
	if( tool )
	{
		tool->DrawHUD( ViewportClient, Viewport, View, Canvas );
	}

	if (ViewportClient->IsPerspective() && GetDefault<ULevelEditorViewportSettings>()->bHighlightWithBrackets)
	{
		DrawBrackets( ViewportClient, Viewport, View, Canvas );
	}

	// If this viewport doesn't show mode widgets or the mode itself doesn't want them, leave.
	if( !(ViewportClient->EngineShowFlags.ModeWidgets) || !ShowModeWidgets() )
	{
		return;
	}

	// Clear Hit proxies
	const bool bIsHitTesting = Canvas->IsHitTesting();
	if ( !bIsHitTesting )
	{
		Canvas->SetHitProxy(NULL);
	}

	// Draw vertices for selected BSP brushes and static meshes if the large vertices show flag is set.
	if ( !ViewportClient->bDrawVertices )
	{
		return;
	}

	const bool bLargeVertices		= View->Family->EngineShowFlags.LargeVertices;
	const bool bShowBrushes			= View->Family->EngineShowFlags.Brushes;
	const bool bShowBSP				= View->Family->EngineShowFlags.BSP;
	const bool bShowBuilderBrush	= View->Family->EngineShowFlags.BuilderBrush != 0;

	UTexture2D* VertexTexture = GetVertexTexture();
	const float TextureSizeX		= VertexTexture->GetSizeX() * ( bLargeVertices ? 1.0f : 0.5f );
	const float TextureSizeY		= VertexTexture->GetSizeY() * ( bLargeVertices ? 1.0f : 0.5f );

	// Temporaries.
	TArray<FVector> Vertices;

	for ( FSelectionIterator It( *Owner->GetSelectedActors() ) ; It ; ++It )
	{
		AActor* SelectedActor = static_cast<AActor*>( *It );
		checkSlow( SelectedActor->IsA(AActor::StaticClass()) );

		if( bLargeVertices )
		{
			FCanvasItemTestbed::bTestState = !FCanvasItemTestbed::bTestState;

			// Static mesh vertices
			AStaticMeshActor* Actor = Cast<AStaticMeshActor>( SelectedActor );
			if( Actor && Actor->GetStaticMeshComponent() && Actor->GetStaticMeshComponent()->GetStaticMesh()
				&& Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData())
			{
				FTransform ActorToWorld = Actor->ActorToWorld();
				Vertices.Empty();
				const FPositionVertexBuffer& VertexBuffer = Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				for( uint32 i = 0 ; i < VertexBuffer.GetNumVertices() ; i++ )
				{
					Vertices.AddUnique( ActorToWorld.TransformPosition( VertexBuffer.VertexPosition(i) ) );
				}

				const float InvDpiScale = 1.0f / Canvas->GetDPIScale();

				FCanvasTileItem TileItem( FVector2D( 0.0f, 0.0f ), FVector2D( 0.0f, 0.0f ), FLinearColor::White );
				TileItem.BlendMode = SE_BLEND_Translucent;
				for( int32 VertexIndex = 0 ; VertexIndex < Vertices.Num() ; ++VertexIndex )
				{				
					const FVector& Vertex = Vertices[VertexIndex];
					FVector2D PixelLocation;
					if(View->ScreenToPixel(View->WorldToScreen(Vertex),PixelLocation))
					{
						PixelLocation *= InvDpiScale;

						const bool bOutside =
							PixelLocation.X < 0.0f || PixelLocation.X > View->UnscaledViewRect.Width()*InvDpiScale ||
							PixelLocation.Y < 0.0f || PixelLocation.Y > View->UnscaledViewRect.Height()*InvDpiScale;
						if( !bOutside )
						{
							const float X = PixelLocation.X - (TextureSizeX/2);
							const float Y = PixelLocation.Y - (TextureSizeY/2);
							if( bIsHitTesting ) 
							{
								Canvas->SetHitProxy( new HStaticMeshVert(Actor,Vertex) );
							}
							TileItem.Texture = VertexTexture->Resource;
							
							TileItem.Size = FVector2D( TextureSizeX, TextureSizeY );
							Canvas->DrawItem( TileItem, FVector2D( X, Y ) );							
							if( bIsHitTesting )
							{
								Canvas->SetHitProxy( NULL );
							}
						}
					}
				}
			}
		}
	}

	FLegacyEdModeWidgetHelper::DrawHUD(ViewportClient, Viewport, View, Canvas);
}

void FEdMode::DrawBrackets( FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas )
{
	USelection& SelectedActors = *Owner->GetSelectedActors();
	for( int32 CurSelectedActorIndex = 0; CurSelectedActorIndex < SelectedActors.Num(); ++CurSelectedActorIndex )
	{
		AActor* SelectedActor = Cast<AActor>( SelectedActors.GetSelectedObject(CurSelectedActorIndex ) );
		if( SelectedActor != NULL )
		{
			// Draw a bracket for selected "paintable" static mesh actors
			const bool bIsValidActor = ( Cast< AStaticMeshActor >( SelectedActor ) != NULL );

			const FLinearColor SelectedActorBoxColor( 0.6f, 0.6f, 1.0f );
			const bool bDrawBracket = bIsValidActor;
			ViewportClient->DrawActorScreenSpaceBoundingBox( Canvas, View, Viewport, SelectedActor, SelectedActorBoxColor, bDrawBracket );
		}
	}
}

bool FEdMode::UsesToolkits() const
{
	return false;
}

UWorld* FEdMode::GetWorld() const
{
	return Owner->GetWorld();
}

class FEditorModeTools* FEdMode::GetModeManager() const
{
	return Owner;
}

bool FEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bResult = false;
	if( GetCurrentTool() )
	{
		bResult = GetCurrentTool()->StartModify();
	}
	return bResult;
}

bool FEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bResult = false;
	if( GetCurrentTool() )
	{
		bResult = GetCurrentTool()->EndModify();
	}
	return bResult;
}

bool FEdMode::IsSnapRotationEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->RotGridEnabled;
}
