// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "InteractiveToolsContext.h"
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


class FEdModeQueriesImpl : public IToolsContextQueriesAPI
{
public:
	UInteractiveToolsContext* ToolsContext;
	UEdMode* EdMode;
	FViewCameraState CachedViewState;

	FEdModeQueriesImpl(UInteractiveToolsContext* Context, UEdMode* InEdMode)
	{
		ToolsContext = Context;
		EdMode = InEdMode;
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
		StateOut.World = GEditor->GetWorld();
		GEditor->GetSelectedActors()->GetSelectedObjects(StateOut.SelectedActors);
		GEditor->GetSelectedComponents()->GetSelectedObjects(StateOut.SelectedComponents);
	}


	virtual void GetCurrentViewState(FViewCameraState& StateOut) const override
	{
		StateOut = CachedViewState;
	}


	virtual EToolContextCoordinateSystem GetCurrentCoordinateSystem() const override
	{
		ECoordSystem CoordSys = EdMode->GetModeManager()->GetCoordSystem();
		return (CoordSys == COORD_World) ? EToolContextCoordinateSystem::World : EToolContextCoordinateSystem::Local;
	}


	virtual bool ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const override
	{
		if (Request.RequestType != ESceneSnapQueryType::Position)
		{
			return false;		// not supported yet
		}

		int FoundResultCount = 0;

		//
		// Run a snap query by casting ray into the world.
		// If a hit is found, we look up what triangle was hit, and then test its vertices and edges
		// 

		// cast ray into world
		FVector RayStart = CachedViewState.Position;
		FVector RayDirection = Request.Position - RayStart; RayDirection.Normalize();
		FVector RayEnd = RayStart + 9999999 * RayDirection;
		FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
		FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
		QueryParams.bTraceComplex = true;
		QueryParams.bReturnFaceIndex = true;
		FHitResult HitResult;
		bool bHitWorld = GEditor->GetWorld()->LineTraceSingleByObjectType(HitResult, RayStart, RayEnd, ObjectQueryParams, QueryParams);
		if (bHitWorld && HitResult.FaceIndex >= 0)
		{
			float VisualAngle = OpeningAngleDeg(Request.Position, HitResult.ImpactPoint, RayStart);
			//UE_LOG(LogTemp, Warning, TEXT("[HIT] visualangle %f faceindex %d"), VisualAngle, HitResult.FaceIndex);
			if (VisualAngle < Request.VisualAngleThresholdDegrees)
			{
				UPrimitiveComponent* Component = HitResult.Component.Get();
				if (Cast<UStaticMeshComponent>(Component) != nullptr)
				{
					// HitResult.FaceIndex is apparently an index into the TriMeshCollisionData, not sure how
					// to directly access it. Calling GetPhysicsTriMeshData is expensive!
					//UBodySetup* BodySetup = Cast<UStaticMeshComponent>(Component)->GetBodySetup();
					//UObject* CDPObj = BodySetup->GetOuter();
					//IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(CDPObj);
					//FTriMeshCollisionData TriMesh;
					//CDP->GetPhysicsTriMeshData(&TriMesh, true);
					//FTriIndices Triangle = TriMesh.Indices[HitResult.FaceIndex];
					//FVector Positions[3] = { TriMesh.Vertices[Triangle.v0], TriMesh.Vertices[Triangle.v1], TriMesh.Vertices[Triangle.v2] };

					// physics collision data is created from StaticMesh RenderData
					// so use HitResult.FaceIndex to extract triangle from the LOD0 mesh
					// (note: this may be incorrect if there are multiple sections...in that case I think we have to 
					//  first find section whose accumulated index range would contain .FaceIndexX)
					UStaticMesh* StaticMesh = Cast<UStaticMeshComponent>(Component)->GetStaticMesh();
					FStaticMeshLODResources& LOD = StaticMesh->RenderData->LODResources[0];
					FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
					int32 TriIdx = 3 * HitResult.FaceIndex;
					FVector Positions[3];
					Positions[0] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx]);
					Positions[1] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx + 1]);
					Positions[2] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx + 2]);

					// transform to world space
					FTransform ComponentTransform = Component->GetComponentTransform();
					Positions[0] = ComponentTransform.TransformPosition(Positions[0]);
					Positions[1] = ComponentTransform.TransformPosition(Positions[1]);
					Positions[2] = ComponentTransform.TransformPosition(Positions[2]);

					FSceneSnapQueryResult SnapResult;
					SnapResult.TriVertices[0] = Positions[0];
					SnapResult.TriVertices[1] = Positions[1];
					SnapResult.TriVertices[2] = Positions[2];

					// try snapping to vertices
					float SmallestAngle = Request.VisualAngleThresholdDegrees;
					if ((Request.TargetTypes & ESceneSnapQueryTargetType::MeshVertex) != ESceneSnapQueryTargetType::None)
					{
						for (int j = 0; j < 3; ++j)
						{
							VisualAngle = OpeningAngleDeg(Request.Position, Positions[j], RayStart);
							if (VisualAngle < SmallestAngle)
							{
								SmallestAngle = VisualAngle;
								SnapResult.Position = Positions[j];
								SnapResult.TargetType = ESceneSnapQueryTargetType::MeshVertex;
								SnapResult.TriSnapIndex = j;
							}
						}
					}

					// try snapping to nearest points on edges
					if (((Request.TargetTypes & ESceneSnapQueryTargetType::MeshEdge) != ESceneSnapQueryTargetType::None) &&
						(SnapResult.TargetType != ESceneSnapQueryTargetType::MeshVertex))
					{
						for (int j = 0; j < 3; ++j)
						{
							FVector EdgeNearestPt = NearestSegmentPt(Positions[j], Positions[(j + 1) % 3], Request.Position);
							VisualAngle = OpeningAngleDeg(Request.Position, EdgeNearestPt, RayStart);
							if (VisualAngle < SmallestAngle)
							{
								SmallestAngle = VisualAngle;
								SnapResult.Position = EdgeNearestPt;
								SnapResult.TargetType = ESceneSnapQueryTargetType::MeshEdge;
								SnapResult.TriSnapIndex = j;
							}
						}
					}

					// if we found a valid snap, return it
					if (SmallestAngle < Request.VisualAngleThresholdDegrees)
					{
						SnapResult.TargetActor = HitResult.Actor.Get();
						SnapResult.TargetComponent = HitResult.Component.Get();
						Results.Add(SnapResult);
						FoundResultCount++;
					}
				}
			}

		}

		return (FoundResultCount > 0);
	}


	//@ todo this are mirrored from GeometryProcessing, which is still experimental...replace w/ direct calls once GP component is standardized
	static float OpeningAngleDeg(FVector A, FVector B, const FVector& P)
	{
		A -= P;
		A.Normalize();
		B -= P;
		B.Normalize();
		float Dot = FMath::Clamp(FVector::DotProduct(A, B), -1.0f, 1.0f);
		return acos(Dot) * (180.0f / 3.141592653589f);
	}
	static FVector NearestSegmentPt(FVector A, FVector B, const FVector& P)
	{
		FVector Direction = (B - A);
		float Length = Direction.Size();
		Direction /= Length;
		float t = FVector::DotProduct((P - A), Direction);
		if (t >= Length)
		{
			return B;
		}
		if (t <= 0)
		{
			return A;
		}
		return A + t * Direction;
	}



	virtual UMaterialInterface* GetStandardMaterial(EStandardToolContextMaterials MaterialType) const
	{
		if (MaterialType == EStandardToolContextMaterials::VertexColorMaterial)
		{
			return EdMode->StandardVertexColorMaterial;
		}
		check(false);
		return nullptr;
	}

};




