// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/TransformGizmo.h"
#include "BaseGizmos/AxisPositionGizmo.h"
#include "BaseGizmos/TransformSources.h"
#include "EditorGizmos/EditorAxisSources.h"
#include "EditorGizmos/EditorTransformGizmoSource.h"
#include "EditorGizmos/EditorTransformProxy.h"
#include "EditorGizmos/EditorParameterToTransformAdapters.h"
#include "EditorGizmos/GizmoArrowObject.h"
#include "EditorGizmos/GizmoBoxObject.h"
#include "EditorGizmos/GizmoConeObject.h"
#include "EditorGizmos/GizmoCylinderObject.h"
#include "EditorGizmos/GizmoGroupObject.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EditorModeTools.h"
#include "UnrealEdGlobals.h"
#include "UnrealEngine.h"

#define LOCTEXT_NAMESPACE "UTransformGizmo"

constexpr float UTransformGizmo::AXIS_LENGTH;
constexpr float UTransformGizmo::AXIS_RADIUS;
constexpr float UTransformGizmo::AXIS_CONE_ANGLE;
constexpr float UTransformGizmo::AXIS_CONE_HEIGHT;
constexpr float UTransformGizmo::AXIS_CONE_HEAD_OFFSET;
constexpr float UTransformGizmo::AXIS_CUBE_SIZE;
constexpr float UTransformGizmo::AXIS_CUBE_HEAD_OFFSET;
constexpr float UTransformGizmo::TRANSLATE_ROTATE_AXIS_CIRCLE_RADIUS;
constexpr float UTransformGizmo::TWOD_AXIS_CIRCLE_RADIUS;
constexpr float UTransformGizmo::INNER_AXIS_CIRCLE_RADIUS;
constexpr float UTransformGizmo::OUTER_AXIS_CIRCLE_RADIUS;
constexpr float UTransformGizmo::ROTATION_TEXT_RADIUS;
constexpr int32 UTransformGizmo::AXIS_CIRCLE_SIDES;
constexpr float UTransformGizmo::ARCALL_RELATIVE_INNER_SIZE;
constexpr float UTransformGizmo::AXIS_LENGTH_SCALE;
constexpr float UTransformGizmo::AXIS_LENGTH_SCALE_OFFSET;

constexpr FLinearColor UTransformGizmo::AxisColorX;
constexpr FLinearColor UTransformGizmo::AxisColorY;
constexpr FLinearColor UTransformGizmo::AxisColorZ;
constexpr FLinearColor UTransformGizmo::ScreenAxisColor;
constexpr FColor UTransformGizmo::PlaneColorXY;
constexpr FColor UTransformGizmo::ArcBallColor;
constexpr FColor UTransformGizmo::ScreenSpaceColor;
constexpr FColor UTransformGizmo::CurrentColor;

void UTransformGizmo::SetDisallowNegativeScaling(bool bDisallow)
{
	if (bDisallowNegativeScaling != bDisallow)
	{
		bDisallowNegativeScaling = bDisallow;
		for (UInteractiveGizmo* SubGizmo : this->ActiveGizmos)
		{
			if (UAxisPositionGizmo* CastGizmo = Cast<UAxisPositionGizmo>(SubGizmo))
			{
				if (UGizmoAxisScaleParameterSource* ParamSource = Cast<UGizmoAxisScaleParameterSource>(CastGizmo->ParameterSource.GetObject()))
				{
					ParamSource->bClampToZero = bDisallow;
				}
			}
			/* @todo
			if (UPlanePositionGizmo* CastGizmo = Cast<UEditorPlanePositionGizmo>(SubGizmo))
			{
				if (UGizmoPlaneScaleParameterSource* ParamSource = Cast<UGizmoPlaneScaleParameterSource>(CastGizmo->ParameterSource.GetObject()))
				{
					ParamSource->bClampToZero = bDisallow;
				}
			}
			*/
		}
	}
}

void UTransformGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	UMaterial* AxisMaterialBase = GEngine->ArrowMaterial;

	AxisMaterialX = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialX->SetVectorParameterValue("GizmoColor", AxisColorX);

	AxisMaterialY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialY->SetVectorParameterValue("GizmoColor", AxisColorY);

	AxisMaterialZ = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialZ->SetVectorParameterValue("GizmoColor", AxisColorZ);

	CurrentAxisMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	CurrentAxisMaterial->SetVectorParameterValue("GizmoColor", CurrentColor);

	OpaquePlaneMaterialXY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	OpaquePlaneMaterialXY->SetVectorParameterValue("GizmoColor", FLinearColor::White);

	TransparentPlaneMaterialXY = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial.WidgetVertexColorMaterial"), NULL, LOAD_None, NULL);

	GridMaterial = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetGridVertexColorMaterial_Ma.WidgetGridVertexColorMaterial_Ma"), NULL,
		LOAD_None, NULL);
	if (!GridMaterial)
	{
		GridMaterial = TransparentPlaneMaterialXY;
	}
}


void UTransformGizmo::Shutdown()
{
	ClearActiveTarget();
}

void UTransformGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bVisible && GizmoGroupObject)
	{
		GizmoGroupObject->Render(RenderAPI);
	}
}

void UTransformGizmo::UpdateMode()
{
	if (TransformSource && TransformSource->GetVisible())
	{
		EGizmoTransformMode NewMode = TransformSource->GetGizmoMode();
		EAxisList::Type NewAxisToDraw = TransformSource->GetGizmoAxisToDraw(NewMode);

		if (NewMode != CurrentMode)
		{
			ActiveObjects.Empty();
			EnableMode(CurrentMode, EAxisList::None);
			EnableMode(NewMode, NewAxisToDraw);

			CurrentMode = NewMode;
			CurrentAxisToDraw = NewAxisToDraw;
		}
		else if (NewAxisToDraw != CurrentAxisToDraw)
		{
			ActiveObjects.Empty();
			EnableMode(CurrentMode, NewAxisToDraw);
			CurrentAxisToDraw = NewAxisToDraw;
		}
	}
	else
	{
		ActiveObjects.Empty();
		EnableMode(CurrentMode, EAxisList::None);
		CurrentMode = EGizmoTransformMode::None;
	}
}

