// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimEdMode.h"
#include "EngineUtils.h"
#include "ContextualAnimSceneAsset.h"
#include "ContextualAnimSelectionCriterion.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimSceneInstance.h"
#include "ContextualAnimViewportClient.h"
#include "ContextualAnimAssetEditorToolkit.h"
#include "ContextualAnimViewModel.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState_IKWindow.h"
#include "SkeletalDebugRendering.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "Components/SkeletalMeshComponent.h"

const FEditorModeID FContextualAnimEdMode::EdModeId = TEXT("ContextualAnimEdMode");

IMPLEMENT_HIT_PROXY(HSelectionCriterionHitProxy, HHitProxy);

FContextualAnimEdMode::FContextualAnimEdMode()
{
}

FContextualAnimEdMode::~FContextualAnimEdMode()
{
}

void FContextualAnimEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
}

void FContextualAnimEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	// @TODO: This should not be initialized here
	FContextualAnimViewportClient* ViewportClient = static_cast<FContextualAnimViewportClient*>(Viewport->GetClient());
	if(!ViewModel)
	{
		ViewModel = ViewportClient->GetAssetEditorToolkit()->GetViewModel();
	}

	if (ViewModel)
	{
		if (const UContextualAnimSceneInstance* SceneInstance = ViewModel->GetSceneInstance())
		{
			const UContextualAnimSceneAsset& SceneAsset = SceneInstance->GetSceneAsset();
			const FContextualAnimSceneBindings& Bindings = SceneInstance->GetBindings();

			// Draw Scene Pivots
			if(const FContextualAnimSceneSection* Section = SceneAsset.GetSection(Bindings.GetSectionIdx()))
			{
				if(const FContextualAnimSet* AnimSet = Section->GetAnimSet(Bindings.GetAnimSetIdx()))
				{
					for (const FTransform& ScenePivot : AnimSet->ScenePivots)
					{
						DrawCoordinateSystem(PDI, ScenePivot.GetLocation(), ScenePivot.Rotator(), 50.f, SDPG_Foreground);
					}
				}
			}

			const EShowIKTargetsDrawMode IKTargetsDrawMode = ViewportClient->GetShowIKTargetsDrawMode();

			for (const FContextualAnimSceneBinding& Binding : Bindings)
			{
				// Draw IK Targets
				if (IKTargetsDrawMode == EShowIKTargetsDrawMode::All || (IKTargetsDrawMode == EShowIKTargetsDrawMode::Selected && Binding.GetActor() == ViewModel->GetSelectedActor()))
				{
					DrawIKTargetsForBinding(*PDI, Binding);
				}

				// Draw Selection Criteria
				if(const FContextualAnimSceneBinding* PrimaryBinding = Bindings.FindBindingByRole(SceneAsset.GetPrimaryRole()))
				{
					const FTransform PrimaryTransform = PrimaryBinding->GetTransform();

					const FContextualAnimTrack& AnimTrack = Binding.GetAnimTrack();
					for (int32 CriterionIdx = 0; CriterionIdx < AnimTrack.SelectionCriteria.Num(); CriterionIdx++)
					{
						if(const UContextualAnimSelectionCriterion* Criterion = AnimTrack.SelectionCriteria[CriterionIdx])
						{
							FLinearColor DrawColor = FLinearColor::White;
							if (Criterion->DoesQuerierPassCondition(FContextualAnimSceneBindingContext(PrimaryTransform), Binding.GetContext()))
							{
								DrawColor = FLinearColor::Green;
							}

							//@TODO: Each SelectionCriterion should implement this, and here we should just call "Criterion->Draw()"
							if (const UContextualAnimSelectionCriterion_TriggerArea* Spatial = Cast<UContextualAnimSelectionCriterion_TriggerArea>(Criterion))
							{
								const float HalfHeight = Spatial->Height / 2.f;
								const int32 LastIndex = Spatial->PolygonPoints.Num() - 1;
								for (int32 Idx = 0; Idx <= LastIndex; Idx++)
								{
									const FVector P0 = PrimaryTransform.TransformPositionNoScale(Spatial->PolygonPoints[Idx]);
									const FVector P1 = PrimaryTransform.TransformPositionNoScale(Spatial->PolygonPoints[Idx == LastIndex ? 0 : Idx + 1]);

									PDI->DrawLine(P0, P1, DrawColor, SDPG_Foreground, 2.f);

									PDI->DrawLine(P0 + FVector::UpVector * Spatial->Height, P1 + FVector::UpVector * Spatial->Height, DrawColor, SDPG_Foreground, 2.f);

									PDI->DrawLine(P0, P0 + FVector::UpVector * Spatial->Height, DrawColor, SDPG_Foreground, 2.f);

									PDI->SetHitProxy(new HSelectionCriterionHitProxy(Binding.GetRoleDef().Name, CriterionIdx, Idx));
									PDI->DrawPoint(P0, FLinearColor::Black, 15.f, SDPG_Foreground);
									PDI->SetHitProxy(nullptr);

									PDI->SetHitProxy(new HSelectionCriterionHitProxy(Binding.GetRoleDef().Name, CriterionIdx, Idx + 4));
									PDI->DrawPoint(P0 + FVector::UpVector * Spatial->Height, FLinearColor::Black, 15.f, SDPG_Foreground);
									PDI->SetHitProxy(nullptr);
								}
							}
						}
					}
				}
			}
		}
	}
}

void FContextualAnimEdMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FEdMode::DrawHUD(ViewportClient, Viewport, View, Canvas);

	if (ViewModel)
	{
		FCanvasTextItem TextItem(FVector2D(10.f, 40.f), ViewModel->GetSelectionDebugText(), GEngine->GetSmallFont(), FLinearColor::White);
		Canvas->DrawItem(TextItem);
	}
}

void FContextualAnimEdMode::DrawIKTargetsForBinding(FPrimitiveDrawInterface& PDI, const FContextualAnimSceneBinding& Binding) const
{
	for (const FContextualAnimIKTargetDefinition& IKTargetDef : Binding.GetIKTargetDefs().IKTargetDefs)
	{
		if (const FContextualAnimSceneBinding* TargetBinding = Binding.GetSceneInstance()->FindBindingByRole(IKTargetDef.TargetRoleName))
		{
			if (USkeletalMeshComponent* TargetSkelMeshComp = TargetBinding->GetSkeletalMeshComponent())
			{
				const float Alpha = UAnimNotifyState_IKWindow::GetIKAlphaValue(IKTargetDef.GoalName, Binding.GetAnimMontageInstance());

				if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Bone)
				{
					//@TODO:...
				}
				else if (IKTargetDef.Provider == EContextualAnimIKTargetProvider::Autogenerated)
				{
					const FTransform ParentTransform = TargetSkelMeshComp->GetSocketTransform(IKTargetDef.TargetBoneName);

					const FTransform TargetTransform = Binding.GetAnimTrack().IKTargetData.ExtractTransformAtTime(IKTargetDef.GoalName, Binding.GetAnimMontageTime()) * ParentTransform;

					FLinearColor Color = Alpha > 0.f ? FLinearColor(FColor::MakeRedToGreenColorFromScalar(Alpha)) : FLinearColor::White;

					FVector Start = ParentTransform.GetLocation();
					FVector End = TargetTransform.GetLocation();

					const float Radius = 1.f;
					SkeletalDebugRendering::DrawWireBone(&PDI, Start, End, Color, SDPG_Foreground, Radius);
					SkeletalDebugRendering::DrawAxes(&PDI, FTransform(End), SDPG_Foreground, 0.f, Radius);
				}
			}
		}
	}
}

bool FContextualAnimEdMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HitProxy != nullptr)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = static_cast<HActor*>(HitProxy);
			ViewModel->UpdateSelection(ActorHitProxy->Actor);
			return true;
		}
		else if (HitProxy->IsA(HSelectionCriterionHitProxy::StaticGetType()))
		{
			HSelectionCriterionHitProxy* CriterionHitProxy = static_cast<HSelectionCriterionHitProxy*>(HitProxy);
			ViewModel->UpdateSelection(CriterionHitProxy->Role, CriterionHitProxy->IndexPair.Key, CriterionHitProxy->IndexPair.Value);
			return true;
		}
	}

	ViewModel->ClearSelection();
	return false; // unhandled
}

bool FContextualAnimEdMode::GetHitResultUnderCursor(FHitResult& OutHitResult, FEditorViewportClient* InViewportClient, const FViewportClick& Click) const
{
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(InViewportClient->Viewport, InViewportClient->GetScene(), InViewportClient->EngineShowFlags).SetRealtimeUpdate(InViewportClient->IsRealtime()));
	FSceneView* View = InViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation Cursor(View, InViewportClient, Click.GetClickPos().X, Click.GetClickPos().Y);
	const auto ViewportType = InViewportClient->GetViewportType();

	const FVector RayStart = Cursor.GetOrigin();
	const FVector RayEnd = RayStart + Cursor.GetDirection() * HALF_WORLD_MAX;

	return InViewportClient->GetWorld()->LineTraceSingleByChannel(OutHitResult, RayStart, RayEnd, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam);
}

bool FContextualAnimEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FContextualAnimEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::EndTracking(InViewportClient, InViewport);
}

bool FContextualAnimEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	EAxisList::Type CurrentAxis = InViewportClient->GetCurrentWidgetAxis();

	if (CurrentAxis != EAxisList::None)
	{
		if (ViewModel)
		{
			return ViewModel->ProcessInputDelta(InDrag, InRot, InScale);
		}
	}

	return false; // unhandled
}

bool FContextualAnimEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if(ViewModel && ViewModel->IsSimulateModePaused())
	{
		if (Key == EKeys::Enter && Event == IE_Released)
		{
			ViewModel->StartSimulation();
			return true;
		}
	}

	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FContextualAnimEdMode::AllowWidgetMove()
{
	return ShouldDrawWidget();
}

bool FContextualAnimEdMode::ShouldDrawWidget() const
{
	if (ViewModel)
	{
		return ViewModel->ShouldPreviewSceneDrawWidget();
	}

	return false;
}

bool FContextualAnimEdMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (ViewModel)
	{
		return ViewModel->GetCustomDrawingCoordinateSystem(InMatrix, InData);
	}

	return false;
}

bool FContextualAnimEdMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

FVector FContextualAnimEdMode::GetWidgetLocation() const
{
	if (ViewModel)
	{
		return ViewModel->GetWidgetLocationFromSelection();
	}

	return FVector::ZeroVector;
}