class FEdModeTransactionImpl : public IToolsContextTransactionsAPI
{
public:
	UInteractiveToolsContext* ToolsContext;
	UEdMode* EdMode;

	FEdModeTransactionImpl(UInteractiveToolsContext* Context, UEdMode* InEdMode)
	{
		ToolsContext = Context;
		EdMode = InEdMode;
	}

	virtual void DisplayMessage(const FText& Message, EToolMessageLevel Level) override
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
	}

	virtual void PostInvalidation() override
	{
		EdMode->PostInvalidation();
	}

	virtual void BeginUndoTransaction(const FText& Description) override
	{
		GEditor->BeginTransaction(Description);
	}

	virtual void EndUndoTransaction() override
	{
		GEditor->EndTransaction();
	}

	virtual void AppendChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description) override
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

		if (SelectionChange.ModificationType == ESelectedObjectsModificationType::Replace)
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
}


void UEdMode::OnModeUnregistered(FEditorModeID ModeID)
{
	if (ModeID == Info.ID)
	{
		// This should be synonymous with "delete this"
		Owner->DestroyMode(ModeID);
	}
}

void UEdMode::TerminateActiveToolsOnPIEStart()
{
	DeactivateAllActiveTools();
}
void UEdMode::TerminateActiveToolsOnSaveWorld()
{
	DeactivateAllActiveTools();
}

