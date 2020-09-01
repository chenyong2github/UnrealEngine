// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomEditorMode.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorViewportClient.h"
#include "EdModeInteractiveToolsContext.h"

#include "EditorModeManager.h"
#include "GroomEditorCommands.h"
#include "Toolkits/BaseToolkit.h"
#include "EdMode.h"

#define LOCTEXT_NAMESPACE "FGroomEditorMode"

const FEditorModeID FGroomEditorMode::EM_GroomEditorModeId = TEXT("EM_GroomEditorMode");

FGroomEditorMode::FGroomEditorMode()
{
	ToolsContext = nullptr;

	UICommandList = MakeShareable(new FUICommandList);
}

FGroomEditorMode::~FGroomEditorMode()
{
	if (ToolsContext != nullptr)
	{
		ToolsContext->ShutdownContext();
		ToolsContext = nullptr;
	}
}

void FGroomEditorMode::ActorSelectionChangeNotify()
{
}

bool FGroomEditorMode::ProcessEditDelete()
{
	return ToolsContext->ProcessEditDelete();
}

bool FGroomEditorMode::AllowWidgetMove()
{ 
	return false; 
}

bool FGroomEditorMode::ShouldDrawWidget() const
{ 
	// allow standard xform gizmo if we don't have an active tool
	if (ToolsContext != nullptr)
	{
		return ToolsContext->ToolManager->HasAnyActiveTool() == false;
	}
	return true; 
}

bool FGroomEditorMode::UsesTransformWidget() const
{ 
	return true; 
}

void FGroomEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (ToolsContext != nullptr)
	{
		ToolsContext->Tick(ViewportClient, DeltaTime);
	}
}

void FGroomEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	if (ToolsContext != nullptr)
	{
		ToolsContext->Render(View, Viewport, PDI);
	}
}

bool FGroomEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
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
}

bool FGroomEditorMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	// mouse axes: EKeys::MouseX, EKeys::MouseY, EKeys::MouseWheelAxis
	return FEdMode::InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
}

bool FGroomEditorMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) 
{ 
	bool bHandled = FEdMode::StartTracking(InViewportClient, InViewport);
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("START TRACKING - base handled was %d"), (int)bHandled);
#endif

	bHandled |= ToolsContext->StartTracking(InViewportClient, InViewport);

	return bHandled;
}

bool FGroomEditorMode::CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	bool bHandled = ToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	return bHandled;
}

bool FGroomEditorMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bool bHandled = ToolsContext->EndTracking(InViewportClient, InViewport);
	return bHandled;
}

bool FGroomEditorMode::ReceivedFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("RECEIVED FOCUS"));
#endif

	return false;
}

bool FGroomEditorMode::LostFocus(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
#ifdef ENABLE_DEBUG_PRINTING
	UE_LOG(LogTemp, Warning, TEXT("LOST FOCUS"));
#endif

	return false;
}

bool FGroomEditorMode::MouseEnter(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseEnter(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FGroomEditorMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	bool bHandled = ToolsContext->MouseMove(ViewportClient, Viewport, x, y);
	return bHandled;
}

bool FGroomEditorMode::MouseLeave(FEditorViewportClient* ViewportClient, FViewport* Viewport)
{
	bool bHandled = ToolsContext->MouseLeave(ViewportClient, Viewport);
	return bHandled;
}

void FGroomEditorMode::Enter()
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
		//Toolkit = MakeShareable(new FGroomEditorModeToolkit);
		//Toolkit->Init(Owner->GetToolkitHost());

		//const FModelingToolsManagerCommands& ToolManagerCommands = FModelingToolsManagerCommands::Get();
		//const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

		/*CommandList->MapAction(
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
		);*/

	}

	const FGroomEditorCommands& ToolManagerCommands = FGroomEditorCommands::Get();
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
	////
	//auto HairPlaceToolBuilder = NewObject<UHairPlaceToolBuilder>();
	//HairPlaceToolBuilder->AssetAPI = ToolsContext->GetAssetAPI();
	//RegisterToolFunc(ToolManagerCommands.BeginHairPlaceTool, TEXT("HairPlaceTool"), HairPlaceToolBuilder);
			
	ToolsContext->ToolManager->SelectActiveToolType(EToolSide::Left, TEXT("HairPlaceTool"));

	// listen for Tool start/end events to bind/unbind any hotkeys relevant to that Tool
	ToolsContext->ToolManager->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		//FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, false);
	});
	ToolsContext->ToolManager->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		//FModelingToolActionCommands::UpdateToolCommandBinding(Tool, UICommandList, true);
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

void FGroomEditorMode::Exit()
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

bool FGroomEditorMode::UsesToolkits() const
{
	return true;
}

void FGroomEditorMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ToolsContext);
}

#undef LOCTEXT_NAMESPACE