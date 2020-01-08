// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorMode.h"
#include "ModelingToolsEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorViewportClient.h"

//#include "SingleClickTool.h"
//#include "MeshSurfacePointTool.h"
//#include "MeshVertexDragTool.h"
#include "DynamicMeshSculptTool.h"
#include "EditMeshPolygonsTool.h"
#include "ConvertToPolygonsTool.h"
#include "AddPrimitiveTool.h"
#include "SmoothMeshTool.h"
#include "RemeshMeshTool.h"
#include "SimplifyMeshTool.h"
#include "MeshInspectorTool.h"
#include "WeldMeshEdgesTool.h"
#include "DrawPolygonTool.h"
#include "ShapeSprayTool.h"
#include "MergeMeshesTool.h"
#include "VoxelCSGMeshesTool.h"
#include "PlaneCutTool.h"
#include "PolygonOnMeshTool.h"
#include "DisplaceMeshTool.h"
#include "MeshSpaceDeformerTool.h"
#include "EditNormalsTool.h"
#include "AttributeEditorTool.h"
#include "TransformMeshesTool.h"
#include "MeshSelectionTool.h"
#include "UVProjectionTool.h"

#include "ParameterizeMeshTool.h"

#include "EditorModeManager.h"

// viewport interaction support
#include "ViewportInteractor.h"
#include "ActorViewportTransformable.h"
#include "ViewportWorldInteraction.h"
#include "IViewportInteractionModule.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"

#include "ModelingToolsActions.h"
#include "ModelingToolsManagerActions.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorMode"


//#define ENABLE_DEBUG_PRINTING
//#define TOOLED_ENABLE_VIEWPORT_INTERACTION

const FEditorModeID FModelingToolsEditorMode::EM_ModelingToolsEditorModeId = TEXT("EM_ModelingToolsEditorMode");

FModelingToolsEditorMode::FModelingToolsEditorMode()
{
	ToolsContext = nullptr;

	UICommandList = MakeShareable(new FUICommandList);
}

FModelingToolsEditorMode::~FModelingToolsEditorMode()
{
	if (ToolsContext != nullptr)
	{
		ToolsContext->ShutdownContext();
		ToolsContext = nullptr;
	}
}


void FModelingToolsEditorMode::ActorSelectionChangeNotify()
{
}


bool FModelingToolsEditorMode::ProcessEditDelete()
{
	return ToolsContext->ProcessEditDelete();
}


bool FModelingToolsEditorMode::CanAutoSave() const
{
	// prevent autosave if any tool is active
	return ToolsContext->ToolManager->HasAnyActiveTool() == false;
}

bool FModelingToolsEditorMode::AllowWidgetMove()
{ 
	return false; 
}

bool FModelingToolsEditorMode::ShouldDrawWidget() const
{ 
	// allow standard xform gizmo if we don't have an active tool
	if (ToolsContext != nullptr)
	{
		return ToolsContext->ToolManager->HasAnyActiveTool() == false;
	}
	return true; 
}

bool FModelingToolsEditorMode::UsesTransformWidget() const
{ 
	return true; 
}


void FModelingToolsEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (ToolsContext != nullptr)
	{
		ToolsContext->Tick(ViewportClient, DeltaTime);
	}
}




void FModelingToolsEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	// we do not use PDI hit testing in modeling tools, so skip these render passes
	if (PDI->IsHitTesting())
	{
		return;
	}

	if (ToolsContext != nullptr)
	{
		ToolsContext->Render(View, Viewport, PDI);
	}
}





bool FModelingToolsEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// try hotkeys
	if (Event != IE_Released)
	{
		if (UICommandList->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
		{
			return true;
		}
	}

	bool bHandled = ToolsContext->InputKey(ViewportClient, Viewport, Key, Event);
	if (bHandled == false)
	{
		bHandled = FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	}
	return bHandled;

	//bool bHandled = FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	//bHandled |= ToolsContext->InputKey(ViewportClient, Viewport, Key, Event);
	//return bHandled;
}


bool FModelingToolsEditorMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	// mouse axes: EKeys::MouseX, EKeys::MouseY, EKeys::MouseWheelAxis
	return FEdMode::InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}





bool FModelingToolsEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{ 
	bool bHandled = FEdMode::StartTracking(InViewportClient, InViewport);
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("START TRACKING - base handled was %d"), (int)bHandled);
#endif

	bHandled |= ToolsContext->StartTracking(InViewportClient, InViewport);

	return bHandled;
}

bool FModelingToolsEditorMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	bool bHandled = ToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	return bHandled;
}


bool FModelingToolsEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bHandled = ToolsContext->EndTracking(InViewportClient, InViewport);
	return bHandled;
}








bool FModelingToolsEditorMode::ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("RECEIVED FOCUS"));
#endif

	return false;
}

bool FModelingToolsEditorMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("LOST FOCUS"));
#endif

	return false;
}



