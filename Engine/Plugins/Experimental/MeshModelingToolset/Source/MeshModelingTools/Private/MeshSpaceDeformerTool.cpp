// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSpaceDeformerTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveGizmoManager.h"
#include "ToolBuilderUtil.h"

#include "SegmentTypes.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "MeshOpPreviewHelpers.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"
#include "Intersection/IntersectionUtil.h"
#include "PreviewMesh.h"

#include "BaseBehaviors/SingleClickBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"
#include "BaseGizmos/IntervalGizmo.h"
#include "PositionPlaneGizmo.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "CoreMinimal.h"
#include "Math/Matrix.h"



#define LOCTEXT_NAMESPACE "MeshSpaceDeformerTool"


/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UMeshSpaceDeformerToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshSpaceDeformerTool* MeshSpaceDeformerTool = NewObject<UMeshSpaceDeformerTool>(SceneState.ToolManager);
	
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	MeshSpaceDeformerTool->SetSelection(MakeComponentTarget(MeshComponent));
	MeshSpaceDeformerTool->SetWorld(SceneState.World);

	return MeshSpaceDeformerTool;
}


bool UMeshSpaceDeformerToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}



TUniquePtr<FDynamicMeshOperator> USpaceDeformerOperatorFactory::MakeNewOperator()
{

	check(SpaceDeformerTool);

	const ENonlinearOperationType OperationType = SpaceDeformerTool->SelectedOperationType;
	
	// Create the actual operator type based on the requested operation
	TUniquePtr<FMeshSpaceDeformerOp>  DeformerOp;
	
	switch (OperationType)
	{
		case ENonlinearOperationType::Bend: 
		{
			DeformerOp = MakeUnique<FBendMeshOp>();
			break;
		}
		case ENonlinearOperationType::Flare:
		{
			DeformerOp = MakeUnique<FFlareMeshOp>();
			break;
		}
		case ENonlinearOperationType::Twist:
		{
			DeformerOp = MakeUnique<FTwistMeshOp>();
			break;
		}
	default:
		check(0);
	}

	// Operator runs on another thread - copy data over that it needs.
	SpaceDeformerTool->UpdateOpParameters(*DeformerOp);
	

	// give the operator
	return DeformerOp;
}


/*
 * Tool
 */
UMeshSpaceDeformerTool::UMeshSpaceDeformerTool()
{
	GizmoCenter = FVector::ZeroVector;
	GizmoOrientation = FQuat::Identity;

}

void UMeshSpaceDeformerTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}


void UMeshSpaceDeformerTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


bool UMeshSpaceDeformerTool::CanAccept() const 
{
	return Super::CanAccept() && (Preview == nullptr || Preview->HaveValidResult());
}

void UMeshSpaceDeformerTool::ComputeAABB(const FDynamicMesh3& MeshIn, const FTransform& XFormIn, FVector& BBoxMin, FVector& BBoxMax) const 
{
	

	BBoxMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
	BBoxMax = -BBoxMin;
	
	for (const auto VertID : MeshIn.VertexIndicesItr())
	{
		const FVector3d LocalPosition = MeshIn.GetVertex(VertID);
		const FVector WorldPosition = XFormIn.TransformPosition(FVector(LocalPosition.X, LocalPosition.Y, LocalPosition.Z));
		for (int i = 0; i < 3; ++i) BBoxMin[i] = FMath::Min(BBoxMin[i], WorldPosition[i]);
		for (int i = 0; i < 3; ++i) BBoxMax[i] = FMath::Max(BBoxMax[i], WorldPosition[i]);
	}

}

