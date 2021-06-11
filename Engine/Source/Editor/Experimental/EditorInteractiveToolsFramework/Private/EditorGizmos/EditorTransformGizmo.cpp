// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformGizmo.h"
#include "BaseGizmos/AxisPositionGizmo.h"
#include "BaseGizmos/GizmoArrowObject.h"
#include "BaseGizmos/GizmoBoxObject.h"
#include "BaseGizmos/GizmoConeObject.h"
#include "BaseGizmos/GizmoCylinderObject.h"
#include "BaseGizmos/GizmoGroupObject.h"
#include "BaseGizmos/TransformSources.h"
#include "EditorGizmos/EditorAxisSources.h"
#include "EditorGizmos/EditorParameterToTransformAdapters.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EditorModeTools.h"
#include "UnrealEdGlobals.h"
#include "UnrealEngine.h"

#define LOCTEXT_NAMESPACE "UEditorTransformGizmo"

constexpr float UEditorTransformGizmo::AXIS_LENGTH;
constexpr float UEditorTransformGizmo::AXIS_RADIUS;
constexpr float UEditorTransformGizmo::AXIS_CONE_ANGLE;
constexpr float UEditorTransformGizmo::AXIS_CONE_HEIGHT;
constexpr float UEditorTransformGizmo::AXIS_CONE_HEAD_OFFSET;
constexpr float UEditorTransformGizmo::AXIS_CUBE_SIZE;
constexpr float UEditorTransformGizmo::AXIS_CUBE_HEAD_OFFSET;
constexpr float UEditorTransformGizmo::TRANSLATE_ROTATE_AXIS_CIRCLE_RADIUS;
constexpr float UEditorTransformGizmo::TWOD_AXIS_CIRCLE_RADIUS;
constexpr float UEditorTransformGizmo::INNER_AXIS_CIRCLE_RADIUS;
constexpr float UEditorTransformGizmo::OUTER_AXIS_CIRCLE_RADIUS;
constexpr float UEditorTransformGizmo::ROTATION_TEXT_RADIUS;
constexpr int32 UEditorTransformGizmo::AXIS_CIRCLE_SIDES;
constexpr float UEditorTransformGizmo::ARCALL_RELATIVE_INNER_SIZE;
constexpr float UEditorTransformGizmo::AXIS_LENGTH_SCALE;
constexpr float UEditorTransformGizmo::AXIS_LENGTH_SCALE_OFFSET;

constexpr FLinearColor UEditorTransformGizmo::AxisColorX;
constexpr FLinearColor UEditorTransformGizmo::AxisColorY;
constexpr FLinearColor UEditorTransformGizmo::AxisColorZ;
constexpr FLinearColor UEditorTransformGizmo::ScreenAxisColor;
constexpr FColor UEditorTransformGizmo::PlaneColorXY;
constexpr FColor UEditorTransformGizmo::ArcBallColor;
constexpr FColor UEditorTransformGizmo::ScreenSpaceColor;
constexpr FColor UEditorTransformGizmo::CurrentColor;

UInteractiveGizmo* UEditorTransformGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	// @todo - remove global call
	FEditorModeTools& ModeTools = GLevelEditorModeTools();

	ETransformGizmoSubElements Elements  = ETransformGizmoSubElements::None;
	bool bUseContextCoordinateSystem = true;
	UE::Widget::EWidgetMode WidgetMode = ModeTools.GetWidgetMode();
	switch ( WidgetMode )
	{
	case UE::Widget::EWidgetMode::WM_Translate:
		Elements = ETransformGizmoSubElements::TranslateAllAxes | ETransformGizmoSubElements::TranslateAllPlanes;
		break;
	case UE::Widget::EWidgetMode::WM_Rotate:
		Elements = ETransformGizmoSubElements::RotateAllAxes;
		break;
	case UE::Widget::EWidgetMode::WM_Scale:
		Elements = ETransformGizmoSubElements::ScaleAllAxes | ETransformGizmoSubElements::ScaleAllPlanes;
		bUseContextCoordinateSystem = false;
		break;
	case UE::Widget::EWidgetMode::WM_2D:
		Elements = ETransformGizmoSubElements::RotateAxisY | ETransformGizmoSubElements::TranslatePlaneXZ;
		break;
	default:
		Elements = ETransformGizmoSubElements::FullTranslateRotateScale;
		break;
	}

	UEditorTransformGizmo* TransformGizmo = NewObject<UEditorTransformGizmo>(SceneState.GizmoManager);
	TransformGizmo->Setup();

	TransformGizmo->SetWorld(SceneState.World);
	TransformGizmo->bUseContextCoordinateSystem = bUseContextCoordinateSystem;

	// @todo - update to work with typed elements
	TArray<AActor*> SelectedActors;
	ModeTools.GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);

	UTransformProxy* TransformProxy = NewObject<UTransformProxy>();
	for (auto Actor : SelectedActors)
	{
		USceneComponent* SceneComponent = Actor->GetRootComponent();
		TransformProxy->AddComponent(SceneComponent);
	}
	TransformGizmo->SetActiveTarget( TransformProxy );
	TransformGizmo->SetVisibility(SelectedActors.Num() > 0);

	return TransformGizmo;
}

