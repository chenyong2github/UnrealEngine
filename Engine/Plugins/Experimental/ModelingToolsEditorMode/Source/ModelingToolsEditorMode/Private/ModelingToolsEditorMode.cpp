// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorMode.h"
#include "InteractiveTool.h"
#include "InteractiveToolsSelectionStoreSubsystem.h"
#include "ModelingToolsEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorViewportClient.h"
#include "EngineAnalytics.h"

//#include "SingleClickTool.h"
//#include "MeshSurfacePointTool.h"
//#include "MeshVertexDragTool.h"
#include "DynamicMeshSculptTool.h"
#include "MeshVertexSculptTool.h"
#include "EditMeshPolygonsTool.h"
#include "DeformMeshPolygonsTool.h"
#include "SubdividePolyTool.h"
#include "GroupEdgeInsertionTool.h"
#include "EdgeLoopInsertionTool.h"
#include "ConvertToPolygonsTool.h"
#include "AddPrimitiveTool.h"
#include "AddPatchTool.h"
#include "RevolveBoundaryTool.h"
#include "SmoothMeshTool.h"
#include "OffsetMeshTool.h"
#include "RemeshMeshTool.h"
#include "SimplifyMeshTool.h"
#include "MeshInspectorTool.h"
#include "WeldMeshEdgesTool.h"
#include "DrawPolygonTool.h"
#include "DrawPolyPathTool.h"
#include "DrawAndRevolveTool.h"
#include "ShapeSprayTool.h"
#include "MergeMeshesTool.h"
#include "VoxelCSGMeshesTool.h"
#include "VoxelSolidifyMeshesTool.h"
#include "VoxelBlendMeshesTool.h"
#include "VoxelMorphologyMeshesTool.h"
#include "PlaneCutTool.h"
#include "MirrorTool.h"
#include "SelfUnionMeshesTool.h"
#include "CSGMeshesTool.h"
#include "BspConversionTool.h"
#include "MeshToVolumeTool.h"
#include "VolumeToMeshTool.h"
#include "HoleFillTool.h"
#include "PolygonOnMeshTool.h"
#include "DisplaceMeshTool.h"
#include "MeshSpaceDeformerTool.h"
#include "EditNormalsTool.h"
#include "RemoveOccludedTrianglesTool.h"
#include "AttributeEditorTool.h"
#include "TransformMeshesTool.h"
#include "MeshSelectionTool.h"
#include "UVProjectionTool.h"
#include "UVLayoutTool.h"
#include "EditMeshMaterialsTool.h"
#include "EditPivotTool.h"
#include "BakeTransformTool.h"
#include "CombineMeshesTool.h"
#include "AlignObjectsTool.h"
#include "EditUVIslandsTool.h"
#include "BakeMeshAttributeMapsTool.h"
#include "MeshAttributePaintTool.h"
#include "ParameterizeMeshTool.h"
#include "MeshTangentsTool.h"
#include "ProjectToTargetTool.h"
#include "LatticeDeformerTool.h"
#include "SeamSculptTool.h"

#include "Physics/PhysicsInspectorTool.h"
#include "Physics/SetCollisionGeometryTool.h"
#include "Physics/ExtractCollisionGeometryTool.h"
//#include "Physics/EditCollisionGeometryTool.h"

// hair tools
#include "Hair/GroomToMeshTool.h"
#include "Hair/GroomCardsEditorTool.h"
#include "GenerateLODMeshesTool.h"

#include "EditorModeManager.h"

// stylus support
#include "IStylusInputModule.h"

#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "SLevelViewport.h"

#include "ModelingToolsActions.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingModeAssetAPI.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorMode"


//#define ENABLE_DEBUG_PRINTING

const FEditorModeID FModelingToolsEditorMode::EM_ModelingToolsEditorModeId = TEXT("EM_ModelingToolsEditorMode");

FModelingToolsEditorMode::FModelingToolsEditorMode()
{
	ToolsContext = nullptr;

	UICommandList = MakeShareable(new FUICommandList);
}

FModelingToolsEditorMode::~FModelingToolsEditorMode()
{
	ToolsContext = nullptr;
}


void FModelingToolsEditorMode::ActorSelectionChangeNotify()
{
}