void UTransformGizmo::UpdateCoordSystem()
{
	// Note: the following will change with upcoming changes to gizmo objects
	if (ActiveTarget && ActiveObjects.Num() > 0)
	{
		EToolContextCoordinateSystem Space = EToolContextCoordinateSystem::World;
		float Scale = 1.0f;

		if (TransformSource)
		{
			Space = TransformSource->GetGizmoCoordSystemSpace();
			Scale = TransformSource->GetGizmoScale();
		}

		FTransform LocalToWorldTransform = ActiveTarget->GetTransform();
		if (Space == EToolContextCoordinateSystem::World)
		{
			LocalToWorldTransform.SetRotation(FQuat::Identity);
		}

		for (UGizmoBaseObject* Object : ActiveObjects)
		{
			Object->SetWorldLocalState(Space == EToolContextCoordinateSystem::World);
			Object->SetLocalToWorldTransform(LocalToWorldTransform);
			Object->SetGizmoScale(Scale);
		}
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

void UTransformGizmo::EnableObject(UGizmoBaseObject* InGizmoObject, EAxisList::Type InGizmoAxis, EAxisList::Type InAxisListToDraw)
{
	if (static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(InGizmoAxis))
	{
		InGizmoObject->SetVisibility(true);
		ActiveObjects.Add(InGizmoObject);
	}
	else
	{
		InGizmoObject->SetVisibility(false);
	}
}

void UTransformGizmo::EnableTranslate(EAxisList::Type InAxisListToDraw)
{
	if (ensure(AxisXObject && AxisYObject && AxisZObject))
	{
		EnableObject(AxisXObject, EAxisList::X, InAxisListToDraw);
		EnableObject(AxisYObject, EAxisList::Y, InAxisListToDraw);
		EnableObject(AxisZObject, EAxisList::Z, InAxisListToDraw);
	}
}

void UTransformGizmo::EnableRotate(EAxisList::Type InAxisListToDraw)
{
	// @todo
}

void UTransformGizmo::EnableScale(EAxisList::Type InAxisListToDraw)
{
	if (ensure(ScaleAxisXObject && ScaleAxisYObject && ScaleAxisZObject))
	{
		EnableObject(ScaleAxisXObject, EAxisList::X, InAxisListToDraw);
		EnableObject(ScaleAxisYObject, EAxisList::Y, InAxisListToDraw);
		EnableObject(ScaleAxisZObject, EAxisList::Z, InAxisListToDraw);
	}
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

	UpdateCoordSystem();

	UpdateCameraAxisSource();
}

void UTransformGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}

	ActiveTarget = Target;

	if (!ActiveTarget)
	{
		return;
	}

	// create group object to which all active objects will be added
	GizmoGroupObject = NewObject<UGizmoGroupObject>();

	// root component provides local X/Y/Z axis, identified by AxisIndex
	AxisXSource = UGizmoEditorAxisSource::Construct(0, true, this);
	AxisYSource = UGizmoEditorAxisSource::Construct(1, true, this);
	AxisZSource = UGizmoEditorAxisSource::Construct(2, true, this);

	auto MakeArrowObjectFunc = [](FVector Axis, UMaterialInterface* Material, UMaterialInterface* CurrentMaterial)
	{
		UGizmoArrowObject* ArrowObject = NewObject<UGizmoArrowObject>();
		ArrowObject->CylinderObject->Direction = Axis;
		ArrowObject->ConeObject->Direction = -Axis;
		ArrowObject->SetMaterial(Material);
		ArrowObject->SetCurrentMaterial(CurrentMaterial);
		return ArrowObject;
	};

	AxisXObject = MakeArrowObjectFunc(FVector(1.0f, 0.0f, 0.0f), AxisMaterialX, CurrentAxisMaterial);
	AxisYObject = MakeArrowObjectFunc(FVector(0.0f, 1.0f, 0.0f), AxisMaterialY, CurrentAxisMaterial);
	AxisZObject = MakeArrowObjectFunc(FVector(0.0f, 0.0f, 1.0f), AxisMaterialZ, CurrentAxisMaterial);

	UGizmoScaledAndUnscaledTransformSources* AxisTransformSource = UGizmoScaledAndUnscaledTransformSources::Construct(
		UGizmoTransformProxyTransformSource::Construct(ActiveTarget, this),
		UGizmoObjectWorldTransformSource::Construct(GizmoGroupObject, this));
	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}
	StateTarget = UGizmoObjectTransformChangeStateTarget::Construct(GizmoGroupObject,
		LOCTEXT("UTransformGizmoTransaction", "Transform"), TransactionProvider, this);
	StateTarget->DependentChangeSources.Add(MakeUnique<FTransformProxyChangeSource>(Target));

	CameraAxisSource = NewObject<UGizmoConstantFrameAxisSource>(this);

	{
		AddAxisTranslationGizmo(AxisXObject, AxisXSource, AxisTransformSource, StateTarget, EAxisList::X, AxisColorX);
		GizmoGroupObject->Add(AxisXObject);
	}

	{
		AddAxisTranslationGizmo(AxisYObject, AxisYSource, AxisTransformSource, StateTarget, EAxisList::Y, AxisColorY);
		GizmoGroupObject->Add(AxisYObject);
	}

	{
		AddAxisTranslationGizmo(AxisZObject,  AxisZSource, AxisTransformSource, StateTarget, EAxisList::Z, AxisColorZ);
		GizmoGroupObject->Add(AxisZObject);
	}

/*
	// @todo: add plane translation
	if (GizmoActor->TranslateYZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateYZ, GizmoComponent, AxisXSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateYZ);
	}
	if (GizmoActor->TranslateXZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXZ, GizmoComponent, AxisYSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateXZ);
	}
	if (GizmoActor->TranslateXY != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXY, GizmoComponent, AxisZSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateXY);
	}

	// @todo: finish rotation implementation
	if (GizmoActor->RotateX != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateX, GizmoComponent, AxisXSource, SpaceSource, TransformSource, StateTarget, EAxisList::X, AxisColorX);
		ActiveComponents.Add(GizmoActor->RotateX);
	}
	if (GizmoActor->RotateY != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateY, GizmoComponent, AxisYSource, SpaceSource, TransformSource, StateTarget, EAxisList::Y, AxisColorY);
		ActiveComponents.Add(GizmoActor->RotateY);
	}
	if (GizmoActor->RotateZ != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateZ, GizmoComponent, AxisZSource, SpaceSource, TransformSource, StateTarget, EAxisList::Z, AxisColorZ);
		ActiveComponents.Add(GizmoActor->RotateZ);
	}
*/

	// Create objects for scale gizmo
	ScaleAxisXObject = MakeArrowObjectFunc(FVector(1.0f, 0.0f, 0.0f), AxisMaterialX, CurrentAxisMaterial); 
	ScaleAxisYObject = MakeArrowObjectFunc(FVector(0.0f, 1.0f, 0.0f), AxisMaterialY, CurrentAxisMaterial); 
	ScaleAxisZObject = MakeArrowObjectFunc(FVector(0.0f, 0.0f, 1.0f), AxisMaterialZ, CurrentAxisMaterial); 

	// only need these if scaling enabled. Essentially these are just the unit axes, regardless
	// of what 3D axis is in use, we will tell the ParameterSource-to-3D-Scale mapper to
	// use the coordinate axes
	UnitAxisXSource = UGizmoEditorAxisSource::Construct(0, false, this);
	UnitAxisYSource = UGizmoEditorAxisSource::Construct(1, false, this);
	UnitAxisZSource = UGizmoEditorAxisSource::Construct(2, false, this);

