// Copyright Epic Games, Inc. All Rights Reserved.
#include "FleshEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "ChaosFlesh/FleshActor.h"
#include "ChaosFlesh/FleshAsset.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowRenderingActor.h"
#include "Dataflow/DataflowRenderingComponent.h"
#include "EditorViewportClient.h"
#include "FleshEditorToolkit.h"
#include "SCommonEditorViewportToolbarBase.h"

SFleshEditorViewport::SFleshEditorViewport()
{
	// Temporarily allow water subsystem to be created on preview worlds because we need one here : 
	//UWaterSubsystem::FScopedAllowWaterSubsystemOnPreviewWorld AllowWaterSubsystemOnPreviewWorldScope(true);
	PreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));
	PreviewScene->SetFloorVisibility(false);
}

void SFleshEditorViewport::Construct(const FArguments& InArgs)
{
	FleshEditorToolkitPtr = InArgs._FleshEditorToolkit;
	TSharedPtr<FFleshEditorToolkit> FleshEditorToolkit = FleshEditorToolkitPtr.Pin();
	check(FleshEditorToolkitPtr.IsValid());

	SEditorViewport::Construct(SEditorViewport::FArguments());

	FBoxSphereBounds SphereBounds1 = FBoxSphereBounds(EForceInit::ForceInitToZero);
	CustomFleshActor =CastChecked<AFleshActor>(PreviewScene->GetWorld()->SpawnActor(AFleshActor::StaticClass()));
	if (UFleshAsset* FleshAsset = FleshEditorToolkit->GetFleshAsset())
	{
		if (UFleshComponent* FleshComponent = CustomFleshActor->GetFleshComponent())
		{
			FleshComponent->SetRestCollection(FleshAsset);
			SphereBounds1 = FleshComponent->CalcBounds(CustomFleshActor->GetTransform());
		}
	}

	FBoxSphereBounds SphereBounds2 = FBoxSphereBounds(EForceInit::ForceInitToZero);
	CustomDataflowRenderingActor = CastChecked<ADataflowRenderingActor>(PreviewScene->GetWorld()->SpawnActor(ADataflowRenderingActor::StaticClass()));

	EditorViewportClient->SetDataflowRenderingActor(CustomDataflowRenderingActor);
	EditorViewportClient->FocusViewportOnBox( (/*SphereBounds1 + */ SphereBounds2).GetBox());
}

TSharedRef<SEditorViewport> SFleshEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SFleshEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SFleshEditorViewport::OnFloatingButtonClicked()
{
}

void SFleshEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{							
	Collector.AddReferencedObject(CustomFleshActor);
	Collector.AddReferencedObject(CustomDataflowRenderingActor);
}

TSharedRef<FEditorViewportClient> SFleshEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FFleshEditorViewportClient(PreviewScene.Get(), SharedThis(this), FleshEditorToolkitPtr));
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SFleshEditorViewport::MakeViewportToolbar()
{
	return SNew(SCommonEditorViewportToolbarBase, SharedThis(this));
}


// ----------------------------------------------------------------------------------

FFleshEditorViewportClient::FFleshEditorViewportClient(FPreviewScene* InPreviewScene, 
	const TWeakPtr<SEditorViewport> InEditorViewportWidget,
	TWeakPtr<FFleshEditorToolkit> InFleshEditorToolkitPtr)
	: 
	FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget)
	, FleshEditorToolkitPtr(InFleshEditorToolkitPtr)
{
	bSetListenerPosition = false;
	SetRealtime(true);
	EngineShowFlags.Grid = false;
}

Dataflow::FTimestamp FFleshEditorViewportClient::LatestTimestamp(const UDataflow* Dataflow, const Dataflow::FContext* Context)
{
	if (Dataflow && Context)
	{
		return FMath::Max(Dataflow->GetRenderingTimestamp().Value, Context->GetTimestamp().Value);
	}
	return Dataflow::FTimestamp::Invalid;
}


void FFleshEditorViewportClient::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TSharedPtr<FFleshEditorToolkit> FleshEditorToolkit = FleshEditorToolkitPtr.Pin();

	if (DataflowRenderingActor && FleshEditorToolkitPtr.IsValid())
	{
		if (TSharedPtr<Dataflow::FContext> Context = FleshEditorToolkit->GetContext())
		{
			if (UDataflowRenderingComponent* DataflowRenderingComponent = DataflowRenderingActor->GetDataflowRenderingComponent())
			{
				if (const UDataflow* Dataflow = FleshEditorToolkit->GetDataflow())
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


void FFleshEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);

	if (DataflowRenderingActor)
	{
		Collector.AddReferencedObject(DataflowRenderingActor);
	}
}


