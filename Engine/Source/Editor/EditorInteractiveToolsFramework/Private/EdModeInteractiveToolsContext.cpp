// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "EdModeInteractiveToolsContext.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "LevelEditorViewport.h"   // for GCurrentLevelEditingViewportClient
#include "Engine/Selection.h"
#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"
#include "Materials/Material.h"

#include "EditorToolAssetAPI.h"
#include "EditorComponentSourceFactory.h"



//#define ENABLE_DEBUG_PRINTING




class FEdModeToolsContextQueriesImpl : public IToolsContextQueriesAPI
{
public:
	UEdModeInteractiveToolsContext* ToolsContext;
	FEdMode* EditorMode;
	
	FViewCameraState CachedViewState;

	FEdModeToolsContextQueriesImpl(UEdModeInteractiveToolsContext* Context, FEdMode* EditorModeIn)
	{
		ToolsContext = Context;
		EditorMode = EditorModeIn;
	}

	void CacheCurrentViewState(FEditorViewportClient* ViewportClient)
	{
		FViewportCameraTransform ViewTransform = ViewportClient->GetViewTransform();
		CachedViewState.Position = ViewTransform.GetLocation();
		CachedViewState.Orientation = ViewTransform.GetRotation().Quaternion();
		CachedViewState.bIsOrthographic = ViewportClient->IsOrtho();
		CachedViewState.bIsVR = false;
	}

	virtual void GetCurrentSelectionState(FToolBuilderState& StateOut) const override
	{
		StateOut.ToolManager = ToolsContext->ToolManager;
		StateOut.GizmoManager = ToolsContext->GizmoManager;
		StateOut.World = EditorMode->GetWorld();
		StateOut.SelectedActors = EditorMode->GetModeManager()->GetSelectedActors();
		StateOut.SelectedComponents = EditorMode->GetModeManager()->GetSelectedComponents();
		StateOut.SourceBuilder = ToolsContext->GetComponentSourceFactory();
	}


	virtual void GetCurrentViewState(FViewCameraState& StateOut) const override
	{
		StateOut = CachedViewState;
	}

	virtual UMaterialInterface* GetStandardMaterial(EStandardToolContextMaterials MaterialType) const
	{
		if (MaterialType == EStandardToolContextMaterials::VertexColorMaterial)
		{
			return ToolsContext->StandardVertexColorMaterial;
		}
		check(false);
		return nullptr;
	}

};




class FEdModeToolsContextTransactionImpl : public IToolsContextTransactionsAPI
{
public:
	UEdModeInteractiveToolsContext* ToolsContext;
	FEdMode* EditorMode;

	FEdModeToolsContextTransactionImpl(UEdModeInteractiveToolsContext* Context, FEdMode* EditorModeIn)
	{
		ToolsContext = Context;
		EditorMode = EditorModeIn;
	}


	virtual void PostMessage(const TCHAR* Message, EToolMessageLevel Level) override
	{
		UE_LOG(LogTemp, Warning, TEXT("[ToolsContext] %s"), Message);
	}


	virtual void PostInvalidation() override
	{
		ToolsContext->PostInvalidation();
	}

	virtual void BeginUndoTransaction(const FText& Description) override
	{
		GEditor->BeginTransaction(Description);
	}

	virtual void EndUndoTransaction() override
	{
		GEditor->EndTransaction();
	}

	virtual void AppendChange(UObject* TargetObject, TUniquePtr<FChange> Change, const FText& Description) override
	{
		FScopedTransaction Transaction(Description);
		check(GUndo != nullptr);
		GUndo->StoreUndo(TargetObject, MoveTemp(Change));
		// end transaction
	}


	virtual bool RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange) override
	{
		checkf(SelectionChange.Components.Num() == 0, TEXT("FEdModeToolsContextTransactionImpl::RequestSelectionChange - Component selection not supported yet"));

		if (SelectionChange.ModificationType == ESelectedObjectsModificationType::Clear)
		{
			GEditor->SelectNone(true, true, false);
			return true;
		}

		if (SelectionChange.ModificationType == ESelectedObjectsModificationType::Replace )
		{
			GEditor->SelectNone(false, true, false);
		}

		bool bAdd = (SelectionChange.ModificationType != ESelectedObjectsModificationType::Remove);
		int NumActors = SelectionChange.Actors.Num();
		for (int k = 0; k < NumActors; ++k)
		{
			GEditor->SelectActor(SelectionChange.Actors[k], bAdd, false, true, false);
		}

		GEditor->NoteSelectionChange(true);
		return true;
	}

};





UEdModeInteractiveToolsContext::UEdModeInteractiveToolsContext()
{
	EditorMode = nullptr;
	QueriesAPI = nullptr;
	TransactionAPI = nullptr;
	AssetAPI = nullptr;
	SourceFactory = nullptr;
}



