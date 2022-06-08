// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/TransformGizmo.h"
#include "BaseBehaviors/ClickDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseGizmos/AxisSources.h"
#include "BaseGizmos/GizmoElementGroup.h"
#include "BaseGizmos/GizmoElementShapes.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/ParameterSourcesFloat.h"
#include "BaseGizmos/StateTargets.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EditorModeTools.h"
#include "UnrealEdGlobals.h"
#include "UnrealEngine.h"

#define LOCTEXT_NAMESPACE "UTransformGizmo"

DEFINE_LOG_CATEGORY_STATIC(LogTransformGizmo, Log, All);

void UTransformGizmo::SetDisallowNegativeScaling(bool bDisallow)
{
	bDisallowNegativeScaling = bDisallow;
}

void UTransformGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	SetupBehaviors();
	SetupMaterials();

	// @todo: Gizmo element construction will be moved to the UEditorTransformGizmoBuilder to decouple
	// the rendered elements from the transform gizmo.
	GizmoElementRoot = NewObject<UGizmoElementGroup>();
	GizmoElementRoot->SetConstantScale(true);
	GizmoElementRoot->SetHoverMaterial(CurrentAxisMaterial);
	GizmoElementRoot->SetInteractMaterial(CurrentAxisMaterial);

	bInInteraction = false;
}

void UTransformGizmo::SetupBehaviors()
{
	// Add default mouse hover behavior
	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	HoverBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(HoverBehavior);

	// Add default mouse input behavior
	MouseBehavior = NewObject<UClickDragInputBehavior>();
	MouseBehavior->Initialize(this);
	MouseBehavior->SetDefaultPriority(FInputCapturePriority(FInputCapturePriority::DEFAULT_GIZMO_PRIORITY));
	AddInputBehavior(MouseBehavior);
}

void UTransformGizmo::SetupMaterials()
{
	UMaterial* AxisMaterialBase = GEngine->ArrowMaterial;

	AxisMaterialX = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialX->SetVectorParameterValue("GizmoColor", AxisColorX);

	AxisMaterialY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialY->SetVectorParameterValue("GizmoColor", AxisColorY);

	AxisMaterialZ = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialZ->SetVectorParameterValue("GizmoColor", AxisColorZ);

	GreyMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	GreyMaterial->SetVectorParameterValue("GizmoColor", GreyColor);

	WhiteMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	WhiteMaterial->SetVectorParameterValue("GizmoColor", WhiteColor);

	CurrentAxisMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	CurrentAxisMaterial->SetVectorParameterValue("GizmoColor", CurrentColor);

	OpaquePlaneMaterialXY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	OpaquePlaneMaterialXY->SetVectorParameterValue("GizmoColor", FLinearColor::White);

	TransparentVertexColorMaterial = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial.WidgetVertexColorMaterial"), NULL, LOAD_None, NULL);

	GridMaterial = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetGridVertexColorMaterial_Ma.WidgetGridVertexColorMaterial_Ma"), NULL,
		LOAD_None, NULL);
	if (!GridMaterial)
	{
		GridMaterial = TransparentVertexColorMaterial;
	}
}

void UTransformGizmo::Shutdown()
{
	ClearActiveTarget();
}

FTransform UTransformGizmo::GetGizmoTransform() const
{
	float Scale = 1.0f;

	if (TransformGizmoSource)
	{
		Scale = TransformGizmoSource->GetGizmoScale();
	}

	FTransform GizmoLocalToWorldTransform = CurrentTransform;
	GizmoLocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));

	return GizmoLocalToWorldTransform;
}

void UTransformGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bVisible && GizmoElementRoot && RenderAPI)
	{
		CurrentTransform = ActiveTarget->GetTransform();

		UGizmoElementBase::FRenderTraversalState RenderState;
		RenderState.Initialize(RenderAPI->GetSceneView(), GetGizmoTransform());
		GizmoElementRoot->Render(RenderAPI, RenderState);
	}
}

FInputRayHit UTransformGizmo::BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos)
{
	return UpdateHoveredPart(DevicePos);
}

void UTransformGizmo::OnBeginHover(const FInputDeviceRay& DevicePos)
{
}

bool UTransformGizmo::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	FInputRayHit RayHit = UpdateHoveredPart(DevicePos);
	return RayHit.bHit;
}

void UTransformGizmo::OnEndHover()
{
	if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
	{
		HitTarget->UpdateHoverState(false, static_cast<uint32>(LastHitPart));
	}
}

