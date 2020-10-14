// Copyright Epic Games, Inc. All Rights Reserved.


#include "EdModeInteractiveToolsContext.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "IAssetViewport.h"
#include "Math/Rotator.h"
#include "Misc/AssertionMacros.h"
#include "SLevelViewport.h"

#include "Modules/ModuleManager.h"
#include "ShowFlags.h"				// for EngineShowFlags
#include "Engine/Selection.h"
#include "Misc/ITransaction.h"
#include "ScopedTransaction.h"
#include "Materials/Material.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

#include "ToolContextInterfaces.h"
#include "Tools/EditorToolAssetAPI.h"
#include "Tools/EditorComponentSourceFactory.h"
#include "InteractiveToolObjects.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "EditorModeManager.h"
#include "EdMode.h"

#include "BaseGizmos/GizmoRenderingUtil.h"
#include "UnrealClient.h"

//#define ENABLE_DEBUG_PRINTING

static float SnapToIncrement(float fValue, float fIncrement, float offset = 0)
{
	if (!FMath::IsFinite(fValue))
	{
		return 0;
	}
	fValue -= offset;
	float sign = FMath::Sign(fValue);
	fValue = FMath::Abs(fValue);
	int nInc = (int)(fValue / fIncrement);
	float fRem = (float)fmod(fValue, fIncrement);
	if (fRem > fIncrement / 2)
	{
		++nInc;
	}
	return sign * (float)nInc * fIncrement + offset;
}




static bool IsVisibleObjectHit_Internal(const FHitResult& HitResult)
{
	AActor* Actor = HitResult.GetActor();
	if (Actor != nullptr && (Actor->IsHidden() || Actor->IsHiddenEd()) )
	{
		return false;
	}
	UPrimitiveComponent* Component = HitResult.GetComponent();
	if (Component != nullptr && (Component->IsVisible() == false && Component->IsVisibleInEditor() == false))
	{
		return false;
	}
	return true;
}

static bool FindNearestVisibleObjectHit_Internal(UWorld* World, FHitResult& HitResultOut, const FVector& Start, const FVector& End, bool bIsSceneGeometrySnapQuery)
{
	FCollisionObjectQueryParams ObjectQueryParams(FCollisionObjectQueryParams::AllObjects);
	FCollisionQueryParams QueryParams = FCollisionQueryParams::DefaultQueryParam;
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnFaceIndex = bIsSceneGeometrySnapQuery;

	TArray<FHitResult> OutHits;
	if (World->LineTraceMultiByObjectType(OutHits, Start, End, ObjectQueryParams, QueryParams) == false)
	{
		return false;
	}

	float NearestVisible = TNumericLimits<float>::Max();
	for (const FHitResult& CurResult : OutHits)
	{
		if (CurResult.Distance < NearestVisible)
		{
			if (IsVisibleObjectHit_Internal(CurResult))
			{
				HitResultOut = CurResult;
				NearestVisible = CurResult.Distance;
			}
		}
	}

	return NearestVisible < TNumericLimits<float>::Max();
}






class FEdModeToolsContextQueriesImpl : public IToolsContextQueriesAPI
{
public:
	UEdModeInteractiveToolsContext* ToolsContext;
	FEditorModeTools* EditorModeManager;

	FViewCameraState CachedViewState;
	FEditorViewportClient* CachedViewportClient;

	FEdModeToolsContextQueriesImpl(UEdModeInteractiveToolsContext* Context, FEditorModeTools* InEditorModeManager)
	{
		ToolsContext = Context;
		EditorModeManager = InEditorModeManager;
	}

