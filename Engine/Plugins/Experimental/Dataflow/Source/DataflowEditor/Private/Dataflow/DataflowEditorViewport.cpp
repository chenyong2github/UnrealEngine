// Copyright Epic Games, Inc. All Rights Reserved.
#include "Dataflow/DataflowEditorViewport.h"

#include "AdvancedPreviewScene.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowRenderingActor.h"
#include "Dataflow/DataflowRenderingComponent.h"
#include "DynamicMeshBuilder.h"
#include "Dataflow/DataflowEditorToolkit.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "Materials/Material.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "Settings/EditorStyleSettings.h"


static int32 bEnableDataflowClientDrawing = 0;
FAutoConsoleVariableRef CVarbEnableDataflowClientDrawing(
	TEXT("p.Dataflow.Rendering.ClientDraw"), bEnableDataflowClientDrawing,TEXT("Enable the client rendering, will be slower on complex bodies. [default:0]"));

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
	SetRealtime(true);
	SetViewModes(VMI_Lit, VMI_Lit);
	bSetListenerPosition = false;
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

void FDataflowEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	Super::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

}


void FDataflowEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	Super::Draw(View, PDI);
	if (bEnableDataflowClientDrawing)
	{
		GeometryCollection::Facades::FRenderingFacade Facade(&RenderCollection);
		if (Facade.IsValid())
		{
			if (bRenderMesh)
			{
				MeshBuilder.Reset(new FDynamicMeshBuilder(View->GetFeatureLevel()));

				FLinearColor UnselectedColor = GEngine->C_BrushWire;
				//UnselectedColor.A = .1f;

				FLinearColor SelectedColor = GetDefault<UEditorStyleSettings>()->SelectionColor;
				SelectedColor.A = .5f;

				// Allocate the material proxy and register it so it can be deleted properly once the rendering is done with it.
				FDynamicColoredMaterialRenderProxy* SelectedColorInstance = new FDynamicColoredMaterialRenderProxy(GEngine->GeomMaterial->GetRenderProxy(), SelectedColor);
				PDI->RegisterDynamicResource(SelectedColorInstance);

				FDynamicColoredMaterialRenderProxy* UnselectedColorInstance = new FDynamicColoredMaterialRenderProxy(GEngine->GeomMaterial->GetRenderProxy(), UnselectedColor);
				PDI->RegisterDynamicResource(UnselectedColorInstance);

				// mesh surface
				bool bRenderSurface = false;
				if (bRenderSurface)
				{
					MeshBuilder->AddVertices(VertexBuffer);
					MeshBuilder->AddTriangles(IndexBuffer);
					MeshBuilder->Draw(PDI, FMatrix::Identity, UnselectedColorInstance, ESceneDepthPriorityGroup::SDPG_World, false);
				}
				else
				{
					// wireframes
					auto ToF = [](FVector3f V) { return FVector(V.X, V.Y, V.Z); };
					for (int i = 0; i < IndexBuffer.Num(); i += 3)
					{
						PDI->DrawLine(ToF(VertexBuffer[i].Position), ToF(VertexBuffer[i + 1].Position), SelectedColor, SDPG_World, 1.f);
						PDI->DrawLine(ToF(VertexBuffer[i].Position), ToF(VertexBuffer[i + 2].Position), SelectedColor, SDPG_World, 1.f);
						PDI->DrawLine(ToF(VertexBuffer[i + 1].Position), ToF(VertexBuffer[i + 2].Position), SelectedColor, SDPG_World, 1.f);
					}
				}
			}
		}
	}
}

void FDataflowEditorViewportClient::ReleaseRenderStructures()
{
	RenderCollection = FManagedArrayCollection();

	// MeshBuilder
	if (bEnableDataflowClientDrawing)
	{
		bRenderMesh = false;
		IndexBuffer = TArray<uint32>();
		VertexBuffer = TArray<FDynamicMeshVertex>();
		MeshBuilder.Reset(nullptr);
	}
}

void FDataflowEditorViewportClient::RenderIntoStructures()
{
	GeometryCollection::Facades::FRenderingFacade Facade(&RenderCollection);
	if (bEnableDataflowClientDrawing)
	{
		if (Facade.IsValid())
		{
			// @todo(flesh) : Setup buffers that will get directly copied to 
			//                the render. This is only called during invalidation
			//                of the graph, so any state that can be precalculated 
			//                should be set here and just pushed to the render in
			//                the ::Draw() method.
			// 
			//	Note: Draw will still be very slow for complicated geometry, 
			//        we still need to rely on Component rendering through
			//        a scene proxy as the primary rendering path. 
			{
				const TManagedArray< FIntVector >& Indices = *Facade.GetIndices();
				const TManagedArray< FVector3f >& Vertices = *Facade.GetVertices();
				if (Vertices.Num() && Indices.Num())
				{
					IndexBuffer.SetNum(Indices.Num() * 3);
					for (int Idx = 0; Idx < Indices.Num() * 3; Idx++)
					{
						IndexBuffer[Idx] = Idx;
					}

					int Vdx = 0;
					VertexBuffer.SetNum(Indices.Num() * 3);
					for (const FIntVector& Face : Indices)
					{
						FIntVector Tri = FIntVector(Face[0], Face[1], Face[2]);
						const FVector3f Pos0(Vertices[Tri[0]]), Pos1(Vertices[Tri[1]]), Pos2(Vertices[Tri[2]]);
						const FVector3f Normal = FVector3f::CrossProduct(Pos2 - Pos0, Pos1 - Pos0).GetSafeNormal();
						const FVector3f Tangent = ((Pos1 + Pos2) * 0.5f - Pos0).GetSafeNormal();

						VertexBuffer[Vdx++] = FDynamicMeshVertex(Pos0, Tangent, Normal, FVector2f(0.f, 0.f), FColor::White);
						VertexBuffer[Vdx++] = FDynamicMeshVertex(Pos1, Tangent, Normal, FVector2f(0.f, 1.f), FColor::White);
						VertexBuffer[Vdx++] = FDynamicMeshVertex(Pos2, Tangent, Normal, FVector2f(1.f, 1.f), FColor::White);
					}

					bRenderMesh = true;
				}
			}
		}
	}
}


void FDataflowEditorViewportClient::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TSharedPtr<FDataflowEditorToolkit> DataflowEditorToolkit = DataflowEditorToolkitPtr.Pin();

	if (DataflowRenderingActor && DataflowEditorToolkitPtr.IsValid())
	{
		if (TSharedPtr<Dataflow::FContext> Context = DataflowEditorToolkit->GetContext())
		{
			if (const UDataflow* Dataflow = DataflowEditorToolkit->GetDataflow())
			{
				if (UDataflowRenderingComponent* DataflowRenderingComponent = DataflowRenderingActor->GetDataflowRenderingComponent())
				{
					Dataflow::FTimestamp SystemTimestamp = LatestTimestamp(Dataflow, Context.Get());
					if (SystemTimestamp >= LastModifiedTimestamp)
					{
						ReleaseRenderStructures();

						if (Dataflow->GetRenderTargets().Num())
						{
							GeometryCollection::Facades::FRenderingFacade Facade(&RenderCollection);
							for (const UDataflowEdNode* Target : Dataflow->GetRenderTargets())
							{
								Target->Render(Facade, Context);
							}

							// Client View Rendering
							RenderIntoStructures();

							// Component Object Rendering
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
}