FInputRayHit UTransformGizmo::UpdateHoveredPart(const FInputDeviceRay& PressPos)
{
	if (!HitTarget)
	{
		return FInputRayHit();
	}

	FInputRayHit RayHit = HitTarget->IsHit(PressPos);

	ETransformGizmoPartIdentifier HitPart;
	if (RayHit.bHit && VerifyPartIdentifier(RayHit.HitIdentifier))
	{
		HitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
	}
	else
	{
		HitPart = ETransformGizmoPartIdentifier::Default;
	}

	if (HitPart != LastHitPart)
	{
		if (LastHitPart != ETransformGizmoPartIdentifier::Default)
		{
			HitTarget->UpdateHoverState(false, static_cast<uint32>(LastHitPart));
		}

		if (HitPart != ETransformGizmoPartIdentifier::Default)
		{
			HitTarget->UpdateHoverState(true, static_cast<uint32>(HitPart));
		}

		LastHitPart = HitPart;
	}

	return RayHit;
}

uint32 UTransformGizmo::GetMaxPartIdentifier() const
{
	return static_cast<uint32>(ETransformGizmoPartIdentifier::Max);
}

bool UTransformGizmo::VerifyPartIdentifier(uint32 InPartIdentifier) const
{
	if (InPartIdentifier >= GetMaxPartIdentifier())
	{
		UE_LOG(LogTransformGizmo, Warning, TEXT("Unrecognized transform gizmo part identifier %d, valid identifiers are between 0-%d."), 
			InPartIdentifier, GetMaxPartIdentifier());
		return false;
	}

	return true;
}



FInputRayHit UTransformGizmo::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	FInputRayHit RayHit;

	if (HitTarget)
	{
		RayHit = HitTarget->IsHit(PressPos);
		ETransformGizmoPartIdentifier HitPart;
		if (RayHit.bHit && VerifyPartIdentifier(RayHit.HitIdentifier))
		{
			HitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
		}
		else
		{
			HitPart = ETransformGizmoPartIdentifier::Default;
		}

		if (HitPart != ETransformGizmoPartIdentifier::Default)
		{
			LastHitPart = static_cast<ETransformGizmoPartIdentifier>(RayHit.HitIdentifier);
		}
	}

	return RayHit;
}