bool FModelingToolsEditorMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseEnter(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FModelingToolsEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseMove(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FModelingToolsEditorMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	bool bHandled = ToolsContext->MouseLeave(ViewportClient, Viewport);
	return bHandled;
}



void FModelingToolsEditorMode::Enter()
{
	FEdMode::Enter();

	// initialize FEdMode ToolsContext adapter

	//ToolsContext = NewObject<UEdModeInteractiveToolsContext>(GetTransientPackage(), TEXT("ToolsContext"), RF_Transient);
	ToolsContext = NewObject<UEdModeInteractiveToolsContext>();
	ToolsContext->InitializeContextFromEdMode(this);

	ToolsContext->OnToolNotificationMessage.AddLambda([this](const FText& Message)
	{
		this->OnToolNotificationMessage.Broadcast(Message);
	});
	ToolsContext->OnToolWarningMessage.AddLambda([this](const FText& Message)
	{
		this->OnToolWarningMessage.Broadcast(Message);
	});

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FModelingToolsEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());


		const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
		const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

		CommandList->MapAction(
			ToolManagerCommands.AcceptActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Accept); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanAcceptActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

		CommandList->MapAction(
			ToolManagerCommands.CancelActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Cancel); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCancelActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->ActiveToolHasAccept(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

		CommandList->MapAction(
			ToolManagerCommands.CompleteActiveTool,
			FExecuteAction::CreateLambda([this]() { ToolsContext->EndTool(EToolShutdownType::Completed); }),
			FCanExecuteAction::CreateLambda([this]() { return ToolsContext->CanCompleteActiveTool(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([this]() {return ToolsContext->CanCompleteActiveTool(); }),
			EUIActionRepeatMode::RepeatDisabled
		);

	}

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	auto RegisterToolFunc = [this, &CommandList](TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
	{
		ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
		CommandList->MapAction( UICommand,
			FExecuteAction::CreateLambda([this, ToolIdentifier]() { ToolsContext->StartTool(ToolIdentifier); }),
			FCanExecuteAction::CreateLambda([this, ToolIdentifier]() { return ToolsContext->CanStartTool(ToolIdentifier); }));
	};


	// register tool set

	//
	// make shape tools
	//
	auto AddPrimitiveToolBuilder = NewObject<UAddPrimitiveToolBuilder>();
	AddPrimitiveToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginAddPrimitiveTool, TEXT("AddPrimitiveTool"), AddPrimitiveToolBuilder);

	auto DrawPolygonToolBuilder = NewObject<UDrawPolygonToolBuilder>();
	DrawPolygonToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginDrawPolygonTool, TEXT("DrawPolygonTool"), DrawPolygonToolBuilder);

	auto ShapeSprayToolBuilder = NewObject<UShapeSprayToolBuilder>();
	ShapeSprayToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginShapeSprayTool, TEXT("ShapeSprayTool"), ShapeSprayToolBuilder);


	//
	// vertex deform tools
	// 

	auto MoveVerticesToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	MoveVerticesToolBuilder->bEnableRemeshing = false;
	RegisterToolFunc(ToolManagerCommands.BeginSculptMeshTool, TEXT("MoveVerticesTool"), MoveVerticesToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginPolyEditTool, TEXT("EditMeshPolygonsTool"), NewObject<UEditMeshPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSmoothMeshTool, TEXT("SmoothMeshTool"), NewObject<USmoothMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginDisplaceMeshTool, TEXT("DisplaceMeshTool"), NewObject<UDisplaceMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginMeshSpaceDeformerTool, TEXT("MeshSpaceDeformerTool"), NewObject<UMeshSpaceDeformerToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginTransformMeshesTool, TEXT("TransformMeshesTool"), NewObject<UTransformMeshesToolBuilder>());

	// edit tools


	auto DynaSculptToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	DynaSculptToolBuilder->bEnableRemeshing = true;
	RegisterToolFunc(ToolManagerCommands.BeginRemeshSculptMeshTool, TEXT("DynaSculptTool"), DynaSculptToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginRemeshMeshTool, TEXT("RemeshMeshTool"), NewObject<URemeshMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSimplifyMeshTool, TEXT("SimplifyMeshTool"), NewObject<USimplifyMeshToolBuilder>());


	auto EditNormalsToolBuilder = NewObject<UEditNormalsToolBuilder>();
	EditNormalsToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginEditNormalsTool, TEXT("EditNormalsTool"), EditNormalsToolBuilder);

	auto UVProjectionToolBuilder = NewObject<UUVProjectionToolBuilder>();
	UVProjectionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginUVProjectionTool, TEXT("UVProjectionTool"), UVProjectionToolBuilder);

	auto MergeMeshesToolBuilder = NewObject<UMergeMeshesToolBuilder>();
	MergeMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelMergeTool, TEXT("MergeMeshesTool"), MergeMeshesToolBuilder);

	auto VoxelCSGMeshesToolBuilder = NewObject<UVoxelCSGMeshesToolBuilder>();
	VoxelCSGMeshesToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelBooleanTool, TEXT("VoxelCSGMeshesTool"), VoxelCSGMeshesToolBuilder);

	auto PlaneCutToolBuilder = NewObject<UPlaneCutToolBuilder>();
	PlaneCutToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginPlaneCutTool, TEXT("PlaneCutTool"), PlaneCutToolBuilder);

	auto PolygonOnMeshToolBuilder = NewObject<UPolygonOnMeshToolBuilder>();
	PolygonOnMeshToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginPolygonOnMeshTool, TEXT("PolygonOnMeshTool"), PolygonOnMeshToolBuilder);

	auto ParameterizeMeshToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	ParameterizeMeshToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginParameterizeMeshTool, TEXT("ParameterizeMeshTool"), ParameterizeMeshToolBuilder);

	auto MeshSelectionToolBuilder = NewObject<UMeshSelectionToolBuilder>();
	MeshSelectionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginMeshSelectionTool, TEXT("MeshSelectionTool"), MeshSelectionToolBuilder);


	// analysis tools

	RegisterToolFunc(ToolManagerCommands.BeginMeshInspectorTool, TEXT("MeshInspectorTool"), NewObject<UMeshInspectorToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginWeldEdgesTool, TEXT("WeldMeshEdgesTool"), NewObject<UWeldMeshEdgesToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginPolyGroupsTool, TEXT("ConvertToPolygonsTool"), NewObject<UConvertToPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginAttributeEditorTool, TEXT("AttributeEditorTool"), NewObject<UAttributeEditorToolBuilder>());



	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("DynaSculptTool"));

	// listen for Tool start/end events to bind/unbind any hotkeys relevant to that Tool
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, false);
	});
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, true);
	});