	void CacheCurrentViewState(FEditorViewportClient* ViewportClient)
	{
		CachedViewportClient = ViewportClient;
		FViewportCameraTransform ViewTransform = ViewportClient->GetViewTransform();
		CachedViewState.bIsOrthographic = ViewportClient->IsOrtho();
		CachedViewState.Position = ViewTransform.GetLocation();
		CachedViewState.HorizontalFOVDegrees = ViewportClient->ViewFOV;
		CachedViewState.AspectRatio = ViewportClient->AspectRatio;

		// ViewTransform rotation is only initialized for perspective!
		if (CachedViewState.bIsOrthographic == false)
		{
			// if using Orbit camera, the rotation in the ViewTransform is not the current camera rotation, it
			// is set to a different rotation based on the Orbit. So we have to convert back to camera rotation.
			FRotator ViewRotation = (ViewportClient->bUsingOrbitCamera) ? 
				ViewTransform.ComputeOrbitMatrix().InverseFast().Rotator()  :   ViewTransform.GetRotation();

			CachedViewState.Orientation = ViewRotation.Quaternion();
		}
		else
		{
			// These rotations are based on hardcoded values in EditorViewportClient.cpp, see switches in FEditorViewportClient::CalcSceneView and FEditorViewportClient::Draw
			switch (ViewportClient->ViewportType)
			{
			case LVT_OrthoXY:
				CachedViewState.Orientation = FQuat(FRotator(-90.0f, -90.0f, 0.0f));
				break;
			case LVT_OrthoNegativeXY:
				CachedViewState.Orientation = FQuat(FRotator(90.0f, 90.0f, 0.0f));
				break;
			case LVT_OrthoXZ:
				CachedViewState.Orientation = FQuat(FRotator(0.0f, -90.0f, 0.0f));
				break;
			case LVT_OrthoNegativeXZ:
				CachedViewState.Orientation = FQuat(FRotator(0.0f, 90.0f, 0.0f));
				break;
			case LVT_OrthoYZ:
				CachedViewState.Orientation = FQuat(FRotator(0.0f, 0.0f, 0.0f));
				break;
			case LVT_OrthoNegativeYZ:
				CachedViewState.Orientation = FQuat(FRotator(0.0f, 180.0f, 0.0f));
				break;
			default:
				CachedViewState.Orientation = FQuat::Identity;
			}

			CachedViewState.OrthoWorldCoordinateWidth = ViewportClient->GetOrthoUnitsPerPixel(ViewportClient->Viewport) * ViewportClient->Viewport->GetSizeXY().X;
		}

		CachedViewState.bIsVR = false;
	}

	virtual void GetCurrentSelectionState(FToolBuilderState& StateOut) const override
	{
		StateOut.ToolManager = ToolsContext->ToolManager;
		StateOut.GizmoManager = ToolsContext->GizmoManager;
		StateOut.World = EditorModeManager->GetWorld();
		EditorModeManager->GetSelectedActors()->GetSelectedObjects(StateOut.SelectedActors);
		EditorModeManager->GetSelectedComponents()->GetSelectedObjects(StateOut.SelectedComponents);
	}

	virtual void GetCurrentViewState(FViewCameraState& StateOut) const override
	{
		StateOut = CachedViewState;
	}

	virtual EToolContextCoordinateSystem GetCurrentCoordinateSystem() const override
	{
		ECoordSystem CoordSys = EditorModeManager->GetCoordSystem();
		return (CoordSys == COORD_World) ? EToolContextCoordinateSystem::World : EToolContextCoordinateSystem::Local;
	}