void UTransformGizmo::OnClickPress(const FInputDeviceRay& PressPos)
{
	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisType = EAxisList::X;
		OnClickPressAxis(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateYAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisType = EAxisList::Y;
		OnClickPressAxis(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateZAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisType = EAxisList::Z;
		OnClickPressAxis(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleXAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisType = EAxisList::X;
		OnClickPressAxis(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleYAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisType = EAxisList::Y;
		OnClickPressAxis(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleZAxis)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionAxis = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisType = EAxisList::Z;
		OnClickPressAxis(PressPos);
	}
	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXYPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisType = EAxisList::XY;
		OnClickPressPlanar(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateYZPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisType = EAxisList::YZ;
		OnClickPressPlanar(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXZPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisType = EAxisList::XZ;
		OnClickPressPlanar(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleXYPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisType = EAxisList::XY;
		OnClickPressPlanar(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleYZPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisType = EAxisList::YZ;
		OnClickPressPlanar(PressPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::ScaleXZPlanar)
	{
		InteractionAxisOrigin = CurrentTransform.GetLocation();
		InteractionNormal = GetWorldAxis(FVector::YAxisVector);
		InteractionAxisX = GetWorldAxis(FVector::ZAxisVector);
		InteractionAxisY = GetWorldAxis(FVector::XAxisVector);
		InteractionAxisType = EAxisList::XZ;
		OnClickPressPlanar(PressPos);
	}

	if (bInInteraction)
	{
		if (HitTarget&& LastHitPart != ETransformGizmoPartIdentifier::Default)
		{
			HitTarget->UpdateInteractingState(true, static_cast<uint32>(LastHitPart));
		}

		if (StateTarget)
		{
			StateTarget->BeginUpdate();
		}
	}
}

void UTransformGizmo::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (!bInInteraction)
	{
		return;
	}

	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateZAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleYAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleZAxis)
	{
		OnClickDragAxis(DragPos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXYPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateXZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXYPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleYZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXZPlanar)
	{
		OnClickDragPlanar(DragPos);
	}
}

void UTransformGizmo::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if (!bInInteraction)
	{
		return;
	}

	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateZAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleYAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleZAxis)
	{
		OnClickReleaseAxis(ReleasePos);
	}
	else if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXYPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateXZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXYPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleYZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::ScaleXZPlanar)
	{
		OnClickReleaseAxis(ReleasePos);
	}

	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}

	bInInteraction = false;

	if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
	{
		HitTarget->UpdateInteractingState(false, static_cast<uint32>(LastHitPart));
	}
}


void UTransformGizmo::OnTerminateDragSequence()
{
	if (!bInInteraction)
	{
		return;
	}

	if (StateTarget)
	{
		StateTarget->EndUpdate();
	}
	bInInteraction = false;

	if (HitTarget && LastHitPart != ETransformGizmoPartIdentifier::Default)
	{
		HitTarget->UpdateInteractingState(false, static_cast<uint32>(LastHitPart));
	}
}

void UTransformGizmo::UpdateMode()
{
	if (TransformGizmoSource && TransformGizmoSource->GetVisible())
	{
		EGizmoTransformMode NewMode = TransformGizmoSource->GetGizmoMode();
		EAxisList::Type NewAxisToDraw = TransformGizmoSource->GetGizmoAxisToDraw(NewMode);

		if (NewMode != CurrentMode)
		{
			EnableMode(CurrentMode, EAxisList::None);
			EnableMode(NewMode, NewAxisToDraw);

			CurrentMode = NewMode;
			CurrentAxisToDraw = NewAxisToDraw;
		}
		else if (NewAxisToDraw != CurrentAxisToDraw)
		{
			EnableMode(CurrentMode, NewAxisToDraw);
			CurrentAxisToDraw = NewAxisToDraw;
		}
	}
	else
	{
		EnableMode(CurrentMode, EAxisList::None);
		CurrentMode = EGizmoTransformMode::None;
	}
}

void UTransformGizmo::EnableMode(EGizmoTransformMode InMode, EAxisList::Type InAxisListToDraw)
{
	if (InMode == EGizmoTransformMode::Translate)
	{
		EnableTranslate(InAxisListToDraw);
	}
	else if (InMode == EGizmoTransformMode::Rotate)
	{
		EnableRotate(InAxisListToDraw);
	}
	else if (InMode == EGizmoTransformMode::Scale)
	{
		EnableScale(InAxisListToDraw);
	}
}

void UTransformGizmo::EnableTranslate(EAxisList::Type InAxisListToDraw)
{
	check(GizmoElementRoot);

	const bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	const bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	const bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	const bool bEnableAny = bEnableX || bEnableY || bEnableZ;

	if (bEnableX && TranslateXAxisElement == nullptr)
	{
		TranslateXAxisElement = MakeTranslateAxis(ETransformGizmoPartIdentifier::TranslateXAxis, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), AxisMaterialX);
		GizmoElementRoot->Add(TranslateXAxisElement);
	}

	if (bEnableY && TranslateYAxisElement == nullptr)
	{
		TranslateYAxisElement = MakeTranslateAxis(ETransformGizmoPartIdentifier::TranslateYAxis, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), AxisMaterialY);
		GizmoElementRoot->Add(TranslateYAxisElement);
	}

	if (bEnableZ && TranslateZAxisElement == nullptr)
	{
		TranslateZAxisElement = MakeTranslateAxis(ETransformGizmoPartIdentifier::TranslateZAxis, FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), AxisMaterialZ);
		GizmoElementRoot->Add(TranslateZAxisElement);
	}

	if (bEnableAny && TranslateScreenSpaceElement == nullptr)
	{
		TranslateScreenSpaceElement = MakeTranslateScreenSpaceHandle();
		GizmoElementRoot->Add(TranslateScreenSpaceElement);
	}

	if (TranslateXAxisElement)
	{
		TranslateXAxisElement->SetEnabled(bEnableX);
	}

	if (TranslateYAxisElement)
	{
		TranslateYAxisElement->SetEnabled(bEnableY);
	}

	if (TranslateZAxisElement)
	{
		TranslateZAxisElement->SetEnabled(bEnableZ);
	}

	if (TranslateScreenSpaceElement)
	{
		TranslateScreenSpaceElement->SetEnabled(bEnableAny);
	}

	EnablePlanarObjects(true, bEnableX, bEnableY, bEnableZ);
}

void UTransformGizmo::EnablePlanarObjects(bool bTranslate, bool bEnableX, bool bEnableY, bool bEnableZ)
{
	check(GizmoElementRoot);

	auto EnablePlanarElement = [this](
		TObjectPtr<UGizmoElementRectangle>& PlanarElement,
		ETransformGizmoPartIdentifier PartId,
		const FVector& Axis0,
		const FVector& Axis1,
		const FVector& Axis2,
		const FLinearColor& AxisColor,
		bool bEnable)
	{
		if (bEnable && PlanarElement == nullptr)
		{
			PlanarElement = MakePlanarHandle(PartId, Axis0, Axis1, Axis2, TransparentVertexColorMaterial, AxisColor);
			GizmoElementRoot->Add(PlanarElement);
		}

		if (PlanarElement)
		{
			PlanarElement->SetEnabled(bEnable);
		}
	};

	const bool bEnableXY = bEnableX && bEnableY;
	const bool bEnableYZ = bEnableY && bEnableZ;
	const bool bEnableXZ = bEnableX && bEnableZ;

	const FVector XAxis(1.0f, 0.0f, 0.0f);
	const FVector YAxis(0.0f, 1.0f, 0.0f);
	const FVector ZAxis(0.0f, 0.0f, 1.0f);

	if (bTranslate)
	{
		EnablePlanarElement(TranslatePlanarXYElement, ETransformGizmoPartIdentifier::TranslateXYPlanar, XAxis, YAxis, ZAxis, AxisColorZ, bEnableXY);
		EnablePlanarElement(TranslatePlanarYZElement, ETransformGizmoPartIdentifier::TranslateYZPlanar, YAxis, ZAxis, XAxis, AxisColorX, bEnableYZ);
		EnablePlanarElement(TranslatePlanarXZElement, ETransformGizmoPartIdentifier::TranslateXZPlanar, ZAxis, XAxis, YAxis, AxisColorY, bEnableXZ);
	}
	else
	{
		EnablePlanarElement(ScalePlanarXYElement, ETransformGizmoPartIdentifier::ScaleXYPlanar, XAxis, YAxis, ZAxis, AxisColorZ, bEnableXY);
		EnablePlanarElement(ScalePlanarYZElement, ETransformGizmoPartIdentifier::ScaleYZPlanar, YAxis, ZAxis, XAxis, AxisColorX, bEnableYZ);
		EnablePlanarElement(ScalePlanarXZElement, ETransformGizmoPartIdentifier::ScaleXZPlanar, ZAxis, XAxis, YAxis, AxisColorY, bEnableXZ);
	}
}

