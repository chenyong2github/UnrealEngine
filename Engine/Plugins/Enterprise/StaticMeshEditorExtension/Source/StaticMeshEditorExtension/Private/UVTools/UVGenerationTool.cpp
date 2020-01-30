// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVGenerationTool.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "UVGenerationSettings.h"

#define LOCTEXT_NAMESPACE "UVGenerationTool"

const FEditorModeID FUVGenerationTool::EM_UVGeneration(TEXT("EM_UVGeneration"));

bool FUVGenerationTool::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (IsHandlingInputs())
	{
		InMatrix = FRotationMatrix::Make(ShapeRotation);
		return true;
	}

	return false;
}

bool FUVGenerationTool::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!(IsHandlingInputs() && InViewportClient->bWidgetAxisControlledByDrag))
	{
		return false;
	}

	ShapePosition += InDrag;
	ShapeSize += InScale;

	if (InRot != FRotator::ZeroRotator)
	{
		FRotator RotationWinding, RotationRemainder;
		ShapeRotation.GetWindingAndRemainder(RotationWinding, RotationRemainder);

		const FQuat ActorQuat = RotationRemainder.Quaternion();
		const FQuat DeltaQuat = InRot.Quaternion();
		const FQuat ResultQuat = DeltaQuat * ActorQuat;
		const FRotator NewSocketRotationRemainder = FRotator(ResultQuat);
		FRotator DeltaRot = NewSocketRotationRemainder - RotationRemainder;
		DeltaRot.Normalize();

		ShapeRotation += DeltaRot;
	}

	OnShapeSettingsChangedEvent.Broadcast(ShapePosition, ShapeSize, ShapeRotation);
	return true;
}

bool FUVGenerationTool::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (IsHandlingInputs() && InViewportClient->bWidgetAxisControlledByDrag)
	{
		if (!bIsTrackingWidgetDrag)
		{
			const FWidget::EWidgetMode WidgetMode = GetModeManager()->GetWidgetMode();
			FText TransText;

			if (WidgetMode == FWidget::WM_Rotate)
			{
				TransText = LOCTEXT("ChangeRotationSettings", "Rotate projection shape");
			}
			else if(WidgetMode == FWidget::WM_Scale)
			{
				TransText = LOCTEXT("ChangeSizeSettings", "Scale projection shape");
			}
			else
			{
				TransText = LOCTEXT("ChangePositionSettings", "Move projection shape");
			}

			bIsTrackingWidgetDrag = true;
			GEditor->BeginTransaction(TransText);
		}

		return true;
	}

	return false;
}

bool FUVGenerationTool::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (bIsTrackingWidgetDrag)
	{
		bIsTrackingWidgetDrag = false;
		GEditor->EndTransaction();

		return true;
	}

	return false;
}

void FUVGenerationTool::Enter()
{
	PreviousWidgetMode = GetModeManager()->GetWidgetMode();
	GetModeManager()->SetWidgetMode(FWidget::EWidgetMode::WM_Translate);
}

void FUVGenerationTool::Exit()
{
	GetModeManager()->SetWidgetMode(PreviousWidgetMode);
}

void FUVGenerationTool::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FLinearColor DrawColor = FLinearColor::Green;
	FTransform Transform(ShapeRotation, FVector::ZeroVector, ShapeSize * 0.5f);

	switch (ShapeType)
	{
		case EGenerateUVProjectionType::Box:
		{
			DrawOrientedWireBox(PDI, ShapePosition,
				Transform.GetScaledAxis(EAxis::X), Transform.GetScaledAxis(EAxis::Y), Transform.GetScaledAxis(EAxis::Z),
				FVector::OneVector, DrawColor, SDPG_Foreground);

			break;
		}
		case EGenerateUVProjectionType::Cylindrical:
		{
			//The cylinder of the Cylinder Projection is aligned on the X-axis, but the DrawWireCylinder() is aligned on the Z-axis so we apply a rotation and shape correction.
			FVector BoxSize = ShapeSize * 0.5f;
			Transform = FTransform((FRotator(90, 0, 0).Quaternion() * ShapeRotation.Quaternion()), FVector::ZeroVector, FVector(BoxSize.Z, BoxSize.Y, BoxSize.X));

			DrawWireCylinder(PDI, ShapePosition,
				Transform.GetScaledAxis(EAxis::X), Transform.GetScaledAxis(EAxis::Y), Transform.GetScaledAxis(EAxis::Z),
				DrawColor, 1, 1, 20, SDPG_Foreground);

			break;
		}
		case EGenerateUVProjectionType::Planar :
		{
			//Simple Plane drawing.
			FVector QuadVertices[4] = {
				FVector(0, -1, 1),
				FVector(0, 1, 1),
				FVector(0, -1, -1),
				FVector(0, 1, -1)
			};

			for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
			{
				QuadVertices[VertexIndex] = ShapePosition + Transform.TransformPosition(QuadVertices[VertexIndex]);
			}

			PDI->DrawLine(QuadVertices[0], QuadVertices[1], DrawColor, SDPG_Foreground, 0, 0, false);
			PDI->DrawLine(QuadVertices[0], QuadVertices[2], DrawColor, SDPG_Foreground, 0, 0, false);
			PDI->DrawLine(QuadVertices[1], QuadVertices[3], DrawColor, SDPG_Foreground, 0, 0, false);
			PDI->DrawLine(QuadVertices[2], QuadVertices[3], DrawColor, SDPG_Foreground, 0, 0, false);

			break;
		}
	}
}

void FUVGenerationTool::SetShapeSettings(const FUVGenerationSettings& GenerationSettings)
{
	ShapeType = GenerationSettings.ProjectionType;
	ShapePosition = GenerationSettings.Position;
	ShapeSize = GenerationSettings.Size;
	ShapeRotation = GenerationSettings.Rotation;
}

bool FUVGenerationTool::IsHandlingInputs() const 
{
	const ECoordSystem DeltaCoordSystem = GetModeManager()->GetCoordSystem();
	const FWidget::EWidgetMode WidgetMode = GetModeManager()->GetWidgetMode();

	return (DeltaCoordSystem == ECoordSystem::COORD_Local || DeltaCoordSystem == COORD_World) &&
		(WidgetMode == FWidget::WM_Translate ||	WidgetMode == FWidget::WM_Rotate ||	WidgetMode == FWidget::WM_Scale);
}

#undef LOCTEXT_NAMESPACE