	bool ExecuteSceneSnapQueryRotation(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
	{
		if ((Request.TargetTypes & ESceneSnapQueryTargetType::Grid) != ESceneSnapQueryTargetType::None)
		{
			FRotator Rotator ( Request.DeltaRotation );
			FRotator RotGrid = Request.RotGridSize.Get(GEditor->GetRotGridSize());
			Rotator = Rotator.GridSnap( RotGrid );

			FSceneSnapQueryResult SnapResult;
			SnapResult.TargetType = ESceneSnapQueryTargetType::Grid;
			SnapResult.DeltaRotation = Rotator.Quaternion();
			Results.Add(SnapResult);
			return true;
		}
		return false;
	}

	bool ExecuteSceneSnapQueryPosition(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const
	{
		int FoundResultCount = 0;

		if ((Request.TargetTypes & ESceneSnapQueryTargetType::Grid) != ESceneSnapQueryTargetType::None)
		{
			FSceneSnapQueryResult SnapResult;
			SnapResult.TargetType = ESceneSnapQueryTargetType::Grid;

			float SnapSize = GEditor->GetGridSize();
			FVector GridSize = Request.GridSize.Get(FVector(SnapSize, SnapSize, SnapSize));

			SnapResult.Position.X = SnapToIncrement(Request.Position.X, GridSize.X);
			SnapResult.Position.Y = SnapToIncrement(Request.Position.Y, GridSize.Y);
			SnapResult.Position.Z = SnapToIncrement(Request.Position.Z, GridSize.Z);

			Results.Add(SnapResult);
			FoundResultCount++;
		}

		//
		// Run a snap query by casting ray into the world.
		// If a hit is found, we look up what triangle was hit, and then test its vertices and edges
		//

		// cast ray into world
		FVector RayStart = CachedViewState.Position;
		FVector RayDirection = Request.Position - RayStart; RayDirection.Normalize();
		FVector RayEnd = RayStart + HALF_WORLD_MAX * RayDirection;
		FHitResult HitResult;
		bool bHitWorld = FindNearestVisibleObjectHit_Internal(EditorModeManager->GetWorld(), HitResult, RayStart, RayEnd, true);
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
					FStaticMeshLODResources& LOD = StaticMesh->GetRenderData()->LODResources[0];
					FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
					int32 TriIdx = 3 * HitResult.FaceIndex;
					FVector Positions[3];
					Positions[0] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx]);
					Positions[1] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx+1]);
					Positions[2] = LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Indices[TriIdx+2]);

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
					if ( (Request.TargetTypes & ESceneSnapQueryTargetType::MeshVertex) != ESceneSnapQueryTargetType::None)
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
					if ( ((Request.TargetTypes & ESceneSnapQueryTargetType::MeshEdge) != ESceneSnapQueryTargetType::None) &&
						 (SnapResult.TargetType != ESceneSnapQueryTargetType::MeshVertex) )
					{
						for (int j = 0; j < 3; ++j)
						{
							FVector EdgeNearestPt = NearestSegmentPt(Positions[j], Positions[(j+1)%3], Request.Position);
							VisualAngle = OpeningAngleDeg(Request.Position, EdgeNearestPt, RayStart);
							if (VisualAngle < SmallestAngle )
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

	virtual bool ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const override
	{
		switch (Request.RequestType)
		{
		case ESceneSnapQueryType::Position:
			return ExecuteSceneSnapQueryPosition(Request, Results);
			break;
		case ESceneSnapQueryType::Rotation:
			return ExecuteSceneSnapQueryRotation(Request, Results);
			break;
		default:
			check(!"Only Position and Rotation Snap Queries are supported");
		}
		return false;
	}

	//@ todo this are mirrored from GeometryProcessing, which is still experimental...replace w/ direct calls once GP component is standardized
	static float OpeningAngleDeg(FVector A, FVector B, const FVector& P)
	{
		A -= P;
		A.Normalize();
		B -= P;
		B.Normalize();
		float Dot = FMath::Clamp(FVector::DotProduct(A,B), -1.0f, 1.0f);
		return acos(Dot) * (180.0f / 3.141592653589f);
	}

	static FVector NearestSegmentPt(FVector A, FVector B, const FVector& P)
	{
		FVector Direction = (B - A);
		float Length = Direction.Size();
		Direction /= Length;
		float t = FVector::DotProduct( (P - A), Direction);
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
			return ToolsContext->StandardVertexColorMaterial;
		}
		check(false);
		return nullptr;
	}

	virtual HHitProxy* GetHitProxy(int32 X, int32 Y) const
	{
		if (CachedViewportClient && CachedViewportClient->Viewport)
		{
			return CachedViewportClient->Viewport->GetHitProxy(X, Y);
		}
		return nullptr;
	}
};

class FEdModeToolsContextTransactionImpl : public IToolsContextTransactionsAPI
{
public:
	UEdModeInteractiveToolsContext* ToolsContext;
	FEditorModeTools* EditorModeManager;

	FEdModeToolsContextTransactionImpl(UEdModeInteractiveToolsContext* Context, FEditorModeTools* InEditorModeManager)
	{
		ToolsContext = Context;
		EditorModeManager = InEditorModeManager;
	}