void UTransformGizmo::EnableRotate(EAxisList::Type InAxisListToDraw)
{
	const bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	const bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	const bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	const bool bEnableAll = bEnableX && bEnableY && bEnableZ;

	const FVector XAxis(1.0f, 0.0f, 0.0f);
	const FVector YAxis(0.0f, 1.0f, 0.0f);
	const FVector ZAxis(0.0f, 0.0f, 1.0f);

	if (bEnableX && RotateXAxisElement == nullptr)
	{
		RotateXAxisElement = MakeRotateAxis(ETransformGizmoPartIdentifier::RotateXAxis, XAxis, YAxis, ZAxis, AxisMaterialX, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateXAxisElement);
	}

	if (bEnableY && RotateYAxisElement == nullptr)
	{
		RotateYAxisElement = MakeRotateAxis(ETransformGizmoPartIdentifier::RotateYAxis, YAxis, ZAxis, XAxis, AxisMaterialY, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateYAxisElement);
	}

	if (bEnableZ && RotateZAxisElement == nullptr)
	{
		RotateZAxisElement = MakeRotateAxis(ETransformGizmoPartIdentifier::RotateZAxis, ZAxis, XAxis, YAxis, AxisMaterialZ, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateZAxisElement);
	}

	if (bEnableAll)
	{
		if (RotateScreenSpaceElement == nullptr)
		{
			RotateScreenSpaceElement = MakeRotateCircleHandle(ETransformGizmoPartIdentifier::RotateScreenSpace, RotateScreenSpaceRadius, RotateScreenSpaceCircleColor, false);
			GizmoElementRoot->Add(RotateScreenSpaceElement);
		}

		if (RotateOuterCircleElement == nullptr)
		{
			RotateOuterCircleElement = MakeRotateCircleHandle(ETransformGizmoPartIdentifier::Default, RotateOuterCircleRadius, RotateOuterCircleColor, false);
			GizmoElementRoot->Add(RotateOuterCircleElement);
		}

		if (RotateArcballOuterElement == nullptr)
		{
			RotateArcballOuterElement = MakeRotateCircleHandle(ETransformGizmoPartIdentifier::RotateArcball, RotateArcballOuterRadius, RotateArcballCircleColor, false);
			GizmoElementRoot->Add(RotateArcballOuterElement);
		}

		if (RotateArcballInnerElement == nullptr)
		{
			RotateArcballInnerElement = MakeRotateCircleHandle(ETransformGizmoPartIdentifier::RotateArcballInnerCircle, RotateArcballInnerRadius, RotateArcballCircleColor, true);
			GizmoElementRoot->Add(RotateArcballInnerElement);
		}
	}

	if (RotateXAxisElement)
	{
		RotateXAxisElement->SetEnabled(bEnableX);
	}

	if (RotateYAxisElement)
	{
		RotateYAxisElement->SetEnabled(bEnableY);
	}

	if (RotateZAxisElement)
	{
		RotateZAxisElement->SetEnabled(bEnableZ);
	}

	if (RotateScreenSpaceElement)
	{
		RotateScreenSpaceElement->SetEnabled(bEnableAll);
	}

	if (RotateOuterCircleElement)
	{
		RotateOuterCircleElement->SetEnabled(bEnableAll);
	}

	if (RotateArcballOuterElement)
	{
		RotateArcballOuterElement->SetEnabled(bEnableAll);
	}

	if (RotateArcballInnerElement)
	{ 
		RotateArcballInnerElement->SetEnabled(bEnableAll);
	}
}

