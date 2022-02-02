// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerEditorMode.h"
#include "MLDeformerEditor.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerAssetDetails.h"
#include "MLDeformerComponent.h"
#include "MLDeformerInstance.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorStyle.h"

#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "IPersonaPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Materials/Material.h"

#include "Components/TextRenderComponent.h"
#include "GeometryCacheComponent.h"

#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "SSimpleTimeSlider.h"

FName FMLDeformerEditorMode::ModeName("MLDeformerAssetEditMode");

FMLDeformerEditorMode::FMLDeformerEditorMode()
{
}

void FMLDeformerEditorMode::SetEditorData(TSharedPtr<FMLDeformerEditorData> InEditorData)
{
	EditorData = InEditorData;
}

void FMLDeformerEditorMode::EncapsulateBounds(const FMLDeformerEditorActor* Actor, FBox& Box) const
{
	check(Actor);
	const USkeletalMeshComponent* SkelMeshComponent = Actor->SkelMeshComponent;
	const UGeometryCacheComponent* GeomCacheComponent = Actor->GeomCacheComponent;

	if (SkelMeshComponent && SkelMeshComponent->IsVisible())
	{
		Box += SkelMeshComponent->Bounds.GetBox();
	}
	else if (GeomCacheComponent && GeomCacheComponent->IsVisible())
	{
		Box += GeomCacheComponent->Bounds.GetBox();
	}
}

bool FMLDeformerEditorMode::GetCameraTarget(FSphere& OutTarget) const
{
	auto Data = EditorData.Pin();

	FBox Box;
	Box.Init();
	EncapsulateBounds(&Data->GetEditorActor(EMLDeformerEditorActorIndex::Base), Box);
	EncapsulateBounds(&Data->GetEditorActor(EMLDeformerEditorActorIndex::Target), Box);
	EncapsulateBounds(&Data->GetEditorActor(EMLDeformerEditorActorIndex::Test), Box);
	EncapsulateBounds(&Data->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest), Box);
	EncapsulateBounds(&Data->GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth), Box);

	if (Box.IsValid == 1)
	{
		OutTarget = FSphere(Box.GetCenter(), Box.GetExtent().X * 0.75f);
		return true;
	}

	return false;
}

IPersonaPreviewScene& FMLDeformerEditorMode::GetAnimPreviewScene() const
{
	return *static_cast<IPersonaPreviewScene*>(static_cast<FAssetEditorModeManager*>(Owner)->GetPreviewScene());
}

void FMLDeformerEditorMode::GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const
{
}

void FMLDeformerEditorMode::DrawDebugPoints(FPrimitiveDrawInterface* PDI, const TArray<FVector3f>& Points, int32 DepthGroup, const FLinearColor& Color)
{
	const float PointSize = MLDeformerCVars::DebugDrawPointSize.GetValueOnAnyThread();
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FVector Position = (FVector)Points[Index];
		PDI->DrawPoint(Position, Color, PointSize, DepthGroup);
	}
}

void FMLDeformerEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	FMLDeformerEditorData* Data = EditorData.Pin().Get();
	const UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();
	if (DeformerAsset == nullptr)
	{
		return;
	}

	const UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
	{
		// Draw deltas.
		if (VizSettings->GetDrawVertexDeltas() && (Data->VertexDeltas.Num() / 3) == Data->LinearSkinnedPositions.Num())
		{
			const FLinearColor DeltasColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.Deltas.Color");
			const FLinearColor DebugVectorsColor = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color");
			const FLinearColor DebugVectorsColor2 = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color2");
			const uint8 DepthGroup = VizSettings->GetXRayDeltas() ? 100 : 0;
			const TArray<FVector3f>& SkinnedPositions = Data->LinearSkinnedPositions;
			for (int32 Index = 0; Index < Data->LinearSkinnedPositions.Num(); ++Index)
			{
				const int32 ArrayIndex = 3 * Index;
				const FVector Delta(
					Data->VertexDeltas[ArrayIndex], 
					Data->VertexDeltas[ArrayIndex + 1], 
					Data->VertexDeltas[ArrayIndex + 2]);
				const FVector VertexPos = (FVector)SkinnedPositions[Index];
				PDI->DrawLine(VertexPos, VertexPos + Delta, DeltasColor, DepthGroup);
			}
		}

		// Draw the first set of debug points.
		if (MLDeformerCVars::DebugDraw1.GetValueOnAnyThread())
		{
			const uint8 DepthGroup = VizSettings->GetXRayDeltas() ? 100 : 0;
			const FLinearColor Color = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color");
			DrawDebugPoints(PDI, Data->DebugVectors, DepthGroup, Color);
		}

		// Draw the second set of debug points.
		if (MLDeformerCVars::DebugDraw2.GetValueOnAnyThread())
		{
			const uint8 DepthGroup = VizSettings->GetXRayDeltas() ? 100 : 0;
			const FLinearColor Color = FMLDeformerEditorStyle::Get().GetColor("MLDeformer.DebugVectors.Color2");
			DrawDebugPoints(PDI, Data->DebugVectors2, DepthGroup, Color);
		}
	}
}

bool FMLDeformerEditorMode::AllowWidgetMove()
{
	return false;
}

bool FMLDeformerEditorMode::ShouldDrawWidget() const
{
	return false;
}