	virtual void DisplayMessage(const FText& Message, EToolMessageLevel Level) override
	{
		if (Level == EToolMessageLevel::UserNotification)
		{
			ToolsContext->PostToolNotificationMessage(Message);
		}
		if (Level == EToolMessageLevel::UserWarning)
		{
			ToolsContext->PostToolWarningMessage(Message);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
		}
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
	QueriesAPI = nullptr;
	TransactionAPI = nullptr;
	AssetAPI = nullptr;
}



void UEdModeInteractiveToolsContext::Initialize(IToolsContextQueriesAPI* QueriesAPIIn, IToolsContextTransactionsAPI* TransactionsAPIIn)
{
	UInteractiveToolsContext::Initialize(QueriesAPIIn, TransactionsAPIIn);

	BeginPIEDelegateHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool bSimulating)
	{
		TerminateActiveToolsOnPIEStart();
	});
	PreSaveWorldDelegateHandle = FEditorDelegates::PreSaveWorld.AddLambda([this](uint32 SaveFlags, UWorld* World)
	{
		TerminateActiveToolsOnSaveWorld();
	});

	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	WorldTearDownDelegateHandle = LevelEditor.OnMapChanged().AddLambda([this](UWorld* World, EMapChangeType ChangeType)
	{
		if (ChangeType == EMapChangeType::TearDownWorld)
		{
			TerminateActiveToolsOnWorldTearDown();
		}
	});

	ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager*, UInteractiveTool*)
	{
		RestoreEditorState();
	});

	// if viewport clients change we will discard our overrides as we aren't sure what happened
	ViewportClientListChangedHandle = GEditor->OnViewportClientListChanged().AddLambda([this]()
	{
		RestoreEditorState();
	});


	// If user right-press-drags, this enables "fly mode" in the main viewport, and in that mode the QEWASD keys should
	// be used for flying control. However the EdMode InputKey/etc system doesn't enforce any of this, we can still also
	// get that mouse input and hotkeys. So we register a dummy behavior that captures all right-mouse dragging, and
	// in that mode we set bInFlyMode=true, so that Modes based on this Context will know to skip hotkey processing
	ULocalClickDragInputBehavior* RightMouseBehavior = NewObject<ULocalClickDragInputBehavior>(this);
	RightMouseBehavior->CanBeginClickDragFunc = [](const FInputDeviceRay& PressPos) { return  FInputRayHit(0); };
	RightMouseBehavior->OnClickPressFunc = [this](const FInputDeviceRay&) { bInFlyMode = true; };
	RightMouseBehavior->OnClickReleaseFunc = [this](const FInputDeviceRay&) { bInFlyMode = false; };
	RightMouseBehavior->OnTerminateFunc = [this]() { bInFlyMode = false; };
	RightMouseBehavior->SetDefaultPriority(FInputCapturePriority(0));
	RightMouseBehavior->SetUseRightMouseButton();
	RightMouseBehavior->Initialize();
	InputRouter->RegisterBehavior(RightMouseBehavior, this);

	InvalidationTimestamp = 0;
}

void UEdModeInteractiveToolsContext::Shutdown()
{
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditor.OnMapChanged().Remove(WorldTearDownDelegateHandle);
	FEditorDelegates::BeginPIE.Remove(BeginPIEDelegateHandle);
	FEditorDelegates::PreSaveWorld.Remove(PreSaveWorldDelegateHandle);
	GEditor->OnViewportClientListChanged().Remove(ViewportClientListChangedHandle);

	// auto-accept any in-progress tools
	DeactivateAllActiveTools();

	UInteractiveToolsContext::Shutdown();
}

void UEdModeInteractiveToolsContext::InitializeContextFromEdMode(FEdMode* EditorModeIn, IToolsContextAssetAPI* UseAssetAPI)
{
	check(EditorModeIn);
	InitializeContextWithEditorModeManager(EditorModeIn->GetModeManager(), UseAssetAPI);
}

void UEdModeInteractiveToolsContext::InitializeContextWithEditorModeManager(FEditorModeTools* InEditorModeManager,
	IToolsContextAssetAPI* UseAssetAPI)
{
	check(InEditorModeManager);
	EditorModeManager = InEditorModeManager;

	this->TransactionAPI = new FEdModeToolsContextTransactionImpl(this, InEditorModeManager);
	this->QueriesAPI = new FEdModeToolsContextQueriesImpl(this, InEditorModeManager);
	this->AssetAPI = (UseAssetAPI != nullptr) ? UseAssetAPI: new FEditorToolAssetAPI();

	Initialize(QueriesAPI, TransactionAPI);

	// enable auto invalidation in Editor, because invalidating for all hover and capture events is unpleasant
	this->InputRouter->bAutoInvalidateOnHover = true;
	this->InputRouter->bAutoInvalidateOnCapture = true;

	// set up standard materials
	StandardVertexColorMaterial = GEngine->VertexColorMaterial;
}


void UEdModeInteractiveToolsContext::ShutdownContext()
{
	Shutdown();

	OnToolNotificationMessage.Clear();
	OnToolWarningMessage.Clear();

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
}


void UEdModeInteractiveToolsContext::TerminateActiveToolsOnPIEStart()
{
	DeactivateAllActiveTools();
}
void UEdModeInteractiveToolsContext::TerminateActiveToolsOnSaveWorld()
{
	DeactivateAllActiveTools();
}
void UEdModeInteractiveToolsContext::TerminateActiveToolsOnWorldTearDown()
{
	DeactivateAllActiveTools();
}

void UEdModeInteractiveToolsContext::PostInvalidation()
{
	InvalidationTimestamp++;
}