void UTransformGizmo::EnableScale(EAxisList::Type InAxisListToDraw)
{
	check(GizmoElementRoot);

	bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	
	if (bEnableX && ScaleXAxisElement == nullptr)
	{
		ScaleXAxisElement = MakeScaleAxis(ETransformGizmoPartIdentifier::ScaleXAxis, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), AxisMaterialX);
		GizmoElementRoot->Add(ScaleXAxisElement);
	}

	if (bEnableY && ScaleYAxisElement == nullptr)
	{
		ScaleYAxisElement = MakeScaleAxis(ETransformGizmoPartIdentifier::ScaleYAxis, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), AxisMaterialY);
		GizmoElementRoot->Add(ScaleYAxisElement);
	}

	if (bEnableZ && ScaleZAxisElement == nullptr)
	{
		ScaleZAxisElement = MakeScaleAxis(ETransformGizmoPartIdentifier::ScaleZAxis, FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), AxisMaterialZ);
		GizmoElementRoot->Add(ScaleZAxisElement);
	}

	if ((bEnableX || bEnableY || bEnableZ) && ScaleUniformElement == nullptr)
	{
		ScaleUniformElement = MakeUniformScaleHandle();
		GizmoElementRoot->Add(ScaleUniformElement);
	}

	if (ScaleXAxisElement)
	{
		ScaleXAxisElement->SetEnabled(bEnableX);
	}

	if (ScaleYAxisElement)
	{
		ScaleYAxisElement->SetEnabled(bEnableY);
	}

	if (ScaleZAxisElement)
	{
		ScaleZAxisElement->SetEnabled(bEnableZ);
	}

	if (ScaleUniformElement)
	{
		ScaleUniformElement->SetEnabled(bEnableX || bEnableY || bEnableZ);
	}

	EnablePlanarObjects(false, bEnableX, bEnableY, bEnableZ);
}

void UTransformGizmo::UpdateCameraAxisSource()
{
	FViewCameraState CameraState;
	GetGizmoManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	if (CameraAxisSource != nullptr)
	{
		CameraAxisSource->Origin = ActiveTarget ? ActiveTarget->GetTransform().GetLocation() : FVector::ZeroVector;
		CameraAxisSource->Direction = -CameraState.Forward();
		CameraAxisSource->TangentX = CameraState.Right();
		CameraAxisSource->TangentY = CameraState.Up();
	}
}

void UTransformGizmo::Tick(float DeltaTime)
{
	UpdateMode();

	UpdateCameraAxisSource();
}

void UTransformGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}

	ActiveTarget = Target;

	// Set current mode to none, mode will be updated next Tick()
	CurrentMode = EGizmoTransformMode::None;

	if (!ActiveTarget)
	{
		return;
	}

	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}
	
	/* @todo - Add state target 
	
	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}
	StateTarget = UGizmoTransformChangeStateTarget::Construct(GizmoElementRoot,
		LOCTEXT("UCombinedTransformGizmoTransaction", "Transform"), TransactionProvider, this);
	StateTarget->DependentChangeSources.Add(MakeUnique<FTransformProxyChangeSource>(Target));
	*/

	CameraAxisSource = NewObject<UGizmoConstantFrameAxisSource>(this);
}

// @todo: This should either be named to "SetScale" or removed, since it can be done with ReinitializeGizmoTransform
void UTransformGizmo::SetNewChildScale(const FVector& NewChildScale)
{
	FTransform NewTransform = ActiveTarget->GetTransform();
	NewTransform.SetScale3D(NewChildScale);

	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}

void UTransformGizmo::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;
}

UGizmoElementArrow* UTransformGizmo::MakeTranslateAxis(ETransformGizmoPartIdentifier InPartId, const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial)
{
	UGizmoElementArrow* ArrowElement = NewObject<UGizmoElementArrow>();
	ArrowElement->SetPartIdentifier(static_cast<uint32>(InPartId));
	ArrowElement->SetHeadType(EGizmoElementArrowHeadType::Cone);
	ArrowElement->SetBase(InAxisDir * AxisLengthOffset);
	ArrowElement->SetDirection(InAxisDir);
	ArrowElement->SetSideDirection(InSideDir);
	ArrowElement->SetBodyLength(TranslateAxisLength);
	ArrowElement->SetBodyRadius(AxisRadius);
	ArrowElement->SetHeadLength(TranslateAxisConeHeight);
	ArrowElement->SetHeadRadius(TranslateAxisConeRadius);
	ArrowElement->SetNumSides(32);
	ArrowElement->SetMaterial(InMaterial);
	ArrowElement->SetViewDependentType(EGizmoElementViewDependentType::Axis);
	ArrowElement->SetViewDependentAxis(InAxisDir);
	return ArrowElement;
}

