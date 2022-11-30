// Copyright Epic Games, Inc. All Rights Reserved.
#include "Dataflow/DataflowEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowActor.h"
#include "Dataflow/DataflowComponent.h"
#include "Dataflow/DataflowEditorViewportToolbar.h"
#include "Dataflow/DataflowEngineSceneHitProxies.h"
#include "DynamicMeshBuilder.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "EditorViewportCommands.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UIAction.h"
#include "Materials/Material.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "Settings/EditorStyleSettings.h"

SDataflowEditorViewport::SDataflowEditorViewport()
{
	PreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));
	PreviewScene->SetFloorVisibility(false);
}

void SDataflowEditorViewport::Construct(const FArguments& InArgs)
{
	DataflowEditorToolkitPtr = InArgs._DataflowEditorToolkit;
	TSharedPtr<FDataflowEditorToolkit> DataflowEditorToolkit = DataflowEditorToolkitPtr.Pin();
	check(DataflowEditorToolkitPtr.IsValid());

	SEditorViewport::Construct(SEditorViewport::FArguments());

	FBoxSphereBounds SphereBounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
	CustomDataflowActor = CastChecked<ADataflowActor>(PreviewScene->GetWorld()->SpawnActor(ADataflowActor::StaticClass()));

	ViewportClient->SetDataflowActor(CustomDataflowActor);
	ViewportClient->FocusViewportOnBox( SphereBounds.GetBox());
}

TSharedRef<SEditorViewport> SDataflowEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}


TSharedPtr<FExtender> SDataflowEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SDataflowEditorViewport::OnFloatingButtonClicked()
{
}

void SDataflowEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CustomDataflowActor);
}

TSharedRef<FEditorViewportClient> SDataflowEditorViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShareable(new FDataflowEditorViewportClient(PreviewScene.Get(), SharedThis(this), DataflowEditorToolkitPtr));
	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDataflowEditorViewport::MakeViewportToolbar()
{
	return
		SNew(SDataflowViewportSelectionToolBar)
		.EditorViewport(SharedThis(this))
		.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());
}

void SDataflowEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();
	{
		const FDataflowEditorCommandsImpl& Commands = FDataflowEditorCommands::Get();
		TSharedRef<FDataflowEditorViewportClient> ClientRef = ViewportClient.ToSharedRef();

		CommandList->MapAction(
			Commands.ToggleObjectSelection,
			FExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::SetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Object)
			,FCanExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::CanSetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Object)
			,FIsActionChecked::CreateSP(ClientRef, &FDataflowEditorViewportClient::IsSelectionModeActive, FDataflowSelectionState::EMode::DSS_Dataflow_Object)
		);

		CommandList->MapAction(
			Commands.ToggleFaceSelection,
			FExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::SetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Face)
			, FCanExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::CanSetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Face)
			, FIsActionChecked::CreateSP(ClientRef, &FDataflowEditorViewportClient::IsSelectionModeActive, FDataflowSelectionState::EMode::DSS_Dataflow_Face)
		);

		CommandList->MapAction(
			Commands.ToggleVertexSelection,
			FExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::SetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Vertex)
			, FCanExecuteAction::CreateSP(ClientRef, &FDataflowEditorViewportClient::CanSetSelectionMode, FDataflowSelectionState::EMode::DSS_Dataflow_Vertex)
			, FIsActionChecked::CreateSP(ClientRef, &FDataflowEditorViewportClient::IsSelectionModeActive, FDataflowSelectionState::EMode::DSS_Dataflow_Vertex)
		);
	}
}

// ----------------------------------------------------------------------------------

FDataflowEditorViewportClient::FDataflowEditorViewportClient(FPreviewScene* InPreviewScene, 
	const TWeakPtr<SEditorViewport> InEditorViewportWidget,
	TWeakPtr<FDataflowEditorToolkit> InDataflowEditorToolkitPtr)
	: 
	FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget)
	, DataflowEditorToolkitPtr(InDataflowEditorToolkitPtr)
{
	SetRealtime(true);
	SetViewModes(VMI_Lit, VMI_Lit);
	bSetListenerPosition = false;
	EngineShowFlags.Grid = false;
}

void FDataflowEditorViewportClient::SetSelectionMode(FDataflowSelectionState::EMode InState)
{
	FDataflowSelectionState State = DataflowActor->DataflowComponent->GetSelectionState();
		
	if (SelectionMode == InState)
	{
		SelectionMode = FDataflowSelectionState::EMode::DSS_Dataflow_None;
	}
	else
	{
		SelectionMode = InState;
	}

	State.Mode = SelectionMode;
	DataflowActor->DataflowComponent->SetSelectionState(State);

	if (SelectionMode == FDataflowSelectionState::EMode::DSS_Dataflow_None)
	{
		if (!DataflowActor->DataflowComponent->GetSelectionState().IsEmpty())
		{
			DataflowActor->DataflowComponent->SetSelectionState(FDataflowSelectionState(SelectionMode));
		}
	}
}
bool FDataflowEditorViewportClient::CanSetSelectionMode(FDataflowSelectionState::EMode InState)
{
	TSharedPtr<FDataflowEditorToolkit> DataflowEditorToolkit = DataflowEditorToolkitPtr.Pin();
	if (DataflowEditorToolkitPtr.IsValid())
	{
		if (const UDataflow* Dataflow = DataflowEditorToolkit->GetDataflow())
		{
			if (Dataflow->GetRenderTargets().Num())
			{
				if (InState == FDataflowSelectionState::EMode::DSS_Dataflow_Object)
				{
					return true;
				}

				if (InState == FDataflowSelectionState::EMode::DSS_Dataflow_Vertex
					&& !DataflowActor->DataflowComponent->GetSelectionState().Nodes.IsEmpty())
				{
					return true;
				}
			}
		}
	}

	return false;
}
bool FDataflowEditorViewportClient::IsSelectionModeActive(FDataflowSelectionState::EMode InState) 
{ 
	return SelectionMode == InState;
}