void UEdModeInteractiveToolsContext::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	// invalidate this viewport if it's timestamp is not current
	const int32* FoundTimestamp = InvalidationMap.Find(ViewportClient);
	if (FoundTimestamp == nullptr)
	{
		ViewportClient->Invalidate(false, false);
		InvalidationMap.Add(ViewportClient, InvalidationTimestamp);
	}
	if (FoundTimestamp != nullptr && *FoundTimestamp < InvalidationTimestamp)
	{
		ViewportClient->Invalidate(false, false);
		InvalidationMap[ViewportClient] = InvalidationTimestamp;
	}

	// This Tick() is called for every ViewportClient, however we only want to Tick the ToolManager and GizmoManager
	// once, for the 'Active'/Focused Viewport, so early-out here
	if (ViewportClient != EditorModeManager->GetFocusedViewportClient())
	{
		return;
	}

	if ( PendingToolShutdownType )
	{
		UInteractiveToolsContext::EndTool(EToolSide::Mouse, *PendingToolShutdownType);
		PendingToolShutdownType.Reset();
	}
	if ( PendingToolToStart )
	{
		if (UInteractiveToolsContext::StartTool(EToolSide::Mouse, *PendingToolToStart))
		{
			SetEditorStateForTool();
		}
		PendingToolToStart.Reset();
	}

	// Cache current camera state from this Viewport in the ContextQueries, which we will use for things like snapping/etc that
	// is computed by the Tool and Gizmo Tick()s
	// (This is not necessarily correct for Hover, because we might be Hovering over a different Viewport than the Active one...)
	((FEdModeToolsContextQueriesImpl*)this->QueriesAPI)->CacheCurrentViewState(ViewportClient);

	// tick our stuff
	ToolManager->Tick(DeltaTime);
	GizmoManager->Tick(DeltaTime);
}



class FEdModeTempRenderContext : public IToolsContextRenderAPI
{
public:
	FPrimitiveDrawInterface* PDI;
	const FSceneView* SceneView;
	FViewCameraState ViewCameraState;
	EViewInteractionState ViewInteractionState;

	FEdModeTempRenderContext(const FSceneView* View, FViewport* Viewport, FEditorViewportClient* ViewportClient, FPrimitiveDrawInterface* DrawInterface, EViewInteractionState ViewInteractionState)
		:PDI(DrawInterface), SceneView(View), ViewInteractionState(ViewInteractionState)
	{
		CacheCurrentViewState(Viewport, ViewportClient);
	}

	virtual FPrimitiveDrawInterface* GetPrimitiveDrawInterface() override
	{
		return PDI;
	}

	virtual const FSceneView* GetSceneView() override
	{
		return SceneView;
	}

	virtual FViewCameraState GetCameraState() override
	{
		return ViewCameraState;
	}

	virtual EViewInteractionState GetViewInteractionState() override
	{
		return ViewInteractionState;
	}

	void CacheCurrentViewState(FViewport* Viewport, FEditorViewportClient* ViewportClient)
	{
		FViewportCameraTransform ViewTransform = ViewportClient->GetViewTransform();
		ViewCameraState.bIsOrthographic = ViewportClient->IsOrtho();
		ViewCameraState.Position = ViewTransform.GetLocation();
		ViewCameraState.HorizontalFOVDegrees = ViewportClient->ViewFOV;
		ViewCameraState.AspectRatio = ViewportClient->AspectRatio;

		// ViewTransform rotation is only initialized for perspective!
		if (ViewCameraState.bIsOrthographic == false)
		{
			// if using Orbit camera, the rotation in the ViewTransform is not the current camera rotation, it
			// is set to a different rotation based on the Orbit. So we have to convert back to camera rotation.
			FRotator ViewRotation = (ViewportClient->bUsingOrbitCamera) ?
				ViewTransform.ComputeOrbitMatrix().InverseFast().Rotator() : ViewTransform.GetRotation();

			ViewCameraState.Orientation = ViewRotation.Quaternion();
		}
		else
		{
			// These rotations are based on hardcoded values in EditorViewportClient.cpp, see switches in FEditorViewportClient::CalcSceneView and FEditorViewportClient::Draw
			switch (ViewportClient->ViewportType)
			{
			case LVT_OrthoXY:
				ViewCameraState.Orientation = FQuat(FRotator(-90.0f, -90.0f, 0.0f));
				break;
			case LVT_OrthoNegativeXY:
				ViewCameraState.Orientation = FQuat(FRotator(90.0f, 90.0f, 0.0f));
				break;
			case LVT_OrthoXZ:
				ViewCameraState.Orientation = FQuat(FRotator(0.0f, -90.0f, 0.0f));
				break;
			case LVT_OrthoNegativeXZ:
				ViewCameraState.Orientation = FQuat(FRotator(0.0f, 90.0f, 0.0f));
				break;
			case LVT_OrthoYZ:
				ViewCameraState.Orientation = FQuat(FRotator(0.0f, 0.0f, 0.0f));
				break;
			case LVT_OrthoNegativeYZ:
				ViewCameraState.Orientation = FQuat(FRotator(0.0f, 180.0f, 0.0f));
				break;
			default:
				ViewCameraState.Orientation = FQuat::Identity;
			}

			ViewCameraState.OrthoWorldCoordinateWidth = ViewportClient->GetOrthoUnitsPerPixel(ViewportClient->Viewport) * ViewportClient->Viewport->GetSizeXY().X;
		}

		ViewCameraState.bIsVR = false;
	}

};