UGizmoElementArrow* UTransformGizmo::MakeScaleAxis(ETransformGizmoPartIdentifier InPartId, const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial)
{
	UGizmoElementArrow* ArrowElement = NewObject<UGizmoElementArrow>();
	ArrowElement->SetPartIdentifier(static_cast<uint32>(InPartId));
	ArrowElement->SetHeadType(EGizmoElementArrowHeadType::Cube);
	ArrowElement->SetBase(InAxisDir * AxisLengthOffset);
	ArrowElement->SetDirection(InAxisDir);
	ArrowElement->SetSideDirection(InSideDir);
	ArrowElement->SetBodyLength(ScaleAxisLength);
	ArrowElement->SetBodyRadius(AxisRadius);
	ArrowElement->SetHeadLength(ScaleAxisCubeDim);
	ArrowElement->SetNumSides(32);
	ArrowElement->SetMaterial(InMaterial);
	ArrowElement->SetViewDependentType(EGizmoElementViewDependentType::Axis);
	ArrowElement->SetViewDependentAxis(InAxisDir);
	return ArrowElement;
}

UGizmoElementBox* UTransformGizmo::MakeUniformScaleHandle()
{
	UGizmoElementBox* BoxElement = NewObject<UGizmoElementBox>();
	BoxElement->SetPartIdentifier(static_cast<uint32>(ETransformGizmoPartIdentifier::ScaleUniform));
	BoxElement->SetCenter(FVector::ZeroVector);
	BoxElement->SetUpDirection(FVector::UpVector);
	BoxElement->SetSideDirection(FVector::RightVector);
	BoxElement->SetDimensions(FVector(ScaleAxisCubeDim, ScaleAxisCubeDim, ScaleAxisCubeDim));
	BoxElement->SetMaterial(GreyMaterial);
	return BoxElement;
}

UGizmoElementRectangle* UTransformGizmo::MakePlanarHandle(ETransformGizmoPartIdentifier InPartId, const FVector& InUpDirection, const FVector& InSideDirection, const FVector& InPlaneNormal,
	UMaterialInterface* InMaterial, const FLinearColor& InVertexColor)
{
	FVector PlanarHandleCenter = (InUpDirection + InSideDirection) * PlanarHandleOffset;

	FColor LineColor = InVertexColor.ToFColor(false);
	FColor VertexColor = LineColor;
	VertexColor.A = LargeOuterAlpha;

	UGizmoElementRectangle* RectangleElement = NewObject<UGizmoElementRectangle>();
	RectangleElement->SetPartIdentifier(static_cast<uint32>(InPartId));
	RectangleElement->SetUpDirection(InUpDirection);
	RectangleElement->SetSideDirection(InSideDirection);
	RectangleElement->SetCenter(PlanarHandleCenter);
	RectangleElement->SetHeight(PlanarHandleSize);
	RectangleElement->SetWidth(PlanarHandleSize);
	RectangleElement->SetMaterial(InMaterial);
	RectangleElement->SetVertexColor(VertexColor);
	RectangleElement->SetLineColor(LineColor);
	RectangleElement->SetDrawLine(true);
	RectangleElement->SetDrawMesh(true);
	RectangleElement->SetHitMesh(true);
	RectangleElement->SetViewDependentType(EGizmoElementViewDependentType::Plane);
	RectangleElement->SetViewDependentAxis(InPlaneNormal);
	return RectangleElement;
}

UGizmoElementRectangle* UTransformGizmo::MakeTranslateScreenSpaceHandle()
{
	UGizmoElementRectangle* RectangleElement = NewObject<UGizmoElementRectangle>();
	RectangleElement->SetPartIdentifier(static_cast<uint32>(ETransformGizmoPartIdentifier::TranslateScreenSpace));
	RectangleElement->SetUpDirection(FVector::UpVector);
	RectangleElement->SetSideDirection(FVector::RightVector);
	RectangleElement->SetCenter(FVector::ZeroVector);
	RectangleElement->SetHeight(TranslateScreenSpaceHandleSize);
	RectangleElement->SetWidth(TranslateScreenSpaceHandleSize);
	RectangleElement->SetScreenSpace(true);
	RectangleElement->SetMaterial(TransparentVertexColorMaterial);
	RectangleElement->SetLineColor(ScreenSpaceColor);
	RectangleElement->SetHitMesh(true);
	RectangleElement->SetDrawMesh(false);
	RectangleElement->SetDrawLine(true);
	RectangleElement->SetHoverLineThicknessMultiplier(3.0);
	return RectangleElement;
}