Dataflow::FTimestamp FDataflowEditorViewportClient::LatestTimestamp(const UDataflow* Dataflow, const Dataflow::FContext* Context)
{
	if (Dataflow && Context)
	{
		return FMath::Max(Dataflow->GetRenderingTimestamp().Value, Context->GetTimestamp().Value);
	}
	return Dataflow::FTimestamp::Invalid;
}


void FDataflowEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	Super::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
	if (DataflowActor && DataflowActor->DataflowComponent)
	{
		const bool bIsShiftKeyDown = Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift);
		const bool bIsCtrltKeyDown = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);

		FDataflowSelectionState SelectionState = DataflowActor->DataflowComponent->GetSelectionState();
		FDataflowSelectionState PreState = SelectionState;

		if (SelectionMode == FDataflowSelectionState::EMode::DSS_Dataflow_Object)
		{
			if (HitProxy && HitProxy->IsA(HDataflowNode::StaticGetType()))
			{
				HDataflowNode* DataflowNode = (HDataflowNode*)(HitProxy);
				FDataflowSelectionState::ObjectID ID(DataflowNode->NodeName, DataflowNode->GeometryIndex);

				if (bIsShiftKeyDown)
				{
					if (!SelectionState.Nodes.Contains(ID))
					{
						SelectionState.Nodes.AddUnique(ID);
					}
				}
				else if (bIsCtrltKeyDown)
				{
					if (SelectionState.Nodes.Contains(ID))
					{
						SelectionState.Nodes.Remove(ID);
					}
				}
				else
				{
					SelectionState.Nodes.Empty();
					SelectionState.Nodes.AddUnique(ID);
				}
			}
			else if (!bIsShiftKeyDown && !bIsCtrltKeyDown)
			{
				SelectionState.Nodes.Empty();
			}
		}

		if (SelectionMode == FDataflowSelectionState::EMode::DSS_Dataflow_Vertex)
		{
			if (HitProxy && HitProxy->IsA(HDataflowVertex::StaticGetType()))
			{
				HDataflowVertex* DataflowVertex = (HDataflowVertex*)(HitProxy);
				int32 ID = DataflowVertex->SectionIndex;
				if (bIsShiftKeyDown)
				{
					if (!SelectionState.Vertices.Contains(ID))
					{
						SelectionState.Vertices.AddUnique(ID);
					}
				}
				else if (bIsCtrltKeyDown)
				{
					if (SelectionState.Vertices.Contains(ID))
					{
						SelectionState.Vertices.Remove(ID);
					}
				}
				else
				{
					SelectionState.Vertices.Empty();
					SelectionState.Vertices.AddUnique(ID);
				}
			}
			else if (!bIsShiftKeyDown && !bIsCtrltKeyDown)
			{
				SelectionState.Vertices.Empty();
			}
		}

		if (PreState != SelectionState)
		{
			DataflowActor->DataflowComponent->SetSelectionState(SelectionState);
		}
	}
}

void FDataflowEditorViewportClient::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TSharedPtr<FDataflowEditorToolkit> DataflowEditorToolkit = DataflowEditorToolkitPtr.Pin();

	if (DataflowActor && DataflowEditorToolkitPtr.IsValid())
	{
		if (TSharedPtr<Dataflow::FContext> Context = DataflowEditorToolkit->GetContext())
		{
			if (const UDataflow* Dataflow = DataflowEditorToolkit->GetDataflow())
			{
				if (UDataflowComponent* DataflowComponent = DataflowActor->GetDataflowComponent())
				{
					Dataflow::FTimestamp SystemTimestamp = LatestTimestamp(Dataflow, Context.Get());
					if (SystemTimestamp >= LastModifiedTimestamp)
					{
						if (Dataflow->GetRenderTargets().Num())
						{
							// Component Object Rendering
							DataflowComponent->ResetRenderTargets();
							DataflowComponent->SetDataflow(Dataflow);
							DataflowComponent->SetContext(Context);
							for (const UDataflowEdNode* Node : Dataflow->GetRenderTargets())
							{
								DataflowComponent->AddRenderTarget(Node);
							}
						}
						else
						{
							DataflowComponent->ResetRenderTargets();
						}

						LastModifiedTimestamp = LatestTimestamp(Dataflow, Context.Get()).Value + 1;
					}
				}
			}
		}
	}

	// Tick the preview scene world.
	if (!GIntraFrameDebuggingGameThread)
	{
		PreviewScene->GetWorld()->Tick(IsRealtime() ? LEVELTICK_All : LEVELTICK_TimeOnly, DeltaSeconds);
	}
}


void FDataflowEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}