void UEdModeInteractiveToolsContext::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	// tools framework cannot use HitProxy so skip these calls
	if (PDI->IsHitTesting())
	{
		return;
	}

	// THIS IS NOT SAFE!! However it appears that (1) it is only possible to get certain info from the EditorViewportClient,
	// but (2) there is no way to know if a FViewportClient is an FEditorViewportClient. Currently this ::Render() function
	// is only intended to be called by FEdMode/UEdMode::Render(), and their ::Render() calls are only called by the
	// FEditorViewportClient, which passes it's own Viewport down. So, this cast should be valid (for now)
	FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());

	// Update the global currently-focused FSceneView variable, which GizmoArrowComponent and friends will
	// use to know when they are seeing the SceneView they should use to recalculate their size/visibility/etc.
	// This could go away if we could move that functionality out of the RenderProxy (tricky given that it needs
	// to respond to each FSceneView...)
	if (ViewportClient == EditorModeManager->GetHoveredViewportClient())
	{
		// This locks internally and so no need to do on Render thread, and possibly better to do immediately (?)
		//ENQUEUE_RENDER_COMMAND(BlerBlerBler)( [View](FRHICommandListImmediate& RHICmdList) {
			GizmoRenderingUtil::SetGlobalFocusedEditorSceneView(View);
		//});
	}

	// Render Tool and Gizmos
	const FEditorViewportClient* Focused = EditorModeManager->GetFocusedViewportClient();
	const FEditorViewportClient* Hovered = EditorModeManager->GetHoveredViewportClient();
	EViewInteractionState InteractionState = EViewInteractionState::None;
	if (ViewportClient == Focused )
	{
		InteractionState |= EViewInteractionState::Focused;
	}
	if (ViewportClient == Hovered )
	{
		InteractionState |= EViewInteractionState::Hovered;
	}
	FEdModeTempRenderContext RenderContext(View, Viewport, ViewportClient, PDI, InteractionState);
	ToolManager->Render(&RenderContext);
	GizmoManager->Render(&RenderContext);
}

void UEdModeInteractiveToolsContext::DrawHUD(FViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View, FCanvas* Canvas)
{
	FEditorViewportClient* EditorViewportClient = static_cast<FEditorViewportClient*>(Viewport->GetClient());
	const FViewportClient* Focused = EditorModeManager->GetFocusedViewportClient();
	const FViewportClient* Hovered = EditorModeManager->GetHoveredViewportClient();
	EViewInteractionState InteractionState = EViewInteractionState::None;
	if (ViewportClient == Focused )
	{
		InteractionState |= EViewInteractionState::Focused;
	}
	if (ViewportClient == Hovered )
	{
		InteractionState |= EViewInteractionState::Hovered;
	}
	FEdModeTempRenderContext RenderContext(View, Viewport, EditorViewportClient, nullptr /*PDI*/, InteractionState);
	ToolManager->DrawHUD(Canvas, &RenderContext);
	GizmoManager->DrawHUD(Canvas, &RenderContext);
}