bool UEditorTransformGizmoBuilder::SatisfiesCondition(const FToolBuilderState& SceneState) const 
{
	if (UTypedElementSelectionSet* SelectionSet = SceneState.TypedElementSelectionSet.Get())
	{
		return (SelectionSet->HasSelectedElements());
	}
	return true;
}

void UEditorTransformGizmo::SetWorld(UWorld* WorldIn)
{
	this->World = WorldIn;
}

void UEditorTransformGizmo::SetElements(ETransformGizmoSubElements InEnableElements)
{
	EnableElements = InEnableElements;
}

void UEditorTransformGizmo::SetDisallowNegativeScaling(bool bDisallow)
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

void UEditorTransformGizmo::SetIsNonUniformScaleAllowedFunction(TUniqueFunction<bool()>&& IsNonUniformScaleAllowedIn)
{
	IsNonUniformScaleAllowed = MoveTemp(IsNonUniformScaleAllowedIn);
}


void UEditorTransformGizmo::Setup()
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


void UEditorTransformGizmo::Shutdown()
{
	ClearActiveTarget();
}


void UEditorTransformGizmo::UpdateCameraAxisSource()
{
	FViewCameraState CameraState;
	GetGizmoManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	if (CameraAxisSource != nullptr) // && GizmoActor != nullptr)
	{
		// @todo get this from the UTransformProxy instead of global?
		FEditorModeTools& EditorModeTools = GLevelEditorModeTools();
		CameraAxisSource->Origin = EditorModeTools.GetWidgetLocation();// GizmoActor->GetTransform().GetLocation();
		CameraAxisSource->Direction = -CameraState.Forward();
		CameraAxisSource->TangentX = CameraState.Right();
		CameraAxisSource->TangentY = CameraState.Up();
	}
}


void UEditorTransformGizmo::Tick(float DeltaTime)
{	
	if (bUseContextCoordinateSystem)
	{
		CurrentCoordinateSystem = GetGizmoManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem();
	}
	
	check(CurrentCoordinateSystem == EToolContextCoordinateSystem::World || CurrentCoordinateSystem == EToolContextCoordinateSystem::Local)
	bool bUseLocalAxes = (CurrentCoordinateSystem == EToolContextCoordinateSystem::Local);

	if (AxisXSource != nullptr && AxisYSource != nullptr && AxisZSource != nullptr)
	{
		AxisXSource->bLocalAxes = bUseLocalAxes;
		AxisYSource->bLocalAxes = bUseLocalAxes;
		AxisZSource->bLocalAxes = bUseLocalAxes;
	}

	for (UGizmoBaseObject* Object : ActiveObjects)
	{
		Object->SetWorldLocalState(CurrentCoordinateSystem == EToolContextCoordinateSystem::World);

		float Scale = GLevelEditorModeTools().GetWidgetScale();
		Object->SetGizmoScale(Scale);
	}

	bool bShouldShowNonUniformScale = IsNonUniformScaleAllowed();

	for (UGizmoBaseObject* Object : NonuniformScaleObjects)
	{
		Object->SetVisibility(bShouldShowNonUniformScale);
	}

	UpdateCameraAxisSource();
}


void UEditorTransformGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}

	ActiveTarget = Target;

	// move gizmo to target location

	FTransform TargetTransform = Target->GetTransform();
	FTransform GizmoTransform = TargetTransform;

	// @todo this needs to be queried and updated in the TransformProxy
	float GizmoScale = GLevelEditorModeTools().GetWidgetScale();
	FMatrix GizmoLocalToWorld = GizmoTransform.ToMatrixNoScale();
	FTransform GizmoLocalToWorldTransform = GizmoTransform;
	GizmoLocalToWorldTransform.SetScale3D(FVector(GizmoScale));

	// create group object to which all active objects will be added
	GizmoGroupObject = NewObject<UGizmoGroupObject>();

	// root component provides local X/Y/Z axis, identified by AxisIndex
	AxisXSource = UGizmoEditorAxisSource::Construct(0, true, this);
	AxisYSource = UGizmoEditorAxisSource::Construct(1, true, this);
	AxisZSource = UGizmoEditorAxisSource::Construct(2, true, this);

	auto MakeArrowObjectFunc = [](FVector Axis, UMaterialInterface* Material, UMaterialInterface* CurrentMaterial, FMatrix LocalToWorld, float GizmoScale, FTransform LocalToWorldTransform)
	{
		UGizmoArrowObject* ArrowObject = NewObject<UGizmoArrowObject>();
		ArrowObject->CylinderObject->Direction = Axis;
		ArrowObject->ConeObject->Direction = -Axis;
		ArrowObject->SetMaterial(Material);
		ArrowObject->SetCurrentMaterial(CurrentMaterial);
		ArrowObject->SetGizmoScale(GizmoScale);
		ArrowObject->SetLocalToWorldTransform(LocalToWorldTransform);
		return ArrowObject;
	};

	UGizmoArrowObject* ArrowXObject = MakeArrowObjectFunc(FVector(1.0f, 0.0f, 0.0f), AxisMaterialX, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);
	UGizmoArrowObject* ArrowYObject = MakeArrowObjectFunc(FVector(0.0f, 1.0f, 0.0f), AxisMaterialY, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);
	UGizmoArrowObject* ArrowZObject = MakeArrowObjectFunc(FVector(0.0f, 0.0f, 1.0f), AxisMaterialZ, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);

	UGizmoScaledAndUnscaledTransformSources* TransformSource = UGizmoScaledAndUnscaledTransformSources::Construct(
		UGizmoTransformProxyTransformSource::Construct(ActiveTarget, this),
		UGizmoObjectWorldTransformSource::Construct(GizmoGroupObject, this));
	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}
	StateTarget = UGizmoObjectTransformChangeStateTarget::Construct(GizmoGroupObject,
		LOCTEXT("UEditorTransformGizmoTransaction", "Transform"), TransactionProvider, this);
	StateTarget->DependentChangeSources.Add(MakeUnique<FTransformProxyChangeSource>(Target));

	CameraAxisSource = NewObject<UGizmoConstantFrameAxisSource>(this);

	FVector TargetWorldOrigin = TargetTransform.GetLocation();

	if ((EnableElements & ETransformGizmoSubElements::TranslateAxisX) != ETransformGizmoSubElements::None)
	{
		AddAxisTranslationGizmo(ArrowXObject, AxisXSource, TransformSource, StateTarget, EAxisList::X, AxisColorX);
		ActiveObjects.Add(ArrowXObject);
		GizmoGroupObject->Add(ArrowXObject);
	}
	if ((EnableElements & ETransformGizmoSubElements::TranslateAxisY) != ETransformGizmoSubElements::None)
	{
		AddAxisTranslationGizmo(ArrowYObject, AxisYSource, TransformSource, StateTarget, EAxisList::Y, AxisColorY);
		ActiveObjects.Add(ArrowYObject);
		GizmoGroupObject->Add(ArrowYObject);
	}
	if ((EnableElements & ETransformGizmoSubElements::TranslateAxisZ) != ETransformGizmoSubElements::None)
	{
		AddAxisTranslationGizmo(ArrowZObject,  AxisZSource, TransformSource, StateTarget, EAxisList::Z, AxisColorZ);
		ActiveObjects.Add(ArrowZObject);
		GizmoGroupObject->Add(ArrowZObject);
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
	UGizmoArrowObject* ScaleArrowXObject = MakeArrowObjectFunc(FVector(1.0f, 0.0f, 0.0f), AxisMaterialX, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);
	UGizmoArrowObject* ScaleArrowYObject = MakeArrowObjectFunc(FVector(0.0f, 1.0f, 0.0f), AxisMaterialY, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);
	UGizmoArrowObject* ScaleArrowZObject = MakeArrowObjectFunc(FVector(0.0f, 0.0f, 1.0f), AxisMaterialZ, CurrentAxisMaterial, GizmoLocalToWorld, GizmoScale, GizmoLocalToWorldTransform);

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

	if ((EnableElements & ETransformGizmoSubElements::ScaleAxisX) != ETransformGizmoSubElements::None)
	{
		AddAxisScaleGizmo(ScaleArrowXObject, AxisXSource, UnitAxisXSource, TransformSource, StateTarget, EAxisList::X, AxisColorX);
		ActiveObjects.Add(ScaleArrowXObject);
		NonuniformScaleObjects.Add(ScaleArrowXObject);
		GizmoGroupObject->Add(ScaleArrowXObject);
	}
	if ((EnableElements & ETransformGizmoSubElements::ScaleAxisY) != ETransformGizmoSubElements::None)
	{
		AddAxisScaleGizmo(ScaleArrowYObject, AxisYSource, UnitAxisYSource, TransformSource, StateTarget, EAxisList::Y, AxisColorY);
		ActiveObjects.Add(ScaleArrowYObject);
		NonuniformScaleObjects.Add(ScaleArrowYObject);
		GizmoGroupObject->Add(ScaleArrowYObject);
	}
	if ((EnableElements & ETransformGizmoSubElements::ScaleAxisZ) != ETransformGizmoSubElements::None)
	{
		AddAxisScaleGizmo(ScaleArrowZObject, AxisZSource, UnitAxisZSource, TransformSource, StateTarget, EAxisList::Z, AxisColorZ);
		ActiveObjects.Add(ScaleArrowZObject);
		NonuniformScaleObjects.Add(ScaleArrowZObject);
		GizmoGroupObject->Add(ScaleArrowZObject);
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
}


void UEditorTransformGizmo::ReinitializeGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	// The underlying proxy has an existing way to reinitialize its transform without callbacks.
	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}


