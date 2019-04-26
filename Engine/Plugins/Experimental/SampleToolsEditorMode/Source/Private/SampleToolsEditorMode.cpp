// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SampleToolsEditorMode.h"
#include "SampleToolsEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"


//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
// AddYourTool Step 1 - include the header file for your Tool here
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
#include "BaseTools/SingleClickTool.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "SampleTools/CreateActorSampleTool.h"
#include "SampleTools/DrawCurveOnMeshSampleTool.h"
#include "SampleTools/MeasureDistanceSampleTool.h"

// step 2: register a ToolBuilder in FSampleToolsEditorMode::Enter()
// step 3: add a button in FSampleToolsEditorModeToolkit::Init()


#define LOCTEXT_NAMESPACE "FSampleToolsEditorMode"

const FEditorModeID FSampleToolsEditorMode::EM_SampleToolsEditorModeId = TEXT("EM_SampleToolsEditorMode");


FSampleToolsEditorMode::FSampleToolsEditorMode()
{
	ToolsContext = nullptr;
}


FSampleToolsEditorMode::~FSampleToolsEditorMode()
{
	// this should have happend already in ::Exit()
	if (ToolsContext != nullptr)
	{
		ToolsContext->ShutdownContext();
		ToolsContext = nullptr;
	}
}


void FSampleToolsEditorMode::ActorSelectionChangeNotify()
{
	// @todo support selection change
}



void FSampleToolsEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	// give ToolsContext a chance to tick
	if (ToolsContext != nullptr)
	{
		ToolsContext->Tick(ViewportClient, DeltaTime);
	}
}




void FSampleToolsEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	// give ToolsContext a chance to render
	if (ToolsContext != nullptr)
	{
		ToolsContext->Render(View, Viewport, PDI);
	}
}





//
// Input device event tracking. We forward input events to the ToolsContext adapter for handling.
// 

bool FSampleToolsEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	bool bHandled = FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
	bHandled |= ToolsContext->InputKey(ViewportClient, Viewport, Key, Event);
	return bHandled;
}

bool FSampleToolsEditorMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	// mouse axes: EKeys::MouseX, EKeys::MouseY, EKeys::MouseWheelAxis
	return FEdMode::InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}


bool FSampleToolsEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{ 
	bool bHandled = FEdMode::StartTracking(InViewportClient, InViewport);
	bHandled |= ToolsContext->StartTracking(InViewportClient, InViewport);
	return bHandled;
}

bool FSampleToolsEditorMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	bool bHandled = ToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	return bHandled;
}

bool FSampleToolsEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bHandled = ToolsContext->EndTracking(InViewportClient, InViewport);
	return bHandled;
}



bool FSampleToolsEditorMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseEnter(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FSampleToolsEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseMove(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FSampleToolsEditorMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	bool bHandled = ToolsContext->MouseLeave(ViewportClient, Viewport);
	return bHandled;
}



bool FSampleToolsEditorMode::ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return FEdMode::ReceivedFocus(ViewportClient, Viewport);
}


bool FSampleToolsEditorMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	return FEdMode::LostFocus(ViewportClient, Viewport);
}






void FSampleToolsEditorMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FSampleToolsEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// initialize the adapter that attaches the ToolsContext to this FEdMode
	ToolsContext = NewObject<UEdModeInteractiveToolsContext>(GetTransientPackage(), TEXT("ToolsContext"), RF_Transient);
	ToolsContext->InitializeContextFromEdMode(this);


	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// AddYourTool Step 2 - register a ToolBuilder for your Tool here.
	// The string name you pass to the ToolManager is used to select/activate your ToolBuilder later.
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 


	auto CreateActorSampleToolBuilder = NewObject<UCreateActorSampleToolBuilder>();
	CreateActorSampleToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	ToolsContext->ToolManager->RegisterToolType(TEXT("CreateActorSampleTool"), CreateActorSampleToolBuilder);

	auto DrawCurveOnMeshSampleToolBuilder = NewObject<UDrawCurveOnMeshSampleToolBuilder>();
	ToolsContext->ToolManager->RegisterToolType(TEXT("DrawCurveOnMeshSampleTool"), DrawCurveOnMeshSampleToolBuilder);

	auto MeasureDistanceSampleToolBuilder = NewObject<UMeasureDistanceSampleToolBuilder>();
	ToolsContext->ToolManager->RegisterToolType(TEXT("MeasureDistanceSampleTool"), MeasureDistanceSampleToolBuilder);

	auto SurfacePointToolBuilder = NewObject<UMeshSurfacePointToolBuilder>();
	ToolsContext->ToolManager->RegisterToolType(TEXT("SurfacePointTool"), SurfacePointToolBuilder);

	// active tool type is not relevant here, we just set to default
	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("SurfacePointTool"));
}




void FSampleToolsEditorMode::Exit()
{
	// shutdown and clean up the ToolsContext
	ToolsContext->ShutdownContext();
	ToolsContext = nullptr;

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	// Call base Exit method to ensure proper cleanup
	FEdMode::Exit();
}

bool FSampleToolsEditorMode::UsesToolkits() const
{
	return true;
}


void FSampleToolsEditorMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ToolsContext);
}









#undef LOCTEXT_NAMESPACE