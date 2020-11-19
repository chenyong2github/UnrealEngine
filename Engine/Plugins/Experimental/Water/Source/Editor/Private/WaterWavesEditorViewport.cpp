#include "WaterWavesEditorViewport.h"

#include "WaterWavesEditorToolkit.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "WaterBodyCustomActor.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "WaterEditorSettings.h"
#include "WaterSplineComponent.h"

#include "WaterSubsystem.h"

SWaterWavesEditorViewport::SWaterWavesEditorViewport()
	: PreviewScene(MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues())))
{
}

void SWaterWavesEditorViewport::Construct(const FArguments& InArgs)
{
	WaterWavesEditorToolkitPtr = InArgs._WaterWavesEditorToolkit;

	TSharedPtr<FWaterWavesEditorToolkit> WaterWavesEditorToolkit = WaterWavesEditorToolkitPtr.Pin();
	check(WaterWavesEditorToolkitPtr.IsValid());

	UWaterWavesAssetReference* WaterWavesAssetRef = WaterWavesEditorToolkit->GetWavesAssetRef();

	SEditorViewport::Construct(SEditorViewport::FArguments());

	PreviewScene->SetFloorVisibility(false);

	CustomWaterBody = CastChecked<AWaterBodyCustom>(PreviewScene->GetWorld()->SpawnActor(AWaterBodyCustom::StaticClass()));
	CustomWaterBody->SetWaterMeshOverride(GetDefault<UWaterEditorSettings>()->WaterBodyCustomDefaults.GetWaterMesh());

	UWaterSplineComponent* WaterSpline = CustomWaterBody->GetWaterSpline();
	WaterSpline->ResetSpline({ FVector(0, 0, 0) });
	CustomWaterBody->SetWaterWaves(WaterWavesAssetRef);
	CustomWaterBody->SetActorScale3D(FVector(60, 60, 1));

	EditorViewportClient->MoveViewportCamera(FVector(-3000, 0, 2000), FRotator(-35.f, 0.f, 0.f));
}

TSharedRef<SEditorViewport> SWaterWavesEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SWaterWavesEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SWaterWavesEditorViewport::OnFloatingButtonClicked()
{
}

void SWaterWavesEditorViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(CustomWaterBody);
}

TSharedRef<FEditorViewportClient> SWaterWavesEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FEditorViewportClient(nullptr, PreviewScene.Get(), SharedThis(this)));
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetRealtime(true);
	EditorViewportClient->EngineShowFlags.Grid = false;
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWaterWavesEditorViewport::MakeViewportToolbar()
{
	return SNew(SCommonEditorViewportToolbarBase, SharedThis(this));
}

void SWaterWavesEditorViewport::SetShouldPauseWaveTime(bool bShouldFreeze)
{
	UWaterSubsystem* WaterSubsystem = EditorViewportClient->GetWorld()->GetSubsystem<UWaterSubsystem>();
	check(WaterSubsystem != nullptr);
	WaterSubsystem->SetShouldPauseWaveTime(bShouldFreeze);
}