/*
	// @todo: add uniform scale handle
	if (GizmoActor->UniformScale != nullptr)
	{
		AddUniformScaleGizmo(GizmoActor->UniformScale, GizmoComponent, CameraAxisSource, CameraAxisSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->UniformScale);
	}
*/

	{
		AddAxisScaleGizmo(ScaleAxisXObject, AxisXSource, UnitAxisXSource, AxisTransformSource, StateTarget, EAxisList::X, AxisColorX);
		GizmoGroupObject->Add(ScaleAxisXObject);
	}

	{
		AddAxisScaleGizmo(ScaleAxisYObject, AxisYSource, UnitAxisYSource, AxisTransformSource, StateTarget, EAxisList::Y, AxisColorY);
		GizmoGroupObject->Add(ScaleAxisYObject);
	}

	{
		AddAxisScaleGizmo(ScaleAxisZObject, AxisZSource, UnitAxisZSource, AxisTransformSource, StateTarget, EAxisList::Z, AxisColorZ);
		GizmoGroupObject->Add(ScaleAxisZObject);
	}

/*
	// @todo: add plane scale
	if (GizmoActor->PlaneScaleYZ != nullptr)
	{
		AddPlaneScaleGizmo(GizmoActor->PlaneScaleYZ, GizmoComponent, AxisXSource, UnitAxisXSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->PlaneScaleYZ);
		NonuniformScaleComponents.Add(GizmoActor->PlaneScaleYZ);
	}
	if (GizmoActor->PlaneScaleXZ != nullptr)
	{
		AddPlaneScaleGizmo(GizmoActor->PlaneScaleXZ, GizmoComponent, AxisYSource, UnitAxisYSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->PlaneScaleXZ);
		NonuniformScaleComponents.Add(GizmoActor->PlaneScaleXZ);
	}
	if (GizmoActor->PlaneScaleXY != nullptr)
	{
		AddPlaneScaleGizmo(GizmoActor->PlaneScaleXY, GizmoComponent, AxisZSource, UnitAxisZSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->PlaneScaleXY);
		NonuniformScaleComponents.Add(GizmoActor->PlaneScaleXY);
	}
*/ 

	GizmoGroupObject->SetVisibility(false);
	CurrentMode = EGizmoTransformMode::None;
}


void UTransformGizmo::ReinitializeGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	// The underlying proxy has an existing way to reinitialize its transform without callbacks.
	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}


void UTransformGizmo::SetNewGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	check(ActiveTarget != nullptr);

	StateTarget->BeginUpdate();

	ActiveTarget->SetTransform(NewTransform);

	StateTarget->EndUpdate();
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