void UEditorTransformGizmo::SetNewGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	check(ActiveTarget != nullptr);

	StateTarget->BeginUpdate();

	ActiveTarget->SetTransform(NewTransform);

	StateTarget->EndUpdate();
}


// @todo: This should either be named to "SetScale" or removed, since it can be done with ReinitializeGizmoTransform
void UEditorTransformGizmo::SetNewChildScale(const FVector& NewChildScale)
{
	FTransform NewTransform = ActiveTarget->GetTransform();
	NewTransform.SetScale3D(NewChildScale);

	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}


void UEditorTransformGizmo::SetVisibility(bool bVisible)
{
	for (UGizmoBaseObject* Object : ActiveObjects)
	{
		Object->SetVisibility(bVisible);
	}
}

UInteractiveGizmo* UEditorTransformGizmo::AddAxisTranslationGizmo(
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

	// arrow object provides the render capability
	TranslateGizmo->GizmoObject = InArrowObject;

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

UInteractiveGizmo* UEditorTransformGizmo::AddPlaneTranslationGizmo(
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

UInteractiveGizmo* UEditorTransformGizmo::AddAxisRotationGizmo(
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


UInteractiveGizmo* UEditorTransformGizmo::AddAxisScaleGizmo(
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

	// arrow object provides the render capability
	ScaleGizmo->GizmoObject = InArrowObject;

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


UInteractiveGizmo* UEditorTransformGizmo::AddPlaneScaleGizmo(
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


UInteractiveGizmo* UEditorTransformGizmo::AddUniformScaleGizmo(
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


void UEditorTransformGizmo::ClearActiveTarget()
{
	for (UInteractiveGizmo* Gizmo : ActiveGizmos)
	{
		GetGizmoManager()->DestroyGizmo(Gizmo);
	}
	ActiveGizmos.SetNum(0);
	ActiveObjects.SetNum(0);
	NonuniformScaleObjects.SetNum(0);

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


bool UEditorTransformGizmo::PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const
{
	SnappedPositionOut = WorldPosition;

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

	return false;
}


FQuat UEditorTransformGizmo::RotationSnapFunction(const FQuat& DeltaRotation) const
{
	FQuat SnappedDeltaRotation = DeltaRotation;

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
	return SnappedDeltaRotation;
}

#undef LOCTEXT_NAMESPACE