UGizmoElementTorus* UTransformGizmo::MakeRotateAxis(ETransformGizmoPartIdentifier InPartId, const FVector& Normal, const FVector& TorusAxis0, const FVector& TorusAxis1,
	UMaterialInterface* InMaterial, UMaterialInterface* InCurrentMaterial)
{
	UGizmoElementTorus* RotateAxisElement = NewObject<UGizmoElementTorus>();
	RotateAxisElement->SetPartIdentifier(static_cast<uint32>(InPartId));
	RotateAxisElement->SetCenter(FVector::ZeroVector);
	RotateAxisElement->SetOuterRadius(UTransformGizmo::RotateAxisOuterRadius);
	RotateAxisElement->SetOuterSegments(UTransformGizmo::RotateAxisOuterSegments);
	RotateAxisElement->SetInnerRadius(UTransformGizmo::RotateAxisInnerRadius);
	RotateAxisElement->SetInnerSlices(UTransformGizmo::RotateAxisInnerSlices);
	RotateAxisElement->SetNormal(Normal);
	RotateAxisElement->SetBeginAxis(TorusAxis0);
	RotateAxisElement->SetPartial(true);
	RotateAxisElement->SetAngle(PI);
	RotateAxisElement->SetViewDependentType(EGizmoElementViewDependentType::Plane);
	RotateAxisElement->SetViewDependentAxis(Normal);
	RotateAxisElement->SetViewAlignType(EGizmoElementViewAlignType::Axial);
	RotateAxisElement->SetViewAlignAxis(Normal);
	RotateAxisElement->SetViewAlignNormal(TorusAxis1);
	RotateAxisElement->SetMaterial(InMaterial);
	return RotateAxisElement;
}

UGizmoElementCircle* UTransformGizmo::MakeRotateCircleHandle(ETransformGizmoPartIdentifier InPartId, float InRadius, const FLinearColor& InColor, float bFill)
{
	UGizmoElementCircle* CircleElement = NewObject<UGizmoElementCircle>();
	CircleElement->SetPartIdentifier(static_cast<uint32>(InPartId));
	CircleElement->SetCenter(FVector::ZeroVector);
	CircleElement->SetRadius(InRadius);
	CircleElement->SetNormal(-FVector::ForwardVector);
	CircleElement->SetLineColor(InColor);
	CircleElement->SetViewAlignType(EGizmoElementViewAlignType::PointOnly);
	CircleElement->SetViewAlignNormal(-FVector::ForwardVector);

	if (bFill)
	{
		CircleElement->SetVertexColor(InColor.ToFColor(true));
		CircleElement->SetMaterial(WhiteMaterial);
	}
	else
	{
		CircleElement->SetDrawLine(true);
		CircleElement->SetHitLine(true);
		CircleElement->SetDrawMesh(false);
		CircleElement->SetHitMesh(false);
	}

	return CircleElement;
}


void UTransformGizmo::ClearActiveTarget()
{
	StateTarget = nullptr;
	ActiveTarget = nullptr;
}


bool UTransformGizmo::PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const
{
	SnappedPositionOut = WorldPosition;
#if 0
	// only snap if we want snapping obvs
	if (bSnapToWorldGrid == false)
	{
		return false;
	}

	// only snap to world grid when using world axes
	if (GetGizmoManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() != EToolContextCoordinateSystem::World)
	{
		return false;
	}

	FSceneSnapQueryRequest Request;
	Request.RequestType = ESceneSnapQueryType::Position;
	Request.TargetTypes = ESceneSnapQueryTargetType::Grid;
	Request.Position = WorldPosition;
	if ( bGridSizeIsExplicit )
	{
		Request.GridSize = ExplicitGridSize;
	}
	TArray<FSceneSnapQueryResult> Results;
	if (GetGizmoManager()->GetContextQueriesAPI()->ExecuteSceneSnapQuery(Request, Results))
	{
		SnappedPositionOut = Results[0].Position;
		return true;
	};
#endif
	return false;
}


FQuat UTransformGizmo::RotationSnapFunction(const FQuat& DeltaRotation) const
{
	FQuat SnappedDeltaRotation = DeltaRotation;
#if 0
	// only snap if we want snapping 
	if (bSnapToWorldRotGrid)
	{
		FSceneSnapQueryRequest Request;
		Request.RequestType   = ESceneSnapQueryType::Rotation;
		Request.TargetTypes   = ESceneSnapQueryTargetType::Grid;
		Request.DeltaRotation = DeltaRotation;
		if ( bRotationGridSizeIsExplicit )
		{
			Request.RotGridSize = ExplicitRotationGridSize;
		}
		TArray<FSceneSnapQueryResult> Results;
		if (GetGizmoManager()->GetContextQueriesAPI()->ExecuteSceneSnapQuery(Request, Results))
		{
			SnappedDeltaRotation = Results[0].DeltaRotation;
		};
	}
#endif	
	return SnappedDeltaRotation;
}

FVector UTransformGizmo::GetWorldAxis(const FVector& InAxis)
{
	if (TransformGizmoSource->GetGizmoCoordSystemSpace() == EToolContextCoordinateSystem::Local)
	{
		return CurrentTransform.GetRotation().RotateVector(InAxis);
	}
	
	return InAxis;
}