UInteractiveGizmo* UTransformGizmo::AddAxisTranslationGizmo(
	UGizmoArrowObject * InArrowObject,
	IGizmoAxisSource * InAxisSource,
	IGizmoTransformSource * InTransformSource,
	IGizmoStateTarget * InStateTarget,
	EAxisList::Type InAxisType,
	const FLinearColor InAxisColor)
{
	check(InArrowObject);
	check(InArrowObject->CylinderObject);
	check(InArrowObject->ConeObject);

	// create axis-position gizmo, axis-position parameter will drive translation
	UAxisPositionGizmo* TranslateGizmo = Cast<UAxisPositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisPositionBuilderIdentifier, FString(), this));
	check(TranslateGizmo);

	InArrowObject->CylinderObject->Length = AXIS_LENGTH;
	InArrowObject->CylinderObject->Radius = AXIS_RADIUS;
	InArrowObject->bHasConeHead = true;

	InArrowObject->ConeObject->Angle = FMath::DegreesToRadians(AXIS_CONE_ANGLE);
	InArrowObject->ConeObject->Height = AXIS_CONE_HEIGHT;
	InArrowObject->ConeObject->Offset = -(AXIS_LENGTH + AXIS_CONE_HEAD_OFFSET);

	// axis source provides the translation axis
	TranslateGizmo->AxisSource = Cast<UObject>(InAxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	UGizmoEditorAxisTranslationParameterSource* ParamSource = UGizmoEditorAxisTranslationParameterSource::Construct(InAxisSource, InTransformSource, this);
	ParamSource->AxisTranslationParameterSource->PositionConstraintFunction = [this](const FVector& Pos, FVector& Snapped) { return PositionSnapFunction(Pos, Snapped); };
	TranslateGizmo->ParameterSource = ParamSource;

	UGizmoObjectHitTarget* HitTarget = UGizmoObjectHitTarget::Construct(InArrowObject, this);

	TranslateGizmo->HitTarget = HitTarget;
	TranslateGizmo->StateTarget = Cast<UObject>(InStateTarget);

	TranslateGizmo->ShouldUseCustomDestinationFunc = [this]() { return ShouldAlignDestination(); };
	TranslateGizmo->CustomDestinationFunc =
		[this](const UAxisPositionGizmo::FCustomDestinationParams& Params, FVector& OutputPoint) {
		return DestinationAlignmentRayCaster(*Params.WorldRay, OutputPoint);
	};

	ActiveGizmos.Add(TranslateGizmo);

	return TranslateGizmo;
}

UInteractiveGizmo* UTransformGizmo::AddPlaneTranslationGizmo(
	IGizmoAxisSource* InAxisSource,
	IGizmoTransformSource* InTransformSource,
	IGizmoStateTarget* InStateTarget)
{
	/* @todo
	// create axis-position gizmo, axis-position parameter will drive translation
	UEditorPlanePositionGizmo* TranslateGizmo = Cast<UEditorPlanePositionGizmo>(GetGizmoManager()->CreateGizmo(
		UEditorInteractiveGizmoManager::DefaultEditorPlanePositionBuilderIdentifier, FString(), this));
	check(TranslateGizmo);

	// axis source provides the translation axis
	TranslateGizmo->AxisSource = Cast<UObject>(InAxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	UGizmoPlaneTranslationParameterSource* ParamSource = UGizmoPlaneTranslationParameterSource::Construct(InAxisSource, InTransformSource, this);
	ParamSource->PositionConstraintFunction = [this](const FVector& Pos, FVector& Snapped) { return PositionSnapFunction(Pos, Snapped); };
	TranslateGizmo->ParameterSource = ParamSource;

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(InAxisComponent, this);
	if (this->UpdateHoverFunctionComponent)
	{
		HitTarget->UpdateHoverFunction = [InAxisComponent, this](bool bHovering) { this->UpdateHoverFunctionComponent(InAxisComponent, bHovering); };
	}
	TranslateGizmo->HitTarget = HitTarget;

	TranslateGizmo->StateTarget = Cast<UObject>(InStateTarget);

	TranslateGizmo->ShouldUseCustomDestinationFunc = [this]() { return ShouldAlignDestination(); };
	TranslateGizmo->CustomDestinationFunc =
		[this](const UEditorPlanePositionGizmo::FCustomDestinationParams& Params, FVector& OutputPoint) {
		return DestinationAlignmentRayCaster(*Params.WorldRay, OutputPoint);
	};

	ActiveGizmos.Add(TranslateGizmo);
	return TranslateGizmo;
	*/
	return nullptr;
}