#ifdef TOOLED_ENABLE_VIEWPORT_INTERACTION
	///
	// Viewport Interaction
	///
	UEditorWorldExtensionCollection* ExtensionCollection = GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld());
	check(ExtensionCollection != nullptr);
	this->ViewportWorldInteraction = NewObject<UViewportWorldInteraction>(ExtensionCollection);
	ExtensionCollection->AddExtension(this->ViewportWorldInteraction);
		//Cast<UViewportWorldInteraction>(ExtensionCollection->AddExtension(UViewportWorldInteraction::StaticClass()));
	check(ViewportWorldInteraction != nullptr);
	//this->ViewportWorldInteraction->UseLegacyInteractions();
	//this->ViewportWorldInteraction->AddMouseCursorInteractor();
	this->ViewportWorldInteraction->SetUseInputPreprocessor(true);
	this->ViewportWorldInteraction->SetGizmoHandleType(EGizmoHandleTypes::All);

	// Set the current viewport.
	{
		const TSharedRef< ILevelEditor >& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor().ToSharedRef();

		// Do we have an active perspective viewport that is valid for VR?  If so, go ahead and use that.
		TSharedPtr<FEditorViewportClient> ViewportClient;
		{
			TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditor->GetActiveViewportInterface();
			if (ActiveLevelViewport.IsValid())
			{
				ViewportClient = StaticCastSharedRef<SLevelViewport>(ActiveLevelViewport->AsWidget())->GetViewportClient();
			}
		}

		this->ViewportWorldInteraction->SetDefaultOptionalViewportClient(ViewportClient);
	}
#endif  // TOOLED_ENABLE_VIEWPORT_INTERACTION
}



void FModelingToolsEditorMode::Exit()
{
	OnToolNotificationMessage.Clear();
	OnToolWarningMessage.Clear();

	ToolsContext->ShutdownContext();
	ToolsContext = nullptr;

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}


#ifdef TOOLED_ENABLE_VIEWPORT_INTERACTION
	///
	// Viewport Interaction
	//
	if (IViewportInteractionModule::IsAvailable())
	{
		if (ViewportWorldInteraction != nullptr)
		{
			ViewportWorldInteraction->ReleaseMouseCursorInteractor();

			// Make sure gizmo is visible.  We may have hidden it
			ViewportWorldInteraction->SetTransformGizmoVisible(true);

			// Unregister mesh element transformer
			//ViewportWorldInteraction->SetTransformer(nullptr);

			UEditorWorldExtensionCollection* ExtensionCollection = GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(GetWorld());
			if (ExtensionCollection != nullptr)
			{
				ExtensionCollection->RemoveExtension(ViewportWorldInteraction);
			}

			ViewportWorldInteraction = nullptr;
		}
	}
#endif // TOOLED_ENABLE_VIEWPORT_INTERACTION


	// Call base Exit method to ensure proper cleanup
	FEdMode::Exit();
}

bool FModelingToolsEditorMode::UsesToolkits() const
{
	return true;
}


void FModelingToolsEditorMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ToolsContext);
}





#undef LOCTEXT_NAMESPACE