void UTransformGizmo::OnClickPressAxis(const FInputDeviceRay& InPressPos)
{
	// Find interaction start point and parameter.
	FVector NearestPt; 
	float RayNearestParam, LineNearestParam;
	GizmoMath::NearestPointOnLineToRay(InteractionAxisOrigin, InteractionAxis,
		InPressPos.WorldRay.Origin, InPressPos.WorldRay.Direction,
		InteractionStartPoint, LineNearestParam,
		NearestPt, RayNearestParam);

	InteractionCurrPoint = InteractionStartPoint;
	bInInteraction = true;
}

void UTransformGizmo::OnClickDragAxis(const FInputDeviceRay& InDragPos)
{
	float RayNearestParam, AxisNearestParam;
	FVector RayNearestPt, AxisNearestPoint;
	GizmoMath::NearestPointOnLineToRay(InteractionAxisOrigin, InteractionAxis,
		InDragPos.WorldRay.Origin, InDragPos.WorldRay.Direction,
		AxisNearestPoint, AxisNearestParam,
		RayNearestPt, RayNearestParam);

	FVector Delta = AxisNearestPoint - InteractionCurrPoint;
	InteractionCurrPoint = AxisNearestPoint;

	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYAxis ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateZAxis)
	{
		ApplyTranslateDelta(Delta);
	}
	else
	{
		Delta *= ScaleMultiplier;
		ApplyScaleDelta(Delta);
	}
}

void UTransformGizmo::OnClickReleaseAxis(const FInputDeviceRay& InReleasePos)
{
	bInInteraction = false;
}

void UTransformGizmo::OnClickPressPlanar(const FInputDeviceRay& PressPos)
{
	// if ray is parallel to plane, nothing has been hit
	if (FMath::IsNearlyZero(FVector::DotProduct(InteractionNormal, PressPos.WorldRay.Direction)))
	{
		return;
	}

	FPlane Plane(InteractionAxisOrigin, InteractionNormal);
	double HitDepth = FMath::RayPlaneIntersectionParam(PressPos.WorldRay.Origin, PressPos.WorldRay.Direction, Plane);
	if (HitDepth < 0)
	{
		return;
	}

	FVector HitPoint = PressPos.WorldRay.Origin + PressPos.WorldRay.Direction * HitDepth;
	InteractionStartPoint = HitPoint;
	InteractionCurrPoint = InteractionStartPoint;

	bInInteraction = true;
}

void UTransformGizmo::OnClickDragPlanar(const FInputDeviceRay& DragPos)
{
	// if ray is parallel to plane, nothing has been hit
	if (FMath::IsNearlyZero(FVector::DotProduct(InteractionNormal, DragPos.WorldRay.Direction)))
	{
		return;
	}

	FPlane Plane(InteractionAxisOrigin, InteractionNormal);
	double HitDepth = FMath::RayPlaneIntersectionParam(DragPos.WorldRay.Origin, DragPos.WorldRay.Direction, Plane);
	if (HitDepth < 0)
	{
		return;
	}

	FVector HitPoint = DragPos.WorldRay.Origin + DragPos.WorldRay.Direction * HitDepth;
	FVector Delta = HitPoint - InteractionCurrPoint;

	InteractionCurrPoint = HitPoint;

	if (LastHitPart == ETransformGizmoPartIdentifier::TranslateXYPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateYZPlanar ||
		LastHitPart == ETransformGizmoPartIdentifier::TranslateXZPlanar)
	{
		ApplyTranslateDelta(Delta);
	}
	else
	{
		const float ScaleMax = Delta.GetMax();
		const float ScaleMin = Delta.GetMin();
		const float ScaleApplied = (ScaleMax > -ScaleMin) ? ScaleMax * ScaleMultiplier : ScaleMin * ScaleMultiplier;

		FVector ScaleDelta(InteractionAxisType & EAxisList::X ? ScaleApplied : 0.0,
						   InteractionAxisType & EAxisList::Y ? ScaleApplied : 0.0,
						   InteractionAxisType & EAxisList::Z ? ScaleApplied : 0.0);

		ApplyScaleDelta(ScaleDelta);
	}
}

void UTransformGizmo::OnClickReleasePlanar(const FInputDeviceRay& InReleasePos)
{
	bInInteraction = false;
}

void UTransformGizmo::ApplyTranslateDelta(const FVector& InTranslateDelta)
{
	CurrentTransform.AddToTranslation(InTranslateDelta);
	ActiveTarget->SetTransform(CurrentTransform);
}

void UTransformGizmo::ApplyScaleDelta(const FVector& InScaleDelta)
{
	FVector StartScale = CurrentTransform.GetScale3D();
	FVector NewScale = StartScale + InScaleDelta * InteractionAxis;
	CurrentTransform.SetScale3D(NewScale);
	ActiveTarget->SetTransform(CurrentTransform);
}

#undef LOCTEXT_NAMESPACE