UInteractiveGizmo* UTransformGizmo::AddAxisRotationGizmo(
	IGizmoAxisSource* InAxisSource,
	IGizmoTransformSource* InTransformSource,
	IGizmoStateTarget* InStateTarget,
	EAxisList::Type InAxisType,
	const FLinearColor InAxisColor)
{
	/* @todo
	// create axis-angle gizmo, angle will drive axis-rotation
	UEditorAxisAngleGizmo* RotateGizmo = Cast<UEditorAxisAngleGizmo>(GetGizmoManager()->CreateGizmo(
		UEditorInteractiveGizmoManager::DefaultEditorAxisAngleBuilderIdentifier, FString(), this));
	check(RotateGizmo);

	// axis source provides the translation axis
	RotateGizmo->Initialize(InAxisType, InAxisColor, CurrentColor);

	// axis source provides the rotation axis
	RotateGizmo->AxisSource = Cast<UObject>(InAxisSource);

	// space source provides coordinate space info specific to the gizmo
	RotateGizmo->GizmoSpaceSource = Cast<UObject>(SpaceSource);

	// parameter source maps angle-parameter-change to rotation of TransformSource's transform
	UGizmoAxisRotationParameterSource* AngleSource = UGizmoAxisRotationParameterSource::Construct(InAxisSource, InTransformSource, this);
	AngleSource->RotationConstraintFunction = [this](const FQuat& DeltaRotation){ return RotationSnapFunction(DeltaRotation); };
	RotateGizmo->AngleSource = AngleSource;

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(InAxisComponent, this);
	if (this->UpdateHoverFunctionComponent)
	{
		HitTarget->UpdateHoverFunction = [InAxisComponent, this](bool bHovering) { this->UpdateHoverFunctionComponent(InAxisComponent, bHovering); };
	}
	RotateGizmo->HitTarget = HitTarget;

	RotateGizmo->StateTarget = Cast<UObject>(StateTarget);

	RotateGizmo->ShouldUseCustomDestinationFunc = [this]() { return ShouldAlignDestination(); };
	RotateGizmo->CustomDestinationFunc =
		[this](const UEditorAxisAngleGizmo::FCustomDestinationParams& Params, FVector& OutputPoint) {
		return DestinationAlignmentRayCaster(*Params.WorldRay, OutputPoint);
	};

	ActiveGizmos.Add(RotateGizmo);

	return RotateGizmo;
	*/
	return nullptr;
}


UInteractiveGizmo* UTransformGizmo::AddAxisScaleGizmo(
	UGizmoArrowObject* InArrowObject,
	IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
	IGizmoTransformSource* InTransformSource,
	IGizmoStateTarget* InStateTarget,
	EAxisList::Type InAxisType,
	const FLinearColor InAxisColor)
{
	check(InArrowObject);
	check(InArrowObject->CylinderObject);
	check(InArrowObject->BoxObject);

	InArrowObject->CylinderObject->Length = AXIS_LENGTH_SCALE;
	InArrowObject->CylinderObject->Radius = AXIS_RADIUS;
	InArrowObject->CylinderObject->Offset = AXIS_LENGTH_SCALE_OFFSET;
	InArrowObject->bHasConeHead = false;

	InArrowObject->BoxObject->Dimensions = FVector(AXIS_CUBE_SIZE, AXIS_CUBE_SIZE, AXIS_CUBE_SIZE);
	InArrowObject->BoxObject->Offset = AXIS_LENGTH_SCALE + AXIS_LENGTH_SCALE_OFFSET + AXIS_CUBE_HEAD_OFFSET;
	if (InAxisType == EAxisList::X)
	{
		InArrowObject->BoxObject->UpDirection = FVector(1, 0, 0);
		InArrowObject->BoxObject->SideDirection = FVector(0, 1, 0);
	}
	else if (InAxisType == EAxisList::Y)
	{
		InArrowObject->BoxObject->UpDirection = FVector(0, 1, 0);
		InArrowObject->BoxObject->SideDirection = FVector(0, 0, 1);
	}
	else
	{
		InArrowObject->BoxObject->UpDirection = FVector(0, 0, 1);
		InArrowObject->BoxObject->SideDirection = FVector(1, 0, 0);
	}

	// create axis-position gizmo, axis-position parameter will drive scale
	UAxisPositionGizmo* ScaleGizmo = Cast<UAxisPositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisPositionBuilderIdentifier, FString(), this));
	ScaleGizmo->bEnableSignedAxis = true;
	check(ScaleGizmo);

	// axis source provides the translation axis
	ScaleGizmo->AxisSource = Cast<UObject>(InGizmoAxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	UGizmoAxisScaleParameterSource* ParamSource = UGizmoAxisScaleParameterSource::Construct(InParameterAxisSource, InTransformSource, this);
	ParamSource->bClampToZero = bDisallowNegativeScaling;
	ScaleGizmo->ParameterSource = ParamSource;

	UGizmoObjectHitTarget* HitTarget = UGizmoObjectHitTarget::Construct(InArrowObject, this);

	ScaleGizmo->HitTarget = HitTarget;
	ScaleGizmo->StateTarget = Cast<UObject>(StateTarget);

	ActiveGizmos.Add(ScaleGizmo);
	return ScaleGizmo;
}