bool FModelingToolsEditorMode::ProcessEditDelete()
{
	if (ToolsContext->ProcessEditDelete())
	{
		return true;
	}

	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if ( GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept() )
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotDeleteWarning", "Cannot delete objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}

	// If we didn't skip deletion, then we're probably deleting something, so it seems fair to clear
	// the selection.
	if (UInteractiveToolsSelectionStoreSubsystem* ToolSelectionStore = GEngine->GetEngineSubsystem<UInteractiveToolsSelectionStoreSubsystem>())
	{
		ToolSelectionStore->ClearStoredSelection();
	}

	return false;
}


bool FModelingToolsEditorMode::ProcessEditCut()
{
	// for now we disable deleting in an Accept-style tool because it can result in crashes if we are deleting target object
	if (GetToolManager()->HasAnyActiveTool() && GetToolManager()->GetActiveTool(EToolSide::Mouse)->HasAccept())
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("CannotCutWarning", "Cannot cut objects while this Tool is active"), EToolMessageLevel::UserWarning);
		return true;
	}

	// If we're doing a cut, we should clear the tool selection.
	if (UInteractiveToolsSelectionStoreSubsystem* ToolSelectionStore = GEngine->GetEngineSubsystem<UInteractiveToolsSelectionStoreSubsystem>())
	{
		ToolSelectionStore->ClearStoredSelection();
	}

	return false;
}



bool FModelingToolsEditorMode::CanAutoSave() const
{
	// prevent autosave if any tool is active
	return ToolsContext->ToolManager->HasAnyActiveTool() == false;
}