void UMeshSpaceDeformerTool::Setup()
{
	UMeshSurfacePointTool::Setup();


	// populate the OriginalDynamicMesh with a conversion of the input mesh.
	{
		OriginalDynamicMesh = MakeShared< FDynamicMesh3 >();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
	}

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);



	//// add properties

	AddToolPropertySource(this);


	// compute the bounding box for the input mesh
	// AABB for the source geometry
	FVector AABBMin;
	FVector AABBMax;
	ComputeAABB(*OriginalDynamicMesh, ComponentTarget->GetWorldTransform(), AABBMin, AABBMax);
	FVector Extents = AABBMax - AABBMin;


	AABBHalfExtents = 0.5 * FVector3d(Extents.X, Extents.Y, Extents.Z);
	GizmoCenter = 0.5 * (AABBMin + AABBMax);


	// add click to set plane behavior
	SetPointInWorldConnector = MakePimpl<FSelectClickedAction>();
	SetPointInWorldConnector->World = this->TargetWorld;
	SetPointInWorldConnector->InvisibleComponentsToHitTest.Add(ComponentTarget->GetOwnerComponent());
	SetPointInWorldConnector->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		SetGizmoPlaneFromWorldPos(Hit.ImpactPoint, -Hit.ImpactNormal, false);
	};

	USingleClickInputBehavior* ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector.Get());
	AddInputBehavior(ClickToSetPlaneBehavior);


	// Create a new TransformGizmo and associated TransformProxy. The TransformProxy will not be the
	// parent of any Components in this case, we just use it's transform and change delegate.
	TransformProxy = NewObject<UTransformProxy>(this);
	TransformProxy->SetTransform(FTransform(GizmoOrientation, GizmoCenter));
	TransformGizmo = GetToolManager()->GetPairedGizmoManager()->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::StandardTranslateRotate, this);

	TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());
	// listen for changes to the proxy and update the preview when that happens
	TransformProxy->OnTransformChanged.AddUObject(this, &UMeshSpaceDeformerTool::TransformProxyChanged);

	// wire these to the input 

	// The initial range for the intervals.
	const float InitialVerticalIntervalSize = AABBHalfExtents.Length();

	// create sources for the interval parameters

	UpIntervalSource      = NewObject< UGizmoLocalFloatParameterSource >(this);
	DownIntervalSource    = NewObject< UGizmoLocalFloatParameterSource >(this);
	ForwardIntervalSource = NewObject< UGizmoLocalFloatParameterSource >(this);

	// Initial Lengths for the interval handles

	UpIntervalSource->Value      = InitialVerticalIntervalSize;
	DownIntervalSource->Value    = -InitialVerticalIntervalSize;
	ForwardIntervalSource->Value = 20.f;

	// Sync the properties panel to the interval handles.

	this->UpperBoundsInterval = UpIntervalSource->Value;
	this->LowerBoundsInterval = DownIntervalSource->Value;
	this->ModifierPercent     = ForwardIntervalSource->Value;

	// Wire up callbacks to update result mesh and the properties panel when these parameters are changed (by gizmo manipulation in viewport).  Note this is just a one-way
	// coupling (Sources to Properties). The OnPropertyModified() method provides the Properties to Souces coupling 

	UpIntervalSource->OnParameterChanged.AddLambda([this](IGizmoFloatParameterSource* ParamSource, FGizmoFloatParameterChange Change)->void
	{
		this->UpperBoundsInterval = Change.CurrentValue;

		if (this->Preview != nullptr)
		{
			this->Preview->InvalidateResult();
		}
	});

	DownIntervalSource->OnParameterChanged.AddLambda([this](IGizmoFloatParameterSource* ParamSource, FGizmoFloatParameterChange Change)->void
	{
		this->LowerBoundsInterval = Change.CurrentValue;

		if (this->Preview != nullptr)
		{
			this->Preview->InvalidateResult();
		}
	});

	ForwardIntervalSource->OnParameterChanged.AddLambda([this](IGizmoFloatParameterSource* ParamSource, FGizmoFloatParameterChange Change)->void
	{
		this->ModifierPercent = Change.CurrentValue;

		if (this->Preview != nullptr)
		{
			this->Preview->InvalidateResult();
		}
	});



	// add the interval gizmo
	IntervalGizmo = GetToolManager()->GetPairedGizmoManager()->CreateGizmo<UIntervalGizmo>(UIntervalGizmo::GizmoName, TEXT("MeshSpaceDefomerInterval"), this);

	// wire in the transform and the interval sources.
	IntervalGizmo->SetActiveTarget(TransformProxy, UpIntervalSource, DownIntervalSource, ForwardIntervalSource, GetToolManager());

	// use the statetarget to track details changes
	StateTarget = IntervalGizmo->StateTarget;
	// Set up the preview object
	{
		// create the operator factory
		USpaceDeformerOperatorFactory* DeformerOperatorFactory = NewObject<USpaceDeformerOperatorFactory>(this);
		DeformerOperatorFactory->SpaceDeformerTool = this; // set the back pointer


		Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(DeformerOperatorFactory, "Preview");
		Preview->Setup(this->TargetWorld, DeformerOperatorFactory);
		Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

		// Give the preview something to display
		Preview->PreviewMesh->UpdatePreview(OriginalDynamicMesh.Get());
		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

		FComponentMaterialSet MaterialSet;
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		// show the preview mesh
		Preview->SetVisibility(true);

		// start the compute
		Preview->InvalidateResult();
	}


	GetToolManager()->DisplayMessage(
		LOCTEXT("MeshSpaceDeformerToolDescription", "Deform the vertices of the selected Mesh using various spatial deformations. Use the in-viewport Gizmo to control the extents/strength of the deformation."),
		EToolMessageLevel::UserNotification);
}