bool UEdModeInteractiveToolsContext::ProcessEditDelete()
{
	if (ToolManager->HasAnyActiveTool() == false)
	{
		return false;
	}

	bool bSkipDelete = false;

	USelection* SelectedActors = GEditor->GetSelectedActors();
	for (int i = 0; i < SelectedActors->Num() && bSkipDelete == false; ++i)
	{
		UObject* SelectedActor = SelectedActors->GetSelectedObject(i);

		// If any of the selected actors are AInternalToolFrameworkActor, we do not want to allow them to be deleted,
		// as generally this will cause problems for the Tool.
		if ( Cast<AInternalToolFrameworkActor>(SelectedActor) != nullptr)
		{
			bSkipDelete = true;
		}

		// If any Components of selected Actors implement UToolFrameworkComponent, we disable delete (for now).
		// (Currently Sculpt and a few other Modeling Tools attach their preview mesh components to the selected Actor)
		AActor* Actor = Cast<AActor>(SelectedActor);
		if (Actor != nullptr)
		{
			const TSet<UActorComponent*>& Components = Actor->GetComponents();
			for (const UActorComponent* Component : Components)
			{
				if ( Component->Implements<UToolFrameworkComponent>() )
				{
					bSkipDelete = true;
				}
			}
		}

	}

	return bSkipDelete;
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
				DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Cancel);
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
					DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Accept);
					return true;
				}
			}
			else
			{
				DeactivateActiveTool(EToolSide::Mouse, EToolShutdownType::Completed);
				return true;
			}
		}
	}

	// This is true if we are using the fly camera controls (ie right-mouse possibly + WASD). 
	// Those controls do *not* capture the mouse and so we still get the events, and we need to ignore them.
	// Note that it is possible to enter fly camera by holding right-mouse, then hold another button and release right-mouse,
	// and that stays in fly mode, so we cannot rely on right-mouse state alone.
	if (ViewportClient->IsMovingCamera())
	{
		// We are still in this state when user releases right-mouse button but is still holding down left-mouse.
		// In that state we need to allow the InputRouter to see the event, so that the right-mouse-capture behavior can release
		bool bIsReleaseRightNavButton = Key.IsMouseButton() && (Key == EKeys::RightMouseButton) && (Event == IE_Released);
		if (bIsReleaseRightNavButton == false)
		{
			return false;
		}
	}

	// convert doubleclick events to pressed, for now...this is a hack!
	if (Event == IE_DoubleClick)
	{
		Event = IE_Pressed;
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
				// if alt is down and we are not capturing, somewhere higher in the ViewportClient/EdMode stack 
				// is going to start doing alt+mouse camera manipulation. So we should ignore this mouse event.
				if (ViewportClient->IsAltPressed() && InputRouter->HasActiveMouseCapture() == false)
				{
					return false;
				}
				// TODO: This should no longer be necessary: test and remove.
				// This is a special-case hack for UMultiClickSequenceInputBehavior, because it holds capture across multiple
				// mouse clicks, which prevents alt+mouse navigation from working between clicks (very annoying in draw polygon).
				// Remove this special-case once that tool is fixed to use CollectSurfacePathMechanic instead
				if (Event == IE_Pressed && bIsLeftMouse && ViewportClient->IsAltPressed() && InputRouter->HasActiveMouseCapture())
				{
					return false;
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

				InputRouter->PostInputEvent(InputState);

				if (InputRouter->HasActiveMouseCapture() && bInFlyMode == false)
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
		else if (Key.IsAnalog())
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
			InputRouter->PostInputEvent(InputState);
		}

	}

	return bHandled;
}



bool UEdModeInteractiveToolsContext::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("MOUSE ENTER"));
#endif

	CurrentMouseState.Mouse.Position2D = FVector2D(x, y);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);

	return false;
}


bool UEdModeInteractiveToolsContext::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("HOVER %p"), ViewportClient);
#endif

	CurrentMouseState.Mouse.Position2D = FVector2D(x, y);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(ViewportClient, Viewport, x, y);
	FInputDeviceState InputState = CurrentMouseState;
	InputState.InputDevice = EInputDevices::Mouse;

	InputState.SetModifierKeyStates(
		ViewportClient->IsShiftPressed(), ViewportClient->IsAltPressed(),
		ViewportClient->IsCtrlPressed(), ViewportClient->IsCmdPressed());

	if (InputRouter->HasActiveMouseCapture())
	{
		// TODO: This should no longer be necessary: test and remove.
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
	// capture tracking if we have an active tool
	if (ToolManager->HasActiveTool(EToolSide::Mouse))
	{
#ifdef ENABLE_DEBUG_PRINTING
		UE_LOG(LogTemp, Warning, TEXT("BEGIN TRACKING"));
#endif
		return true;
	}
	return false;
}