bool FModelingToolsEditorMode::ShouldDrawWidget() const
{ 
	// allow standard xform gizmo if we don't have an active tool
	if (ToolsContext != nullptr && ToolsContext->ToolManager->HasAnyActiveTool())
	{
		return false;
	}
	return FEdMode::ShouldDrawWidget(); 
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

	if (Toolkit.IsValid())
	{
		FModelingToolsEditorModeToolkit* ModelingToolkit = (FModelingToolsEditorModeToolkit*)Toolkit.Get();
		ModelingToolkit->EnableShowRealtimeWarning(ViewportClient->IsRealtime() == false);
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

void FModelingToolsEditorMode::DrawHUD(FEditorViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
	if (ToolsContext != nullptr)
	{
		ToolsContext->DrawHUD(ViewportClient, Viewport, View, Canvas);
	}
}





bool FModelingToolsEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	// try hotkeys
	if (Event != IE_Released)
	{
		if (ToolsContext->ShouldIgnoreHotkeys() == false)		// allow the context to capture keyboard input if necessary
		{
			if (UICommandList->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), false/*Event == IE_Repeat*/))
			{
				return true;
			}
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




//
// FStylusStateTracker registers itself as a listener for stylus events and implements
// the IToolStylusStateProviderAPI interface, which allows MeshSurfacePointTool implementations
 // to query for the pen pressure.
//
// This is kind of a hack. Unfortunately the current Stylus module is a Plugin so it
// cannot be used in the base ToolsFramework, and we need this in the Mode as a workaround.
//
class FStylusStateTracker : public IStylusMessageHandler, public IToolStylusStateProviderAPI
{
public:
	const IStylusInputDevice* ActiveDevice = nullptr;
	int32 ActiveDeviceIndex = -1;

	bool bPenDown = false;
	float ActivePressure = 1.0;

	FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->AddMessageHandler(*this);

		ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
		bPenDown = false;
	}

	virtual ~FStylusStateTracker()
	{
		UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
		StylusSubsystem->RemoveMessageHandler(*this);
	}

	virtual void OnStylusStateChanged(const FStylusState& NewState, int32 StylusIndex) override
	{
		if (ActiveDevice == nullptr)
		{
			UStylusInputSubsystem* StylusSubsystem = GEditor->GetEditorSubsystem<UStylusInputSubsystem>();
			ActiveDevice = FindFirstPenDevice(StylusSubsystem, ActiveDeviceIndex);
			bPenDown = false;
		}
		if (ActiveDevice != nullptr && ActiveDeviceIndex == StylusIndex)
		{
			bPenDown = NewState.IsStylusDown();
			ActivePressure = NewState.GetPressure();
		}
	}


	bool HaveActiveStylusState() const
	{
		return ActiveDevice != nullptr && bPenDown;
	}

	static const IStylusInputDevice* FindFirstPenDevice(const UStylusInputSubsystem* StylusSubsystem, int32& ActiveDeviceOut)
	{
		int32 NumDevices = StylusSubsystem->NumInputDevices();
		for (int32 k = 0; k < NumDevices; ++k)
		{
			const IStylusInputDevice* Device = StylusSubsystem->GetInputDevice(k);
			const TArray<EStylusInputType>& Inputs = Device->GetSupportedInputs();
			for (EStylusInputType Input : Inputs)
			{
				if (Input == EStylusInputType::Pressure)
				{
					ActiveDeviceOut = k;
					return Device;
				}
			}
		}
		return nullptr;
	}



	// IToolStylusStateProviderAPI implementation
	virtual float GetCurrentPressure() const override
	{
		return (ActiveDevice != nullptr && bPenDown) ? ActivePressure : 1.0f;
	}

};





void FModelingToolsEditorMode::Enter()
{
	FEdMode::Enter();

	// initialize FEdMode ToolsContext adapter
	ToolsContext = Owner->GetInteractiveToolsContext();
	ModelingModeAssetGenerationAPI = MakeShareable(new FModelingModeAssetAPI(ToolsContext->GetAssetAPI()));

	// register stylus event handler
	StylusStateTracker = MakeUnique<FStylusStateTracker>();

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
		CommandList->MapAction(
		    ToolManagerCommands.CancelOrCompleteActiveTool,
		    FExecuteAction::CreateLambda([this]() {
				const EToolShutdownType ShutdownType = ToolsContext->CanCancelActiveTool() ? EToolShutdownType::Cancel : EToolShutdownType::Completed;
				ToolsContext->EndTool(ShutdownType);
			}),
		    FCanExecuteAction::CreateLambda([this]() {
			    return ToolsContext->CanCompleteActiveTool() || ToolsContext->CanCancelActiveTool();
		    }),
		    FGetActionCheckState(),
		    FIsActionButtonVisible::CreateLambda([this]() { return ToolsContext->CanCompleteActiveTool() || ToolsContext->CanCancelActiveTool();}),
		    EUIActionRepeatMode::RepeatDisabled);
	}

	const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	auto RegisterToolFunc = [this, &CommandList](TSharedPtr<FUICommandInfo> UICommand, FString ToolIdentifier, UInteractiveToolBuilder* Builder)
	{
		ToolsContext->ToolManager->RegisterToolType(ToolIdentifier, Builder);
		CommandList->MapAction( UICommand,
			FExecuteAction::CreateLambda([this, ToolIdentifier]() { ToolsContext->StartTool(ToolIdentifier); }),
			FCanExecuteAction::CreateLambda([this, ToolIdentifier]() { return ToolsContext->CanStartTool(ToolIdentifier); }),
			FIsActionChecked::CreateLambda([this, ToolIdentifier]() { return ToolsContext->GetActiveToolName() == ToolIdentifier; }));

		RegisteredTools.Emplace(UICommand, ToolIdentifier);
	};


	// register tool set

	//
	// primitive tools
	//
	auto RegisterPrimitiveToolFunc  =
		[this, &RegisterToolFunc](TSharedPtr<FUICommandInfo> UICommand,
								  FString&& ToolIdentifier,
								  UAddPrimitiveToolBuilder::EMakeMeshShapeType ShapeTypeIn)
	{
		auto AddPrimitiveToolBuilder = NewObject<UAddPrimitiveToolBuilder>();
		AddPrimitiveToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
		AddPrimitiveToolBuilder->ShapeType = ShapeTypeIn;
		RegisterToolFunc(UICommand, ToolIdentifier, AddPrimitiveToolBuilder);
	};
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddBoxPrimitiveTool,
							  TEXT("BeginAddBoxPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Box);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddCylinderPrimitiveTool,
							  TEXT("BeginAddCylinderPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Cylinder);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddConePrimitiveTool,
							  TEXT("BeginAddConePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Cone);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddArrowPrimitiveTool,
							  TEXT("BeginAddArrowPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Arrow);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddRectanglePrimitiveTool,
							  TEXT("BeginAddRectanglePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Rectangle);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddRoundedRectanglePrimitiveTool,
							  TEXT("BeginAddRoundedRectanglePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::RoundedRectangle);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddDiscPrimitiveTool,
							  TEXT("BeginAddDiscPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Disc);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddPuncturedDiscPrimitiveTool,
							  TEXT("BeginAddPuncturedDiscPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::PuncturedDisc);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddTorusPrimitiveTool,
							  TEXT("BeginAddTorusPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Torus);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddSpherePrimitiveTool,
							  TEXT("BeginAddSpherePrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::Sphere);
	RegisterPrimitiveToolFunc(ToolManagerCommands.BeginAddSphericalBoxPrimitiveTool,
							  TEXT("BeginAddSphericalBoxPrimitiveTool"),
							  UAddPrimitiveToolBuilder::EMakeMeshShapeType::SphericalBox);

	//
	// make shape tools
	//
	auto AddPatchToolBuilder = NewObject<UAddPatchToolBuilder>();
	AddPatchToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginAddPatchTool, TEXT("AddPatchTool"), AddPatchToolBuilder);

	auto RevolveBoundaryToolBuilder = NewObject<URevolveBoundaryToolBuilder>();
	RevolveBoundaryToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginRevolveBoundaryTool, TEXT("RevolveBoundaryTool"), RevolveBoundaryToolBuilder);

	auto DrawPolygonToolBuilder = NewObject<UDrawPolygonToolBuilder>();
	DrawPolygonToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginDrawPolygonTool, TEXT("DrawPolygonTool"), DrawPolygonToolBuilder);

	auto DrawPolyPathToolBuilder = NewObject<UDrawPolyPathToolBuilder>();
	DrawPolyPathToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginDrawPolyPathTool, TEXT("DrawPolyPath"), DrawPolyPathToolBuilder);

	auto DrawAndRevolveToolBuilder = NewObject<UDrawAndRevolveToolBuilder>();
	DrawAndRevolveToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginDrawAndRevolveTool, TEXT("RevolveTool"), DrawAndRevolveToolBuilder);

	auto ShapeSprayToolBuilder = NewObject<UShapeSprayToolBuilder>();
	ShapeSprayToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginShapeSprayTool, TEXT("ShapeSprayTool"), ShapeSprayToolBuilder);


	//
	// vertex deform tools
	//

	auto MoveVerticesToolBuilder = NewObject<UMeshVertexSculptToolBuilder>();
	MoveVerticesToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterToolFunc(ToolManagerCommands.BeginSculptMeshTool, TEXT("MoveVerticesTool"), MoveVerticesToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginPolyEditTool, TEXT("EditMeshPolygonsTool"), NewObject<UEditMeshPolygonsToolBuilder>());
	UEditMeshPolygonsToolBuilder* TriEditBuilder = NewObject<UEditMeshPolygonsToolBuilder>();
	TriEditBuilder->bTriangleMode = true;
	RegisterToolFunc(ToolManagerCommands.BeginTriEditTool, TEXT("EditMeshTrianglesTool"), TriEditBuilder);
	RegisterToolFunc(ToolManagerCommands.BeginPolyDeformTool, TEXT("DeformMeshPolygonsTool"), NewObject<UDeformMeshPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSmoothMeshTool, TEXT("SmoothMeshTool"), NewObject<USmoothMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginOffsetMeshTool, TEXT("OffsetMeshTool"), NewObject<UOffsetMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginDisplaceMeshTool, TEXT("DisplaceMeshTool"), NewObject<UDisplaceMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginMeshSpaceDeformerTool, TEXT("MeshSpaceDeformerTool"), NewObject<UMeshSpaceDeformerToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginTransformMeshesTool, TEXT("TransformMeshesTool"), NewObject<UTransformMeshesToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginEditPivotTool, TEXT("EditPivotTool"), NewObject<UEditPivotToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginAlignObjectsTool, TEXT("AlignObjects"), NewObject<UAlignObjectsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginBakeTransformTool, TEXT("BakeTransformTool"), NewObject<UBakeTransformToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginTransformUVIslandsTool, TEXT("EditUVIslands"), NewObject<UEditUVIslandsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginLatticeDeformerTool, TEXT("LatticeDeformerTool"), NewObject<ULatticeDeformerToolBuilder>());

	UCombineMeshesToolBuilder* CombineMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	CombineMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginCombineMeshesTool, TEXT("CombineMeshesTool"), CombineMeshesToolBuilder);

	UCombineMeshesToolBuilder* DuplicateMeshesToolBuilder = NewObject<UCombineMeshesToolBuilder>();
	DuplicateMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	DuplicateMeshesToolBuilder->bIsDuplicateTool = true;
	RegisterToolFunc(ToolManagerCommands.BeginDuplicateMeshesTool, TEXT("DuplicateMeshesTool"), DuplicateMeshesToolBuilder);


	// edit tools


	auto DynaSculptToolBuilder = NewObject<UDynamicMeshSculptToolBuilder>();
	DynaSculptToolBuilder->bEnableRemeshing = true;
	DynaSculptToolBuilder->StylusAPI = StylusStateTracker.Get();
	RegisterToolFunc(ToolManagerCommands.BeginRemeshSculptMeshTool, TEXT("DynaSculptTool"), DynaSculptToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginRemeshMeshTool, TEXT("RemeshMeshTool"), NewObject<URemeshMeshToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginProjectToTargetTool, TEXT("ProjectToTargetTool"), NewObject<UProjectToTargetToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSimplifyMeshTool, TEXT("SimplifyMeshTool"), NewObject<USimplifyMeshToolBuilder>());

	auto GroupEdgeInsertionToolBuilder = NewObject<UGroupEdgeInsertionToolBuilder>();
	GroupEdgeInsertionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginGroupEdgeInsertionTool, TEXT("GroupEdgeInsertionTool"), GroupEdgeInsertionToolBuilder);

	auto EdgeLoopInsertionToolBuilder = NewObject<UEdgeLoopInsertionToolBuilder>();
	EdgeLoopInsertionToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	RegisterToolFunc(ToolManagerCommands.BeginEdgeLoopInsertionTool, TEXT("EdgeLoopInsertionTool"), EdgeLoopInsertionToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginSubdividePolyTool, TEXT("SubdividePolyTool"), NewObject<USubdividePolyToolBuilder>());

	auto EditNormalsToolBuilder = NewObject<UEditNormalsToolBuilder>();
	EditNormalsToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginEditNormalsTool, TEXT("EditNormalsTool"), EditNormalsToolBuilder);

	auto TangentsToolBuilder = NewObject<UMeshTangentsToolBuilder>();
	RegisterToolFunc(ToolManagerCommands.BeginEditTangentsTool, TEXT("MeshTangentsTool"), TangentsToolBuilder);

	auto RemoveOccludedTrianglesToolBuilder = NewObject<URemoveOccludedTrianglesToolBuilder>();
	RemoveOccludedTrianglesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginRemoveOccludedTrianglesTool, TEXT("RemoveOccludedTrianglesTool"), RemoveOccludedTrianglesToolBuilder);

	auto HoleFillToolBuilder = NewObject<UHoleFillToolBuilder>();
	RegisterToolFunc(ToolManagerCommands.BeginHoleFillTool, TEXT("HoleFillTool"), HoleFillToolBuilder);

	auto UVProjectionToolBuilder = NewObject<UUVProjectionToolBuilder>();
	UVProjectionToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginUVProjectionTool, TEXT("UVProjectionTool"), UVProjectionToolBuilder);

	auto UVLayoutToolBuilder = NewObject<UUVLayoutToolBuilder>();
	UVLayoutToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginUVLayoutTool, TEXT("UVLayoutTool"), UVLayoutToolBuilder);

	auto MergeMeshesToolBuilder = NewObject<UMergeMeshesToolBuilder>();
	MergeMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelMergeTool, TEXT("MergeMeshesTool"), MergeMeshesToolBuilder);

	auto VoxelCSGMeshesToolBuilder = NewObject<UVoxelCSGMeshesToolBuilder>();
	VoxelCSGMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelBooleanTool, TEXT("VoxelCSGMeshesTool"), VoxelCSGMeshesToolBuilder);

	auto VoxelSolidifyMeshesToolBuilder = NewObject<UVoxelSolidifyMeshesToolBuilder>();
	VoxelSolidifyMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelSolidifyTool, TEXT("VoxelSolidifyMeshesTool"), VoxelSolidifyMeshesToolBuilder);

	auto VoxelBlendMeshesToolBuilder = NewObject<UVoxelBlendMeshesToolBuilder>();
	VoxelBlendMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelBlendTool, TEXT("VoxelBlendMeshesTool"), VoxelBlendMeshesToolBuilder);

	auto VoxelMorphologyMeshesToolBuilder = NewObject<UVoxelMorphologyMeshesToolBuilder>();
	VoxelMorphologyMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVoxelMorphologyTool, TEXT("VoxelMorphologyMeshesTool"), VoxelMorphologyMeshesToolBuilder);

	auto SelfUnionMeshesToolBuilder = NewObject<USelfUnionMeshesToolBuilder>();
	SelfUnionMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginSelfUnionTool, TEXT("SelfUnionMeshesTool"), SelfUnionMeshesToolBuilder);

	auto CSGMeshesToolBuilder = NewObject<UCSGMeshesToolBuilder>();
	CSGMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginMeshBooleanTool, TEXT("CSGMeshesTool"), CSGMeshesToolBuilder);

	auto BspConversionToolBuilder = NewObject<UBspConversionToolBuilder>();
	BspConversionToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginBspConversionTool, TEXT("BspConversionTool"), BspConversionToolBuilder);

	auto MeshToVolumeToolBuilder = NewObject<UMeshToVolumeToolBuilder>();
	MeshToVolumeToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginMeshToVolumeTool, TEXT("MeshToVolumeTool"), MeshToVolumeToolBuilder);

	auto VolumeToMeshToolBuilder = NewObject<UVolumeToMeshToolBuilder>();
	VolumeToMeshToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginVolumeToMeshTool, TEXT("VolumeToMeshTool"), VolumeToMeshToolBuilder);

	auto PlaneCutToolBuilder = NewObject<UPlaneCutToolBuilder>();
	PlaneCutToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginPlaneCutTool, TEXT("PlaneCutTool"), PlaneCutToolBuilder);

	auto MirrorToolBuilder = NewObject<UMirrorToolBuilder>();
	MirrorToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginMirrorTool, TEXT("MirrorTool"), MirrorToolBuilder);

	auto PolygonCutToolBuilder = NewObject<UPolygonOnMeshToolBuilder>();
	RegisterToolFunc(ToolManagerCommands.BeginPolygonCutTool, TEXT("PolyCutTool"), PolygonCutToolBuilder);

	auto GlobalUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GlobalUVGenerateToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	GlobalUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = true;
	RegisterToolFunc(ToolManagerCommands.BeginGlobalUVGenerateTool, TEXT("GlobalParameterizeMeshTool"), GlobalUVGenerateToolBuilder);

	auto GroupUVGenerateToolBuilder = NewObject<UParameterizeMeshToolBuilder>();
	GroupUVGenerateToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	GroupUVGenerateToolBuilder->bDoAutomaticGlobalUnwrap = false;
	RegisterToolFunc(ToolManagerCommands.BeginGroupUVGenerateTool, TEXT("GroupParameterizeMeshTool"), GroupUVGenerateToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginUVSeamEditTool, TEXT("UVSeamSculptTool"), NewObject< USeamSculptToolBuilder>());

	auto MeshSelectionToolBuilder = NewObject<UMeshSelectionToolBuilder>();
	MeshSelectionToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginMeshSelectionTool, TEXT("MeshSelectionTool"), MeshSelectionToolBuilder);

	auto EditMeshMaterialsToolBuilder = NewObject<UEditMeshMaterialsToolBuilder>();
	EditMeshMaterialsToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginEditMeshMaterialsTool, TEXT("EditMaterialsTool"), EditMeshMaterialsToolBuilder);
	
	RegisterToolFunc(ToolManagerCommands.BeginMeshAttributePaintTool, TEXT("MeshAttributePaintTool"), NewObject<UMeshAttributePaintToolBuilder>());

	auto BakeMeshAttributeMapsToolBuilder = NewObject<UBakeMeshAttributeMapsToolBuilder>();
	BakeMeshAttributeMapsToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginBakeMeshAttributeMapsTool, TEXT("BakeMeshMapsTool"), BakeMeshAttributeMapsToolBuilder);

	// analysis tools

	RegisterToolFunc(ToolManagerCommands.BeginMeshInspectorTool, TEXT("MeshInspectorTool"), NewObject<UMeshInspectorToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginWeldEdgesTool, TEXT("WeldMeshEdgesTool"), NewObject<UWeldMeshEdgesToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginPolyGroupsTool, TEXT("ConvertToPolygonsTool"), NewObject<UConvertToPolygonsToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginAttributeEditorTool, TEXT("AttributeEditorTool"), NewObject<UAttributeEditorToolBuilder>());


	// Physics Tools

	RegisterToolFunc(ToolManagerCommands.BeginPhysicsInspectorTool, TEXT("PhysicsInspectorTool"), NewObject<UPhysicsInspectorToolBuilder>());
	RegisterToolFunc(ToolManagerCommands.BeginSetCollisionGeometryTool, TEXT("SetCollisionGeoTool"), NewObject<USetCollisionGeometryToolBuilder>());
	//RegisterToolFunc(ToolManagerCommands.BeginEditCollisionGeometryTool, TEXT("EditCollisionGeoTool"), NewObject<UEditCollisionGeometryToolBuilder>());

	auto ExtractCollisionGeoToolBuilder = NewObject<UExtractCollisionGeometryToolBuilder>();
	ExtractCollisionGeoToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginExtractCollisionGeometryTool, TEXT("ExtractCollisionGeoTool"), ExtractCollisionGeoToolBuilder);



	// (experimental) hair tools

	UGroomToMeshToolBuilder* GroomToMeshToolBuilder = NewObject<UGroomToMeshToolBuilder>();
	GroomToMeshToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginGroomToMeshTool, TEXT("GroomToMeshTool"), GroomToMeshToolBuilder);

	RegisterToolFunc(ToolManagerCommands.BeginGroomCardsEditorTool, TEXT("GroomCardsEditorTool"), NewObject<UGroomCardsEditorToolBuilder>());

	UGenerateLODMeshesToolBuilder* GenerateLODMeshesToolBuilder = NewObject<UGenerateLODMeshesToolBuilder>();
	GenerateLODMeshesToolBuilder->AssetAPI = ModelingModeAssetGenerationAPI.Get();
	RegisterToolFunc(ToolManagerCommands.BeginGenerateLODMeshesTool, TEXT("GenerateLODMeshesTool"), GenerateLODMeshesToolBuilder);


	// PolyModeling tools
	auto RegisterPolyModelSelectTool = [&](EEditMeshPolygonsToolSelectionMode SelectionMode, TSharedPtr<FUICommandInfo> UICommand, FString StringName)
	{
		UEditMeshPolygonsSelectionModeToolBuilder* SelectionModeBuilder = NewObject<UEditMeshPolygonsSelectionModeToolBuilder>();
		SelectionModeBuilder->SelectionMode = SelectionMode;
		RegisterToolFunc(UICommand, StringName, SelectionModeBuilder);
	};
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Faces, ToolManagerCommands.BeginPolyModelTool_FaceSelect, TEXT("PolyEdit_FaceSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Edges, ToolManagerCommands.BeginPolyModelTool_EdgeSelect, TEXT("PolyEdit_EdgeSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Vertices, ToolManagerCommands.BeginPolyModelTool_VertexSelect, TEXT("PolyEdit_VertexSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Loops, ToolManagerCommands.BeginPolyModelTool_LoopSelect, TEXT("PolyEdit_LoopSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::Rings, ToolManagerCommands.BeginPolyModelTool_RingSelect, TEXT("PolyEdit_RingSelect"));
	RegisterPolyModelSelectTool(EEditMeshPolygonsToolSelectionMode::FacesEdgesVertices, ToolManagerCommands.BeginPolyModelTool_AllSelect, TEXT("PolyEdit_AllSelect"));

	auto RegisterPolyModelActionTool = [&](EEditMeshPolygonsToolActions Action, TSharedPtr<FUICommandInfo> UICommand, FString StringName)
	{
		UEditMeshPolygonsActionModeToolBuilder* ActionModeBuilder = NewObject<UEditMeshPolygonsActionModeToolBuilder>();
		ActionModeBuilder->StartupAction = Action;
		RegisterToolFunc(UICommand, StringName, ActionModeBuilder);
	};
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Extrude, ToolManagerCommands.BeginPolyModelTool_Extrude, TEXT("PolyEdit_Extrude"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Offset, ToolManagerCommands.BeginPolyModelTool_Offset, TEXT("PolyEdit_Offset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Inset, ToolManagerCommands.BeginPolyModelTool_Inset, TEXT("PolyEdit_Inset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::Outset, ToolManagerCommands.BeginPolyModelTool_Outset, TEXT("PolyEdit_Outset"));
	RegisterPolyModelActionTool(EEditMeshPolygonsToolActions::CutFaces, ToolManagerCommands.BeginPolyModelTool_CutFaces, TEXT("PolyEdit_CutFaces"));

	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("DynaSculptTool"));

	// register modeling mode hotkeys
	FModelingModeActionCommands::RegisterCommandBindings(UICommandList, [this](EModelingModeActionCommands Command) {
		ModelingModeShortcutRequested(Command);
	});

	// listen for Tool start/end events to bind/unbind any hotkeys relevant to that Tool
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, false);
	});
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, true);
	});

	// enable realtime viewport override
	ConfigureRealTimeViewportsOverride(true);

	//
	// Engine Analytics
	//
	// Log Analytic of mode starting
	if( FEngineAnalytics::IsAvailable() )
	{
		FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.Enter" ) );
	}
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		if( FEngineAnalytics::IsAvailable() )
		{
			FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.ToolStarted" ),
														 TEXT( "DisplayName" ),
														 Tool->GetToolInfo().ToolDisplayName.ToString() );
		}
	});
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		if( FEngineAnalytics::IsAvailable() )
		{
			FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.ToolEnded" ),
														 TEXT( "DisplayName" ),
														 Tool->GetToolInfo().ToolDisplayName.ToString() );
		}
	});
}