void UMeshSpaceDeformerTool::Shutdown(EToolShutdownType ShutdownType)
{
	
	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

	if (Preview != nullptr)
	{
		FDynamicMeshOpResult Result = Preview->Shutdown();
		if (ShutdownType == EToolShutdownType::Accept)
		{

			GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSpaceDeformer", "Space Deformer"));

			FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
			check(DynamicMeshResult != nullptr);

			ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
			});


			GetToolManager()->EndUndoTransaction();
		}
	}

	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);

}



void  UMeshSpaceDeformerTool::TransformProxyChanged(UTransformProxy* Proxy, FTransform Transform)
{
	GizmoOrientation = Transform.GetRotation();
	GizmoCenter = Transform.GetLocation();
	GizmoFrame = FFrame3d( FVector3d(GizmoCenter), FQuaterniond(GizmoOrientation) );

	if (Preview != nullptr)
	{
		Preview->InvalidateResult();
	}
}



#if WITH_EDITOR
void UMeshSpaceDeformerTool::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive)
	{
		if (!bHasBegin)
		{
			StateTarget->BeginUpdate();
			bHasBegin = true;
		}

		// Propagates change to the ParameterSource by triggering UMeshSpaceDeformerTool::OnPropertyModified
		OnPropertyModified(this, PropertyChangedEvent.Property);
	}

	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ValueSet)
	{
		if (!bHasBegin)
		{
			StateTarget->BeginUpdate();
		}

		// Propagates change to the ParameterSource by triggering UMeshSpaceDeformerTool::OnPropertyModified
		OnPropertyModified(this, PropertyChangedEvent.Property);

		StateTarget->EndUpdate();
		// reset the bHasBegin
		bHasBegin = false;
	}
		
}


#endif

void  UMeshSpaceDeformerTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// Update the property sources to reflect any changes.
	// NB: due to callback, the SetParameter will trigger a recompute of the result via  Preview->InvalidateResult();

	if (PropertySet == this)
	{
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMeshSpaceDeformerTool, UpperBoundsInterval)))
		{
			UpIntervalSource->BeginModify();
			UpIntervalSource->SetParameter(this->UpperBoundsInterval);
			UpIntervalSource->EndModify();
		}
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMeshSpaceDeformerTool, LowerBoundsInterval)))
		{
			DownIntervalSource->BeginModify();
			DownIntervalSource->SetParameter(this->LowerBoundsInterval);
			DownIntervalSource->EndModify();
		}
		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMeshSpaceDeformerTool, ModifierPercent)))
		{
			ForwardIntervalSource->BeginModify();
			ForwardIntervalSource->SetParameter(this->ModifierPercent);
			ForwardIntervalSource->EndModify();
		}


		if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMeshSpaceDeformerTool, SelectedOperationType)))
		{
			if (Preview != nullptr)
			{
				Preview->InvalidateResult();
			}
		}
	}
}