FRay UEdMode::GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY)
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



bool UEdMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	CurrentMouseState.Mouse.Position2D = FVector2D(x, y);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);

	return false;
}

bool UEdMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return false;
}

bool UEdMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	CurrentMouseState.Mouse.Position2D = FVector2D(x, y);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);
	FInputDeviceState InputState = CurrentMouseState;
	InputState.InputDevice = EInputDevices::Mouse;

	InputState.SetModifierKeyStates(
		ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
		ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());

	if (ToolsContext->InputRouter->HasActiveMouseCapture())
	{
		// This state occurs if InputBehavior did not release capture on mouse release. 
		// UMultiClickSequenceInputBehavior does this, eg for multi-click draw-polygon sequences.
		// It's not ideal though and maybe would be better done via multiple captures + hover...?
		ToolsContext->InputRouter->PostInputEvent(InputState);
	}
	else
	{
		ToolsContext->InputRouter->PostHoverInputEvent(InputState);
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
	if (InViewportClient->IsAltPressed())
	{
		return false;
	}

	FVector2D OldPosition = CurrentMouseState.Mouse.Position2D;
	CurrentMouseState.Mouse.Position2D = FVector2D(InMouseX, InMouseY);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(InViewportClient, InViewport, InMouseX, InMouseY);

	if (ToolsContext->InputRouter->HasActiveMouseCapture())
	{
		FInputDeviceState InputState = CurrentMouseState;
		InputState.InputDevice = EInputDevices::Mouse;
		InputState.SetModifierKeyStates(
			InViewportClient->IsShiftPressed(), InViewportClient->IsAltPressed(),
			InViewportClient->IsCtrlPressed(), InViewportClient->IsCmdPressed());
		InputState.Mouse.Delta2D = CurrentMouseState.Mouse.Position2D - OldPosition;
		ToolsContext->InputRouter->PostInputEvent(InputState);
		return true;
	}

	return false;
}