void FModelingToolsEditorMode::Exit()
{
	//
	// Engine Analytics
	//
	// Log Analytic of mode ending
	if( FEngineAnalytics::IsAvailable() )
	{
		FEngineAnalytics::GetProvider().RecordEvent( TEXT( "Editor.Usage.MeshModelingMode.Exit" ) );
	}

	StylusStateTracker = nullptr;

	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	for (auto& RegisteredTool : RegisteredTools)
	{
		CommandList->UnmapAction(RegisteredTool.Key);
		ToolsContext->ToolManager->UnregisterToolType(RegisteredTool.Value);
	}
	RegisteredTools.SetNum(0);

	if (Toolkit.IsValid())
	{
		const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
		const TSharedRef<FUICommandList>& ToolkitCommandList = Toolkit->GetToolkitCommands();
		ToolkitCommandList->UnmapAction(ToolManagerCommands.AcceptActiveTool);
		ToolkitCommandList->UnmapAction(ToolManagerCommands.CancelActiveTool);
		ToolkitCommandList->UnmapAction(ToolManagerCommands.CancelOrCompleteActiveTool);
		ToolkitCommandList->UnmapAction(ToolManagerCommands.CompleteActiveTool);

		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	ToolsContext = nullptr;
	ModelingModeAssetGenerationAPI.Reset();

	FModelingModeActionCommands::UnRegisterCommandBindings(UICommandList);

	// clear realtime viewport override
	ConfigureRealTimeViewportsOverride(false);

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


void FModelingToolsEditorMode::ModelingModeShortcutRequested(EModelingModeActionCommands Command)
{
	if (Command == EModelingModeActionCommands::FocusViewToCursor)
	{
		FocusCameraAtCursorHotkey();
	}
}


void FModelingToolsEditorMode::FocusCameraAtCursorHotkey()
{
	FRay Ray = ToolsContext->GetLastWorldRay();

	FHitResult HitResult;
	bool bHitWorld = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(GetWorld(), HitResult, Ray.Origin, Ray.PointAt(HALF_WORLD_MAX));
	if (bHitWorld)
	{
		FVector HitPoint = HitResult.ImpactPoint;
		if (GCurrentLevelEditingViewportClient)
		{
			GCurrentLevelEditingViewportClient->CenterViewportAtPoint(HitPoint, false);
		}
	}
}


bool FModelingToolsEditorMode::GetPivotForOrbit(FVector& OutPivot) const
{
	if (GCurrentLevelEditingViewportClient)
	{
		OutPivot = GCurrentLevelEditingViewportClient->GetViewTransform().GetLookAt();
		return true;
	}
	return false;
}



void FModelingToolsEditorMode::ConfigureRealTimeViewportsOverride(bool bEnable)
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
				const FText SystemDisplayName = LOCTEXT("RealtimeOverrideMessage_ModelingMode", "Modeling Mode");
				if (bEnable)
				{
					Viewport.AddRealtimeOverride(bEnable, SystemDisplayName);
				}
				else
				{
					Viewport.RemoveRealtimeOverride(SystemDisplayName, false);
				}
			}
		}
	}
}



#undef LOCTEXT_NAMESPACE