bool FMLDeformerEditorMode::UsesTransformWidget() const
{
	return false;
}

bool FMLDeformerEditorMode::UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const
{
	return false;
}

void FMLDeformerEditorMode::UpdateLabels()
{
	FMLDeformerEditorData* Data = EditorData.Pin().Get();
	UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();
	check(DeformerAsset);
	
	const UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	const bool bDrawTrainingActors = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData);
	const bool bDrawTestActors = (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TestData);

	for (int32 Index = 0; Index < Data->GetNumEditorActors(); ++Index)
	{
		const EMLDeformerEditorActorIndex ActorIndex = static_cast<EMLDeformerEditorActorIndex>(Index);
		const FMLDeformerEditorActor& EditorActor = Data->GetEditorActor(ActorIndex);
		UTextRenderComponent* LabelComponent = EditorActor.LabelComponent;
		if (LabelComponent)
		{
			const AActor* Actor = EditorActor.Actor;
			const FVector ActorLocation = Actor->GetActorLocation();
			const FVector AlignmentOffset = (EditorActor.GeomCacheComponent != nullptr) ? DeformerAsset->GetAlignmentTransform().GetTranslation() : FVector::ZeroVector;
			LabelComponent->SetRelativeLocation(ActorLocation + FVector(0.0f, 0.0f, VizSettings->GetLabelHeight()) - AlignmentOffset);
			LabelComponent->SetRelativeRotation(FQuat(FVector(0.0f, 0.0f, 1.0f), FMath::DegreesToRadians(90.0f)));
			LabelComponent->SetRelativeScale3D(FVector(VizSettings->GetLabelScale()));

			// Update visibility.
			if (VizSettings->bDrawLabels)
			{
				LabelComponent->SetVisibility(Data->IsActorVisible(ActorIndex));

				// Handle ground truth, disable its label when no ground truth asset was selected.
				if (ActorIndex == EMLDeformerEditorActorIndex::GroundTruth && EditorActor.GeomCacheComponent->GetGeometryCache() == nullptr)
				{
					LabelComponent->SetVisibility(false);
				}
			}
			else
			{
				LabelComponent->SetVisibility(false);
			}
		}
	}
}

void FMLDeformerEditorMode::UpdateActors()
{
	FMLDeformerEditorData* Data = EditorData.Pin().Get();

	UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();
	const UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	check(VizSettings);

	// Update base actor's transform.
	AActor* BaseActor = Data->GetEditorActor(EMLDeformerEditorActorIndex::Base).Actor;
	BaseActor->SetActorTransform(FTransform::Identity);

	// Update target actor's transform.
	AActor* TargetActor = Data->GetEditorActor(EMLDeformerEditorActorIndex::Target).Actor;
	FTransform TargetTransform = DeformerAsset->GetAlignmentTransform();
	TargetTransform.AddToTranslation(VizSettings->GetMeshSpacingOffsetVector());
	TargetActor->SetActorTransform(TargetTransform);

	// Update test actor's transform.
	AActor* TestActor = Data->GetEditorActor(EMLDeformerEditorActorIndex::Test).Actor;
	TestActor->SetActorTransform(FTransform::Identity);

	// Update deformed test actor's transform.
	AActor* DeformedTestActor = Data->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).Actor;
	FTransform DeformedTestTransform = FTransform::Identity;
	DeformedTestTransform.AddToTranslation(VizSettings->GetMeshSpacingOffsetVector());
	DeformedTestActor->SetActorTransform(DeformedTestTransform);

	// Update the ground truth actor's transform.
	AActor* GroundTruthActor = Data->GetEditorActor(EMLDeformerEditorActorIndex::GroundTruth).Actor;
	FTransform GroundTruthTransform = DeformerAsset->GetAlignmentTransform();
	GroundTruthTransform.AddToTranslation(VizSettings->GetMeshSpacingOffsetVector() * 2.0f);
	GroundTruthActor->SetActorTransform(GroundTruthTransform);

	// Update the vertex delta multiplier.
	UMLDeformerComponent* DeformerComponent = Data->GetEditorActor(EMLDeformerEditorActorIndex::DeformedTest).MLDeformerComponent;
	check(DeformerComponent);
	DeformerComponent->SetVertexDeltaMultiplier(VizSettings->GetVertexDeltaMultiplier());
}

void FMLDeformerEditorMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FMLDeformerEditorData* Data = EditorData.Pin().Get();
	Data->ClampFrameIndex();

	UpdateActors();
	UpdateLabels();

	// Calculate the training deltas when needed.
	UMLDeformerAsset* DeformerAsset = Data->GetDeformerAsset();
	UMLDeformerVizSettings* VizSettings = DeformerAsset->GetVizSettings();
	if ((VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData) && VizSettings->GetDrawVertexDeltas())
	{
		Data->GenerateDeltas(0, VizSettings->FrameNumber, Data->VertexDeltas);
	}

	// Force us back into the pose we want to see.
	if (VizSettings->GetVisualizationMode() == EMLDeformerVizMode::TrainingData)
	{
		Data->SetAnimFrame(VizSettings->FrameNumber);
	}

	FEdMode::Tick(ViewportClient, DeltaTime);
}

void FMLDeformerEditorMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);
}