void UEdModeInteractiveToolsContext::Initialize(IToolsContextQueriesAPI* QueriesAPIIn, IToolsContextTransactionsAPI* TransactionsAPIIn)
{
	UInteractiveToolsContext::Initialize(QueriesAPIIn, TransactionsAPIIn);

	bInvalidationPending = false;
}

void UEdModeInteractiveToolsContext::Shutdown()
{
	UInteractiveToolsContext::Shutdown();
}




void UEdModeInteractiveToolsContext::InitializeContextFromEdMode(FEdMode* EditorModeIn)
{
	this->EditorMode = EditorModeIn;

	this->TransactionAPI = new FEdModeToolsContextTransactionImpl(this, EditorModeIn);
	this->QueriesAPI = new FEdModeToolsContextQueriesImpl(this, EditorModeIn);
	this->AssetAPI = new FEditorToolAssetAPI();
	this->SourceFactory = new FEditorComponentSourceFactory();

	Initialize(QueriesAPI, TransactionAPI);

	// enable auto invalidation in Editor, because invalidating for all hover and capture events is unpleasant
	this->InputRouter->bAutoInvalidateOnHover = true;
	this->InputRouter->bAutoInvalidateOnCapture = true;


	// set up standard materials
	StandardVertexColorMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/VertexColor"));
}


void UEdModeInteractiveToolsContext::ShutdownContext()
{
	Shutdown();

	if (QueriesAPI != nullptr)
	{
		delete QueriesAPI;
		QueriesAPI = nullptr;
	}

	if (TransactionAPI != nullptr)
	{
		delete TransactionAPI;
		TransactionAPI = nullptr;
	}

	if (AssetAPI != nullptr)
	{
		delete AssetAPI;
		AssetAPI = nullptr;
	}

	if (SourceFactory != nullptr)
	{
		delete SourceFactory;
		SourceFactory = nullptr;
	}

	this->EditorMode = nullptr;
}



void UEdModeInteractiveToolsContext::PostInvalidation()
{
	bInvalidationPending = true;
}


void UEdModeInteractiveToolsContext::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	ToolManager->Tick(DeltaTime);
	GizmoManager->Tick(DeltaTime);

	if (bInvalidationPending)
	{
		ViewportClient->Invalidate();
		bInvalidationPending = false;
	}

	// save this view
	// Check against GCurrentLevelEditingViewportClient is temporary and should be removed in future.
	// Current issue is that this ::Tick() is called *per viewport*, so once for each view in a 4-up view.
	if (ViewportClient == GCurrentLevelEditingViewportClient)
	{
		((FEdModeToolsContextQueriesImpl*)this->QueriesAPI)->CacheCurrentViewState(ViewportClient);
	}
}



class TempRenderContext : public IToolsContextRenderAPI
{
public:
	FPrimitiveDrawInterface* PDI;

	virtual FPrimitiveDrawInterface* GetPrimitiveDrawInterface() override
	{
		return PDI;
	}
};


void UEdModeInteractiveToolsContext::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	TempRenderContext RenderContext;
	RenderContext.PDI = PDI;
	ToolManager->Render(&RenderContext);
	GizmoManager->Render(&RenderContext);
}




bool UEdModeInteractiveToolsContext::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
#ifdef ENABLE_DEBUG_PRINTING
	if (Event == IE_Pressed) { UE_LOG(LogTemp, Warning, TEXT("PRESSED EVENT")); }
	else if (Event == IE_Released) { UE_LOG(LogTemp, Warning, TEXT("RELEASED EVENT")); }
	else if (Event == IE_Repeat) { UE_LOG(LogTemp, Warning, TEXT("REPEAT EVENT")); }
	else if (Event == IE_Axis) { UE_LOG(LogTemp, Warning, TEXT("AXIS EVENT")); }
	else if (Event == IE_DoubleClick) { UE_LOG(LogTemp, Warning, TEXT("DOUBLECLICK EVENT")); }
