// Copyright Epic Games, Inc. All Rights Reserved.
#include "Dataflow/DataflowEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowRenderingActor.h"
#include "Dataflow/DataflowRenderingComponent.h"
#include "EditorViewportClient.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "SCommonEditorViewportToolbarBase.h"

SDataflowEditorViewport::SDataflowEditorViewport()
{
	// Temporarily allow water subsystem to be created on preview worlds because we need one here : 
	//UWaterSubsystem::FScopedAllowWaterSubsystemOnPreviewWorld AllowWaterSubsystemOnPreviewWorldScope(true);
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
	CustomDataflowRenderingActor = CastChecked<ADataflowRenderingActor>(PreviewScene->GetWorld()->SpawnActor(ADataflowRenderingActor::StaticClass()));

	EditorViewportClient->SetDataflowRenderingActor(CustomDataflowRenderingActor);
	EditorViewportClient->FocusViewportOnBox( SphereBounds.GetBox());
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
	Collector.AddReferencedObject(CustomDataflowRenderingActor);
}

TSharedRef<FEditorViewportClient> SDataflowEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FDataflowEditorViewportClient(PreviewScene.Get(), SharedThis(this), DataflowEditorToolkitPtr));
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDataflowEditorViewport::MakeViewportToolbar()
{
	return SNew(SCommonEditorViewportToolbarBase, SharedThis(this));
}


// ----------------------------------------------------------------------------------

FDataflowEditorViewportClient::FDataflowEditorViewportClient(FPreviewScene* InPreviewScene, 
	const TWeakPtr<SEditorViewport> InEditorViewportWidget,
	TWeakPtr<FDataflowEditorToolkit> InDataflowEditorToolkitPtr)
	: 
	FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget)
	, DataflowEditorToolkitPtr(InDataflowEditorToolkitPtr)
{
	bSetListenerPosition = false;
	SetRealtime(true);
	EngineShowFlags.Grid = false;
}

Dataflow::FTimestamp FDataflowEditorViewportClient::LatestTimestamp(const UDataflow* Dataflow, const Dataflow::FContext* Context)
{
	if (Dataflow && Context)
	{
		return FMath::Max(Dataflow->GetRenderingTimestamp().Value, Context->GetTimestamp().Value);
	}
	return Dataflow::FTimestamp::Invalid;
}


void FDataflowEditorViewportClient::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TSharedPtr<FDataflowEditorToolkit> DataflowEditorToolkit = DataflowEditorToolkitPtr.Pin();

	if (DataflowRenderingActor && DataflowEditorToolkitPtr.IsValid())
	{
		if (TSharedPtr<Dataflow::FContext> Context = DataflowEditorToolkit->GetContext())
		{
			if (UDataflowRenderingComponent* DataflowRenderingComponent = DataflowRenderingActor->GetDataflowRenderingComponent())
			{
				if (const UDataflow* Dataflow = DataflowEditorToolkit->GetDataflow())
				{
					Dataflow::FTimestamp SystemTimestamp = LatestTimestamp(Dataflow, Context.Get());
					if (SystemTimestamp >= LastModifiedTimestamp)
					{
						if (Dataflow->GetRenderTargets().Num())
						{
							// @todo(dataflow) : Check the Time on the target outs first instead.
							//					 to avoid invalidation during unrelated edits. 

							DataflowRenderingComponent->ResetRenderTargets();

							DataflowRenderingComponent->SetDataflow(Dataflow);
							DataflowRenderingComponent->SetContext(Context);
							for (const UDataflowEdNode* Node : Dataflow->GetRenderTargets())
							{
								DataflowRenderingComponent->AddRenderTarget(Node);
							}
						}
						else
						{
							DataflowRenderingComponent->ResetRenderTargets();
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

	if (DataflowRenderingActor)
	{
		Collector.AddReferencedObject(DataflowRenderingActor);
	}
}