bool UEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	
	bool bHandled = false;

	// escape key cancels current tool
	if (Key == EKeys::Escape && Event == IE_Released)
	{
		if (ToolsContext->ToolManager->HasAnyActiveTool())
		{
			if (ToolsContext->ToolManager->HasActiveTool(EToolSide::Mouse))
			{
				ToolsContext->DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Cancel);
				RestoreEditorState();
			}
			return true;
		}
	}

	// enter key accepts current tool, or ends tool if it does not have accept state
	if (Key == EKeys::Enter && Event == IE_Released && ToolsContext->ToolManager->HasAnyActiveTool())
	{
		if (ToolsContext->ToolManager->HasActiveTool(EToolSide::Mouse))
		{
			if (ToolsContext->ToolManager->GetActiveTool(EToolSide::Mouse)->HasAccept())
			{
				if (ToolsContext->ToolManager->CanAcceptActiveTool(EToolSide::Mouse))
				{
					ToolsContext->DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Accept);
					RestoreEditorState();
					return true;
				}
			}
			else
			{
				ToolsContext->DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Completed);
				RestoreEditorState();
				return true;
			}
		}
	}

	// if alt is down we do not process mouse event
	if (ViewportClient->IsAltPressed())
	{
		return false;
	}

	if (Event == IE_Pressed || Event == IE_Released)
	{
		if (Key.IsMouseButton())
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

				FInputDeviceState InputState = CurrentMouseState;
				InputState.InputDevice = EInputDevices::Mouse;
				InputState.SetModifierKeyStates(
					ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
					ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());

				if (bIsLeftMouse)
				{
					InputState.Mouse.Left.SetStates(
						(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
					CurrentMouseState.Mouse.Left.bDown = (Event == IE_Pressed);
				}
				else if (bIsMiddleMouse)
				{
					InputState.Mouse.Middle.SetStates(
						(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
					CurrentMouseState.Mouse.Middle.bDown = (Event == IE_Pressed);
				}
				else
				{
					InputState.Mouse.Right.SetStates(
						(Event == IE_Pressed), (Event == IE_Pressed), (Event == IE_Released));
					CurrentMouseState.Mouse.Right.bDown = (Event == IE_Pressed);
				}

				ToolsContext->InputRouter->PostInputEvent(InputState);

				if (ToolsContext->InputRouter->HasActiveMouseCapture())
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
		else if (Key.IsGamepadKey())
		{
			// not supported yet
		}
		else if (Key.IsTouch())
		{
			// not supported yet
		}
		else if (Key.IsFloatAxis() || Key.IsVectorAxis())
		{
			// not supported yet
		}
		else    // is this definitely a keyboard key?
		{
			FInputDeviceState InputState;
			InputState.InputDevice = EInputDevices::Keyboard;
			InputState.SetModifierKeyStates(
				ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
				ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());
			InputState.Keyboard.ActiveKey.Button = Key;
			bool bPressed = (Event == IE_Pressed);
			InputState.Keyboard.ActiveKey.SetStates(bPressed, bPressed, !bPressed);
			ToolsContext->InputRouter->PostInputEvent(InputState);
		}

	}

	if(!bHandled)
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
	if (ToolsContext && ToolsContext->ToolManager->HasAnyActiveTool() == false)
	{
		return false;
	}

	bool bSkipDelete = false;

	// Test if any of the selected actors are AInternalToolFrameworkActor
	// subclasses. In this case we do not want to allow them to be deleted,
	// as generally this will cause problems for the Tool.
	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (int i = 0; i < SelectedActors->Num(); ++i)
	{
		UObject* SelectedActor = SelectedActors->GetSelectedObject(i);
		if (Cast<AInternalToolFrameworkActor>(SelectedActor) != nullptr)
		{
			bSkipDelete = true;
		}
	}

	return bSkipDelete;
}

void UEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	// give ToolsContext a chance to tick
	if (ToolsContext != nullptr)
	{
		ToolsContext->ToolManager->Tick(DeltaTime);
		ToolsContext->GizmoManager->Tick(DeltaTime);

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
			((FEdModeQueriesImpl*)QueriesAPI)->CacheCurrentViewState(ViewportClient);
		}
	}
}

void UEdMode::ActorSelectionChangeNotify()
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
	for (FSelectionIterator It(*Owner->GetSelectedActors()); It; ++It)
	{
		AActor* SelectedActor = CastChecked<AActor>(*It);
		SelectedActor->MarkComponentsRenderStateDirty();
	}

	bPendingDeletion = false;

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// initialize the adapter that attaches the ToolsContext to this FEdMode
	ToolsContext = NewObject<UInteractiveToolsContext>(GetTransientPackage(), TEXT("ToolsContext"), RF_Transient);
	TransactionAPI = new FEdModeTransactionImpl(ToolsContext, this);
	QueriesAPI = new FEdModeQueriesImpl(ToolsContext, this);
	AssetAPI = new FEditorToolAssetAPI();
	ToolsContext->Initialize(QueriesAPI, TransactionAPI);

	// enable auto invalidation in Editor, because invalidating for all hover and capture events is unpleasant
	ToolsContext->InputRouter->bAutoInvalidateOnHover = true;
	ToolsContext->InputRouter->bAutoInvalidateOnCapture = true;


	// set up standard materials
	StandardVertexColorMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/VertexColor"));

	BeginPIEDelegateHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool bSimulating)
	{
		TerminateActiveToolsOnPIEStart();
	});
	PreSaveWorldDelegateHandle = FEditorDelegates::PreSaveWorld.AddLambda([this](uint32 SaveFlags, UWorld* World)
	{
		TerminateActiveToolsOnSaveWorld();
	});
	bInvalidationPending = false;

	FEditorDelegates::EditorModeIDEnter.Broadcast(GetID());
	const bool bIsEnteringMode = true;
	Owner->BroadcastEditorModeIDChanged(GetID(), bIsEnteringMode);
}