bool UEdModeInteractiveToolsContext::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	// if alt is down we will not allow client to see this event
	if (InViewportClient->IsAltPressed())
	{
		return false;
	}

	FVector2D OldPosition = CurrentMouseState.Mouse.Position2D;
	CurrentMouseState.Mouse.Position2D = FVector2D(InMouseX, InMouseY);
	CurrentMouseState.Mouse.WorldRay = GetRayFromMousePos(InViewportClient, InViewport, InMouseX, InMouseY);

	if (InputRouter->HasActiveMouseCapture())
	{
#ifdef ENABLE_DEBUG_PRINTING
		UE_LOG(LogTemp, Warning, TEXT("CAPTURED MOUSE MOVE"));
#endif

		FInputDeviceState InputState = CurrentMouseState;
		InputState.InputDevice = EInputDevices::Mouse;
		InputState.SetModifierKeyStates(
			InViewportClient->IsShiftPressed(), InViewportClient->IsAltPressed(),
			InViewportClient->IsCtrlPressed(), InViewportClient->IsCmdPressed());
		InputState.Mouse.Delta2D = CurrentMouseState.Mouse.Position2D - OldPosition;
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

	// unlock flight camera
	InViewportClient->bLockFlightCamera = false;

	return true;
}

FRay UEdModeInteractiveToolsContext::GetRayFromMousePos(FEditorViewportClient* ViewportClient, FViewport* Viewport, int MouseX, int MouseY)
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags).SetRealtimeUpdate(ViewportClient->IsRealtime()));		// why SetRealtimeUpdate here??
	// this View is deleted by the FSceneViewFamilyContext destructor
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, (FEditorViewportClient*)Viewport->GetClient(), MouseX, MouseY);

	FVector RayOrigin = MouseViewportRay.GetOrigin();
	FVector RayDirection = MouseViewportRay.GetDirection();

	// in Ortho views, the RayOrigin appears to be completely arbitrary, in some views it is on the view plane,
	// others it moves back/forth with the OrthoZoom. Translate by a large amount here in hopes of getting
	// ray origin "outside" the scene (which is a disaster for numerical precision !! ... )
	if (ViewportClient->IsOrtho())
	{
		RayOrigin -= 0.1 * HALF_WORLD_MAX * RayDirection;
	}

	return FRay(RayOrigin, RayDirection, true);
}

FRay UEdModeInteractiveToolsContext::GetLastWorldRay() const
{
	return CurrentMouseState.Mouse.WorldRay;
}

bool UEdModeInteractiveToolsContext::CanStartTool(const FString& ToolTypeIdentifier) const
{
	return UInteractiveToolsContext::CanStartTool(EToolSide::Mouse, ToolTypeIdentifier);
}

bool UEdModeInteractiveToolsContext::ActiveToolHasAccept() const
{
	return  UInteractiveToolsContext::ActiveToolHasAccept(EToolSide::Mouse);
}

bool UEdModeInteractiveToolsContext::CanAcceptActiveTool() const
{
	return UInteractiveToolsContext::CanAcceptActiveTool(EToolSide::Mouse);
}

bool UEdModeInteractiveToolsContext::CanCancelActiveTool() const
{
	return UInteractiveToolsContext::CanCancelActiveTool(EToolSide::Mouse);
}

bool UEdModeInteractiveToolsContext::CanCompleteActiveTool() const
{
	return UInteractiveToolsContext::CanCompleteActiveTool(EToolSide::Mouse);
}

void UEdModeInteractiveToolsContext::StartTool(const FString& ToolTypeIdentifier)
{
	FString LocalIdentifier(ToolTypeIdentifier);
	PendingToolToStart = LocalIdentifier;
	PostInvalidation();
}

void UEdModeInteractiveToolsContext::EndTool(EToolShutdownType ShutdownType)
{
	PendingToolShutdownType = ShutdownType;
	PostInvalidation();
}



void UEdModeInteractiveToolsContext::DeactivateActiveTool(EToolSide WhichSide, EToolShutdownType ShutdownType)
{
	UInteractiveToolsContext::DeactivateActiveTool(WhichSide, ShutdownType);
	RestoreEditorState();
}

void UEdModeInteractiveToolsContext::DeactivateAllActiveTools()
{
	UInteractiveToolsContext::DeactivateAllActiveTools();
	RestoreEditorState();
}

void UEdModeInteractiveToolsContext::SetEditorStateForTool()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<IAssetViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<IAssetViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetAssetViewportClient();
				Viewport.EnableOverrideEngineShowFlags([](FEngineShowFlags& Flags)
				{
					Flags.SetTemporalAA(false);
					Flags.SetMotionBlur(false);
					// disable this as depending on fixed exposure settings the entire scene may turn black
					//Flags.SetEyeAdaptation(false);
				});
			}
		}
	}
}

void UEdModeInteractiveToolsContext::RestoreEditorState()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor  = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<IAssetViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<IAssetViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetAssetViewportClient();
				Viewport.DisableOverrideEngineShowFlags();
			}
		}
	}
}