void UMeshSpaceDeformerTool::UpdateOpParameters(FMeshSpaceDeformerOp& MeshSpaceDeformerOp) const
{
	
	

	
	FMatrix WorldToGizmo(EForceInit::ForceInitToZero);
	{

		// Matrix that rotates to the gizmo frame 
		const FMatrix3d ToGizmo(GizmoFrame.GetAxis(0),
		                    	GizmoFrame.GetAxis(1),
			                    GizmoFrame.GetAxis(2),
			                    true /* row constructor*/);

		// Gizmo To World is rotation followed by translation of the form
		// world_vec = FromGizmo * local_vec + GizmoCenter
		// so the inverse will be
		// local_vec = FromGizmo^(-1) * world_vec - FromGizmo^(-1) * GizmoCenter
		// 

		// Copy the rotation
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				WorldToGizmo.M[i][j] = ToGizmo(i, j);
			}
		}
		// Add the translation 
		FVector3d Translation = ToGizmo * GizmoCenter;
		WorldToGizmo.M[0][3] = -Translation[0];
		WorldToGizmo.M[1][3] = -Translation[1];
		WorldToGizmo.M[2][3] = -Translation[2];
		WorldToGizmo.M[3][3] = 1.f;
	}
	// Get the transform as a matrix.  Note may need to transpose this due
	// to the vec * Matrix  vs Matrix * vec usage in engine.

	FMatrix ObjectToWorld = ComponentTarget->GetWorldTransform().ToMatrixWithScale().GetTransposed();

	FMatrix ObjectToGizmo(EForceInit::ForceInitToZero);
	{
		// ObjectToGizmo = WorldToGizmo * ObjectToWorld
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				for (int k = 0; k < 4; ++k)
				{
					ObjectToGizmo.M[i][j] += WorldToGizmo.M[i][k] * ObjectToWorld.M[k][j];
				}
			}
		}
	}

	//This allows the object to move, or the handle position to move and have the changes reflected based on their relative world-space positions
	const FVector3d AxisCentroidObjectSpace = ComponentTarget->GetWorldTransform().InverseTransformPositionNoScale(GizmoCenter);

	//Pass the relevant updated data to the operator then run the operation.
	/**
	const double LowerBoundsInterval = this->LowerBoundsInterval;
	const double UpperBoundsInterval = this->UpperBoundsInterval;
	const double ModifierPercent     = this->ModifierPercent;
	*/

	// Set half axis length to be 1/2 the major axis of the bbox.
	double LengthScale = FMath::Max( AABBHalfExtents.MaxAbsElement(), 1.e-3);
	MeshSpaceDeformerOp.AxesHalfLength = LengthScale;

	// set the bound range
	MeshSpaceDeformerOp.UpperBoundsInterval =  UpperBoundsInterval / LengthScale;
	MeshSpaceDeformerOp.LowerBoundsInterval = -LowerBoundsInterval / LengthScale;

	// percent to apply.
	MeshSpaceDeformerOp.ModifierPercent = ModifierPercent;


	// the transform to gizmo space
	MeshSpaceDeformerOp.ObjectToGizmo = ObjectToGizmo;

	//MeshSpaceDeformerOp.AxisOriginObjectSpace = AxisCentroidObjectSpace; // gizmo origin in object space.

	// copy the mesh
	MeshSpaceDeformerOp.CopySource(*OriginalDynamicMesh, ComponentTarget->GetWorldTransform());
}

void UMeshSpaceDeformerTool::OnTick(float DeltaTime)
{
	if (TransformGizmo != nullptr)
	{
		TransformGizmo->bSnapToWorldGrid = this->bSnapToWorldGrid;
	}

	if (Preview != nullptr)
	{
		Preview->Tick(DeltaTime);
	}
}



void UMeshSpaceDeformerTool::SetGizmoPlaneFromWorldPos(const FVector& Position, const FVector& Normal, bool bIsInitializing)
{
	GizmoCenter = Position;

	FFrame3f GizmoPlane(Position, Normal);
	GizmoOrientation = (FQuat)GizmoPlane.Rotation;
	GizmoFrame = FFrame3d(FVector3d(GizmoCenter), FQuaterniond(GizmoOrientation));

	TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());
	if (bIsInitializing)
	{
		TransformGizmo->ReinitializeGizmoTransform(GizmoPlane.ToFTransform());
	}
	else
	{
		TransformGizmo->SetNewGizmoTransform(GizmoPlane.ToFTransform());
	}

	if (Preview != nullptr)
	{
		Preview->InvalidateResult();
	}
}


#undef LOCTEXT_NAMESPACE