UInteractiveGizmo* UTransformGizmo::AddPlaneScaleGizmo(
	IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
	IGizmoTransformSource* InTransformSource,
	IGizmoStateTarget* InStateTarget)
{
	/* @todo
	// create axis-position gizmo, axis-position parameter will drive scale
	UEditorPlanePositionGizmo* ScaleGizmo = Cast<UEditorPlanePositionGizmo>(GetGizmoManager()->CreateGizmo(
		UEditorInteractiveGizmoManager::DefaultEditorPlanePositionBuilderIdentifier, FString(), this));
	ScaleGizmo->bEnableSignedAxis = true;
	check(ScaleGizmo);

	// axis source provides the translation axis
	ScaleGizmo->AxisSource = Cast<UObject>(InGizmoAxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	UGizmoPlaneScaleParameterSource* ParamSource = UGizmoPlaneScaleParameterSource::Construct(InParameterAxisSource, InTransformSource, this);
	ParamSource->bClampToZero = bDisallowNegativeScaling;
	ParamSource->bUseEqualScaling = true;
	ScaleGizmo->ParameterSource = ParamSource;

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(InAxisComponent, this);
	if (this->UpdateHoverFunctionComponent)
	{
		HitTarget->UpdateHoverFunction = [InAxisComponent, this](bool bHovering) { this->UpdateHoverFunctionComponent(InAxisComponent, bHovering); };
	}
	ScaleGizmo->HitTarget = HitTarget;

	ScaleGizmo->StateTarget = Cast<UObject>(StateTarget);

	ActiveGizmos.Add(ScaleGizmo);
	return ScaleGizmo;
	*/
	return nullptr;
}


UInteractiveGizmo* UTransformGizmo::AddUniformScaleGizmo(
	IGizmoAxisSource* InGizmoAxisSource, IGizmoAxisSource* InParameterAxisSource,
	IGizmoTransformSource* InTransformSource,
	IGizmoStateTarget* InStateTarget)
{
	/* @todo
	// create plane-position gizmo, plane-position parameter will drive scale
	UEditorPlanePositionGizmo* ScaleGizmo = Cast<UEditorPlanePositionGizmo>(GetGizmoManager()->CreateGizmo(
		UEditorInteractiveGizmoManager::DefaultEditorPlanePositionBuilderIdentifier, FString(), this));
	check(ScaleGizmo);

	// axis source provides the translation plane
	ScaleGizmo->AxisSource = Cast<UObject>(InGizmoAxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	UGizmoUniformScaleParameterSource* ParamSource = UGizmoUniformScaleParameterSource::Construct(InParameterAxisSource, InTransformSource, this);
	//ParamSource->PositionConstraintFunction = [this](const FVector& Pos, FVector& Snapped) { return PositionSnapFunction(Pos, Snapped); };
	ScaleGizmo->ParameterSource = ParamSource;

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(InScaleComponent, this);
	if (this->UpdateHoverFunctionComponent)
	{
		HitTarget->UpdateHoverFunction = [InScaleComponent, this](bool bHovering) { this->UpdateHoverFunctionComponent(InScaleComponent, bHovering); };
	}
	ScaleGizmo->HitTarget = HitTarget;

	ScaleGizmo->StateTarget = Cast<UObject>(InStateTarget);

	ActiveGizmos.Add(ScaleGizmo);
	return ScaleGizmo;
	*/
	return nullptr;
}


void UTransformGizmo::ClearActiveTarget()
{
	for (UInteractiveGizmo* Gizmo : ActiveGizmos)
	{
		GetGizmoManager()->DestroyGizmo(Gizmo);
	}
	ActiveGizmos.SetNum(0);
	ActiveObjects.SetNum(0);

	CameraAxisSource = nullptr;
	GizmoGroupObject = nullptr;
	AxisXSource = nullptr;
	AxisYSource = nullptr;
	AxisZSource = nullptr;
	AxisXObject = nullptr;
	AxisYObject = nullptr;
	AxisZObject = nullptr;
	UnitAxisXSource = nullptr;
	UnitAxisYSource = nullptr;
	UnitAxisZSource = nullptr;
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

#undef LOCTEXT_NAMESPACE