void UEdMode::Exit()
{
	FEditorDelegates::BeginPIE.Remove(BeginPIEDelegateHandle);
	FEditorDelegates::PreSaveWorld.Remove(PreSaveWorldDelegateHandle);

	// auto-accept any in-progress tools
	DeactivateAllActiveTools();

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

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	const bool bIsEnteringMode = false;
	Owner->BroadcastEditorModeIDChanged(GetID(), bIsEnteringMode);
	FEditorDelegates::EditorModeIDExit.Broadcast(GetID());
	if (ToolsContext != nullptr)
	{
		ToolsContext->Shutdown();
		ToolsContext = nullptr;
	}
}


class FTempRenderContext : public IToolsContextRenderAPI
{
public:
	FPrimitiveDrawInterface* PDI;

	virtual FPrimitiveDrawInterface* GetPrimitiveDrawInterface() override
	{
		return PDI;
	}
};

void UEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	// give ToolsContext a chance to render
	if (ToolsContext != nullptr)
	{
		FTempRenderContext RenderContext;
		RenderContext.PDI = PDI;
		ToolsContext->ToolManager->Render(&RenderContext);
		ToolsContext->GizmoManager->Render(&RenderContext);
	}
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
				&& Actor->GetStaticMeshComponent()->GetStaticMesh()->RenderData)
			{
				FTransform ActorToWorld = Actor->ActorToWorld();
				Vertices.Empty();
				const FPositionVertexBuffer& VertexBuffer = Actor->GetStaticMeshComponent()->GetStaticMesh()->RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
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
	return false; 
}

bool UEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return true; 
}

AActor* UEdMode::GetFirstSelectedActorInstance() const
{
	return Owner->GetSelectedActors()->GetTop<AActor>();
}


void UEdMode::DeactivateAllActiveTools()
{
	ToolsContext->DeactivateAllActiveTools();
	RestoreEditorState();
}

UInteractiveToolManager* UEdMode::GetToolManager() const
{
	return ToolsContext->ToolManager;
}

bool UEdMode::IsSnapRotationEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->RotGridEnabled;
}

void UEdMode::PostInvalidation()
{
	bInvalidationPending = true;
}

void UEdMode::RestoreEditorState()
{
	if (bHaveSavedEditorState && !IsEngineExitRequested())
	{
		GCurrentLevelEditingViewportClient->EngineShowFlags.SetAntiAliasing(bSavedAntiAliasingState);
		bHaveSavedEditorState = false;
	}
}