#endif

	bool bHandled = false;


	// escape key cancels current tool
	if (Key == EKeys::Escape && Event == IE_Released )
	{
		if (ToolManager->HasAnyActiveTool())
		{
			if (ToolManager->HasActiveTool(EToolSide::Mouse))
			{
				ToolManager->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Cancel);
			}
			return true;
		}
	}

	// enter key accepts current tool, or ends tool if it does not have accept state
	if (Key == EKeys::Enter && Event == IE_Released && ToolManager->HasAnyActiveTool())
	{
		if (ToolManager->HasActiveTool(EToolSide::Mouse)) 
		{
			if (ToolManager->GetActiveTool(EToolSide::Mouse)->HasAccept())
			{
				if (ToolManager->CanAcceptActiveTool(EToolSide::Mouse))
				{
					ToolManager->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Accept);
					return true;
				}
			}
			else
			{
				ToolManager->DeactivateTool(EToolSide::Mouse, EToolShutdownType::Completed);
				return true;
			}
		}
	}



	if (Event == IE_Pressed || Event == IE_Released)
	{
		bool bIsLeftMouse = (Key == EKeys::LeftMouseButton);
		bool bIsMiddleMouse = (Key == EKeys::MiddleMouseButton);
		bool bIsRightMouse = (Key == EKeys::RightMouseButton);

		if (bIsLeftMouse || bIsMiddleMouse || bIsRightMouse)
		{

			// early-out here if we are going to do camera manipulation
			if (ViewportClient->IsAltPressed())
			{
				return bHandled;
			}

			FInputDeviceState InputState = CurrentInputState;
			InputState.InputDevice = EInputDevices::Mouse;
			InputState.SetKeyStates(
				ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
				ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());

			if (bIsLeftMouse)
			{
				InputState.Mouse.Left.SetStates(
					(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
				CurrentInputState.Mouse.Left.bDown = (Event == IE_Pressed);
			}
			else if (bIsMiddleMouse)
			{
				InputState.Mouse.Middle.SetStates(
					(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
				CurrentInputState.Mouse.Middle.bDown = (Event == IE_Pressed);
			}
			else
			{
				InputState.Mouse.Right.SetStates(
					(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
				CurrentInputState.Mouse.Right.bDown = (Event == IE_Pressed);
			}

			InputRouter->PostInputEvent(InputState);

			if (InputRouter->HasActiveMouseCapture())
			{
				// what is this about? MeshPaintMode has it...
				ViewportClient->bLockFlightCamera = true;
				bHandled = true;   // indicate that we handled this event,
								   // which will disable camera movement/etc ?
			}
			else
			{
				//ViewportClient->bLockFlightCamera = false;
			}

		}

	}

	return bHandled;
}




bool UEdModeInteractiveToolsContext::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("MOUSE ENTER"));
#endif

	CurrentInputState.Mouse.Position2D = FVector2D(x, y);
	CurrentInputState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);

	return false;
}


bool UEdModeInteractiveToolsContext::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
#ifdef ENABLE_DEBUG_PRINTING
	//UE_LOG(LogTemp, Warning, TEXT("MOUSE MOVE"));
#endif

	CurrentInputState.Mouse.Position2D = FVector2D(x, y);
	CurrentInputState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);
	FInputDeviceState InputState = CurrentInputState;
	InputState.InputDevice = EInputDevices::Mouse;

	InputState.SetKeyStates(
		ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
		ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());

	if (InputRouter->HasActiveMouseCapture())
	{
		// This state occurs if InputBehavior did not release capture on mouse release. 
		// UMultiClickSequenceInputBehavior does this, eg for multi-click draw-polygon sequences.
		// It's not ideal though and maybe would be better done via multiple captures + hover...?
		InputRouter->PostInputEvent(InputState);
	}
	else
	{
		InputRouter->PostHoverInputEvent(InputState);
	}

	return false;
}


bool UEdModeInteractiveToolsContext::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("MOUSE LEAVE"));
#endif

	return false;
}



bool UEdModeInteractiveToolsContext::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return false;
}

bool UEdModeInteractiveToolsContext::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
#ifdef ENABLE_DEBUG_PRINTING
	//UE_LOG(LogTemp, Warning, TEXT("CAPTURED MOUSE MOVE"));
#endif

	FVector2D OldPosition = CurrentInputState.Mouse.Position2D;
	CurrentInputState.Mouse.Position2D = FVector2D(InMouseX, InMouseY);
	CurrentInputState.Mouse.WorldRay = GetRayFromMousePos(InViewportClient, InViewport, InMouseX, InMouseY);

	if (InputRouter->HasActiveMouseCapture())
	{
		FInputDeviceState InputState = CurrentInputState;
		InputState.InputDevice = EInputDevices::Mouse;
		InputState.SetKeyStates(
			InViewportClient->IsShiftPressed(), InViewportClient->IsAltPressed(),
			InViewportClient->IsCtrlPressed(), InViewportClient->IsCmdPressed());
		InputState.Mouse.Delta2D = CurrentInputState.Mouse.Position2D - OldPosition;
		InputRouter->PostInputEvent(InputState);
		return true;
	}

	return false;
}


bool UEdModeInteractiveToolsContext::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("END TRACKING"));
#endif

	return true;
}






FRay UEdModeInteractiveToolsContext::GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY)
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, (FEditorViewportClient*)Viewport->GetClient(), MouseX, MouseY);

	return FRay(MouseViewportRay.GetOrigin(), MouseViewportRay.GetDirection(), true);
}
