// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshSpaceDeformerTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "SegmentTypes.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"
#include "ToolSceneQueriesUtil.h"
#include "Intersection/IntersectionUtil.h"

#include "Async/ParallelFor.h"
#include "Containers/BitArray.h"

///////////////////////////////
// EIGEN

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable : 6011)
#pragma warning(disable : 6387)
#pragma warning(disable : 6313)
#pragma warning(disable : 6294)
#endif
THIRD_PARTY_INCLUDES_START
#include <Eigen/Core>
#include <Eigen/Dense> 
#include <Eigen/Eigenvalues>

#ifndef EIGEN_MPL2_ONLY
#endif
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif

///////////////////////////////


#define LOCTEXT_NAMESPACE "MeshSpaceDeformerTool"


//////////////////////////////
// DEBUG_SETTINGS

#define DEBUG_ASYNC_TASK

#define DEBUG_DRAW_AXES
//////////////////////////////


#ifdef DEBUG_ASYNC_TASK
#define print(text,...) UE_LOG(LogTemp, Warning, TEXT(text), __VA_ARGS__)
#else
#define print(x)
#endif



double RayToSegmentSquareDist(const FRay3d& Ray, const FVector3d& V0, const FVector3d& V1, FVector3d* ClosestPtOnSegment, FVector3d* ClosestPtOnRay)
{
	if (V0 == V1)
	{
		if (ClosestPtOnRay) 
		{
			*ClosestPtOnRay = V0;
		}

		if (ClosestPtOnSegment)
		{
			*ClosestPtOnSegment = V0;
		}
		return 0.;
	}
	FVector3d SegCenter = (V0 + V1) * 0.5;
	FVector3d SegDir = (V1 - V0).Normalized();
	FVector3d Diff = Ray.Origin - SegCenter;

	double SegExtent = V0.Distance(V1) * 0.5;
	double A01 = -Ray.Direction.Dot(SegDir);
	double B0 = Diff.Dot(Ray.Direction);
	double B1 = -Diff.Dot(SegDir);
	double C = (Diff).SquaredLength();
	double det = FMath::Abs(1. - A01 * A01);
	double S0, S1, SqrDist, ExtDet;

	if (det > 0.)
	{
		// The ray and segment are not parallel.

		S0 = A01 * B1 - B0;
		S1 = A01 * B0 - B1;
		ExtDet = SegExtent * det;

		if (S0 >= 0.)
		{
			if (S1 >= -ExtDet)
			{
				if (S1 <= ExtDet)
				{
					// region 0
					// Minimum at interior points of ray and segment.
					float invDet = 1. / det;
					S0 *= invDet;
					S1 *= invDet;
					SqrDist = S0 * (S0 + A01 * S1 + 2. * B0) + S1 * (A01 * S0 + S1 + 2. * B1) + C;
				}
				else
				{
					// region 1
					S1 = SegExtent;
					S0 = std::max(0., -(A01 * S1 + B0));
					SqrDist = -S0 * S0 + S1 * (S1 + 2. * B1) + C;
				}
			}
			else
			{
				// region 5
				S1 = -SegExtent;
				S0 = std::max(0., -(A01 * S1 + B0));
				SqrDist = -S0 * S0 + S1 * (S1 + 2. * B1) + C;
			}
		}
		else
		{
			if (S1 <= -ExtDet)
			{
				// region 4
				S0 = std::max(0., -(-A01 * SegExtent + B0));
				S1 = (S0 > 0.) ? -SegExtent : std::min(std::max(-SegExtent, -B1), SegExtent);
				SqrDist = -S0 * S0 + S1 * (S1 + 2. * B1) + C;
			}
			else if (S1 <= ExtDet)
			{
				// region 3
				S0 = 0;
				S1 = std::min(std::max(-SegExtent, -B1), SegExtent);
				SqrDist = S1 * (S1 + 2. * B1) + C;
			}
			else
			{
				// region 2
				S0 = std::max(0., -(A01 * SegExtent + B0));
				S1 = (S0 > 0.) ? SegExtent : std::min(std::max(-SegExtent, -B1), SegExtent);
				SqrDist = -S0 * S0 + S1 * (S1 + 2. * B1) + C;
			}
		}
	}
	else
	{
		// Ray and segment are parallel.

		S1 = (A01 > 0.) ? -SegExtent : SegExtent;
		S0 = std::max(0., -(A01 * S1 + B0));
		SqrDist = -S0 * S0 + S1 * (S1 + 2. * B1) + C;
	}

	if (ClosestPtOnSegment)
	{
		*ClosestPtOnSegment = SegDir * S1 + SegCenter;
	}

	if (ClosestPtOnRay)
	{
		*ClosestPtOnRay = Ray.Direction * S0 + Ray.Origin;
	}
	return SqrDist;
}

FMatrix3d SolveSampleCovariance(double* Array, const int NCols, const int NRows, FVector3d& ExtentsOut, FVector3d& OriginOut)
{
	using RowMajorVec3dMatrix = Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::RowMajor>;
	using RowMajorVec3d = Eigen::Matrix<double, 1, 3, Eigen::RowMajor>;
	RowMajorVec3dMatrix VertexMatrix = Eigen::Map<RowMajorVec3dMatrix>(Array, NRows, NCols);

	check(VertexMatrix(0, 0) == *Array); // Ensure the mapping is in expected format
	check(VertexMatrix(0, 1) == Array[1]);
	check(VertexMatrix(0, 2) == Array[2]);

	//Using adjoint eigen solver
	auto Origin = VertexMatrix.colwise().mean();															 // Get the centroid of the point cloud
	Eigen::MatrixXd CenteredVertices = VertexMatrix.rowwise() - Origin;										 // Translate each point so centroid is (0,0,0)
	Eigen::MatrixXd CovarianceMatrix = (CenteredVertices.adjoint() * CenteredVertices) / double(NRows - 1);	 // note: adjoint ~= conjugate transpose; formula from https://en.wikipedia.org/wiki/Sample_mean_and_covariance#Sample_covariance
	
	// note: SelfAdjointEigenSolver automatically sorts the eigen vectors by the order of corresponding eigenvalues in increasing order
	Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> EigenSolver(CovarianceMatrix);							 
	Eigen::Matrix3d EigenVectors = EigenSolver.eigenvectors();

	//Align object with the standard basis by rotating all the points by the inverse of EigenVectors.
	//EigenVectors is an orthonormal basis (3x3) so transpose is the inverse; 
	Eigen::Matrix3Xd RotatedVertices = EigenVectors.transpose() * CenteredVertices.transpose();

	//Since our point cloud is now aligned with the standard axes, the dimensions of its AABB should be the dimensions of its 
	RowMajorVec3d Extents = RotatedVertices.rowwise().maxCoeff() - RotatedVertices.rowwise().minCoeff();

	std::sort(Extents.data(), Extents.data() +3);						//Sort the extents to match our sorted eigenvectors
	ExtentsOut = FVector3d{ FMath::Abs(Extents[0]), FMath::Abs(Extents[1]), FMath::Abs(Extents[2]) } / 2;	//Divide by two because extent represents distance from center
	OriginOut = FVector3d{ Origin(0,0), Origin(0,1), Origin(0,2) };		//Origin in world space

	return FMatrix3d{ FVector3d{ EigenVectors(0,0), EigenVectors(1,0), EigenVectors(2,0) },
					  FVector3d{ EigenVectors(0,1), EigenVectors(1,1), EigenVectors(2,1) },
					  FVector3d{ EigenVectors(0,2), EigenVectors(1,2), EigenVectors(2,2) }, 
					  false };
}

FMatrix3d UMeshSpaceDeformerTool::CalculateBestAxis(FDynamicMesh3* Mesh, const TArray<FVector3d>& PositionBuffer, FTransform WorldTransform, FVector3d * ExtentsOut, FVector3d * OriginOut)
{
	PCAVertexPositionBuffer.Reset(3 * Mesh->VertexCount());
	double* VectorData = PCAVertexPositionBuffer.GetData();
	FVector3d* InitializePtr = reinterpret_cast<FVector3d*>(VectorData);
	for (const int& VertexID : Mesh->VertexIndicesItr())
	{
		const FVector3d& Point = PositionBuffer[VertexID];
		*InitializePtr = WorldTransform.TransformPosition(Point);
		++InitializePtr;
	}

	const int NCols = 3; // R^3;
	const int NRows = Mesh->VertexCount();

	return SolveSampleCovariance(VectorData, NCols, NRows, *ExtentsOut, *OriginOut);
}

/*
 * ToolBuilder
 */
UMeshSurfacePointTool* UMeshSpaceDeformerToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshSpaceDeformerTool* NonlinearTool = NewObject<UMeshSpaceDeformerTool>(SceneState.ToolManager);
	return NonlinearTool;
}

UMeshSpaceDeformerTool::UMeshSpaceDeformerTool()
{
}

void UMeshSpaceDeformerTool::Setup()
{
	UMeshSurfacePointTool::Setup();

	//// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "Dynamic Mesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());


	//// copy material if there is one
	auto Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}
	//// dynamic mesh configuration settings
	ComponentTarget->SetOwnerVisibility(false);
	DynamicMeshComponent->TangentsType = EDynamicMeshTangentCalcType::AutoCalculated;
	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OnDynamicMeshComponentChangedHandle = DynamicMeshComponent->OnMeshChanged.Add(
		FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UMeshSpaceDeformerTool::OnDynamicMeshComponentChanged));

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();

	OriginalPositions.SetNumUninitialized(TargetMesh->MaxVertexID());
	for (int32 VertexID : TargetMesh->VertexIndicesItr())
	{
		OriginalPositions[VertexID] = TargetMesh->GetVertex(VertexID);
	}

	bInDrag = false;

	//// add properties
	ToolPropertyObjects.Add(this);

	//// set up visualizers
	AxisRenderer.LineColor = FLinearColor::Red;
	AxisRenderer.LineThickness = 2.0;
	AxisRenderer.bDepthTested = false;

	for (int8 i = 0; i < 3; ++i)
	{
		FMeshSpaceDeformerOp* Op = Operators[i];
		Op->UpdateMesh(DynamicMeshComponent->GetMesh());
	}

	AutoDetectAxes();

}

void UMeshSpaceDeformerTool::Shutdown(EToolShutdownType ShutdownType)
{

	if (DynamicMeshComponent != nullptr)
	{
		DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSpaceDeformerToolTransactionName", "Deform Mesh"));
			ComponentTarget->CommitMesh([&](FMeshDescription* MeshDescription)
			{
				DynamicMeshComponent->Bake(MeshDescription, false);
			});
			GetToolManager()->EndUndoTransaction();
		}
			DynamicMeshComponent->UnregisterComponent();
			DynamicMeshComponent->DestroyComponent();
			DynamicMeshComponent = nullptr;
	}
}


void UMeshSpaceDeformerTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
}

void UMeshSpaceDeformerTool::OnDynamicMeshComponentChanged()
{
}

bool UMeshSpaceDeformerTool::HitTest(const FRay & Ray, FHitResult & OutHit)
{
	return Handle.SelectedVertexID != -1;
}

void UMeshSpaceDeformerTool::OnBeginDrag(const FRay& WorldRay)
{
	if (Handle.SelectedVertexID != -1)
	{
		bInDrag = true;
		Handle.DragUpdateBounds(WorldRay);
		UpdateIntervalsFromDrag();
		UpdateOp();
	}
}


void UMeshSpaceDeformerTool::OnUpdateDrag(const FRay& Ray)
{
	if (bInDrag)
	{
		Handle.DragUpdateBounds(Ray);
		UpdateIntervalsFromDrag();
		UpdateOp();
	}
}

void UMeshSpaceDeformerTool::OnEndDrag(const FRay& Ray)
{
	bInDrag = false;

}

inline void UMeshSpaceDeformerTool::AutoDetectAxes()
{
	FTransform WorldTransform = ComponentTarget->GetWorldTransform();
	FTransform WorldToObject = WorldTransform.Inverse();

	//Retrieves the best axis in world space via principal component analysis.
	FMatrix3d Basis = CalculateBestAxis(DynamicMeshComponent->GetMesh(), OriginalPositions, WorldTransform, &PrincipalAxesHalfExtentCoeff, &AxisCentroidWorldSpace);

	PrincipalAxesWorldSpace[0] = FVector3d{ Basis(0,0),Basis(1,0),Basis(2,0) };
	PrincipalAxesWorldSpace[1] = FVector3d{ Basis(0,1),Basis(1,1),Basis(2,1) };
	PrincipalAxesWorldSpace[2] = FVector3d{ Basis(0,2),Basis(1,2),Basis(2,2) };

	PrincipalAxesObjectSpace[0] = WorldToObject.TransformVectorNoScale(PrincipalAxesWorldSpace[0]);
	PrincipalAxesObjectSpace[1] = WorldToObject.TransformVectorNoScale(PrincipalAxesWorldSpace[1]);
	PrincipalAxesObjectSpace[2] = WorldToObject.TransformVectorNoScale(PrincipalAxesWorldSpace[2]);

	UpdateObjectSpaceAxisCentroid();
}

void UMeshSpaceDeformerTool::SwapSecondaryAxis()
{
	std::swap(SecondaryAxis,ThirdAxis);
}

inline void UMeshSpaceDeformerTool::UpdateObjectSpaceAxisCentroid()
{
	AxisCentroidObjectSpace = ComponentTarget->GetWorldTransform().InverseTransformPositionNoScale(AxisCentroidWorldSpace);
}

void UMeshSpaceDeformerTool::UpdateIntervalsFromDrag()
{
	FHandleWidgetVertex* SelectedVertex = Handle.GetSelectedVertex();
	double CurrentSegmentLength = (SelectedVertex->WorldCenter - AxisCentroidWorldSpace).Length();
	double MaxLength = PrincipalAxesHalfExtentCoeff[SelectedAxis];
	double NewInterval = FMath::Clamp(CurrentSegmentLength / MaxLength, 0.0, 1.5);
	if (Handle.SelectedVertexID == 0)
	{
		LowerBoundsInterval = -NewInterval;
	}
	else
	{
		UpperBoundsInterval = NewInterval;
	}
}

bool UMeshSpaceDeformerTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	if (!bInDrag)
	{
		const auto ToleranceTest = [this](const FVector3d& Position1, const FVector3d& Position2) {
			return ToolSceneQueriesUtil::PointSnapQuery(this->CameraState, Position1, Position2, VisualAngleSnapThreshold);
		};
		Handle.UpdateHover(DevicePos, ComponentTarget->GetWorldTransform(), TFunction<bool(const FVector3d&, const FVector3d&)>{ToleranceTest});
	}
	return true;
}

#if WITH_EDITOR
void UMeshSpaceDeformerTool::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	//UpdateOp();
}
#endif

void UMeshSpaceDeformerTool::UpdateOp()
{
	FMeshSpaceDeformerOp* Op = Operators[static_cast<int8>(SelectedOperationType)];

	//Provide the basis so that the selected axis is in the Y direction (this will NEED to be rotated based on the orientation the operator expects)
	// i.e. the Bend operator expects the bend along the Y axis, the twist expects along the Z axis. Just swap the axes as needed inside the operator
	FMatrix3d Basis
	{
		PrincipalAxesObjectSpace[SecondaryAxis],
		PrincipalAxesObjectSpace[SelectedAxis],
		PrincipalAxesObjectSpace[ThirdAxis],
		false
	};

	//This allows the object to move, or the handle position to move and have the changes reflected based on their relative world-space positions
	UpdateObjectSpaceAxisCentroid();

	//Pass the relevant updated data to the operator then run the operation.
	Op->UpdateAxisData(Basis, AxisCentroidObjectSpace, PrincipalAxesHalfExtentCoeff, LowerBoundsInterval, UpperBoundsInterval, ModifierPercent);
	Op->CalculateResult(nullptr);

	DynamicMeshComponent->FastNotifyPositionsUpdated();
}

void UMeshSpaceDeformerTool::Tick(float DeltaTime)
{
	UMeshSurfacePointTool::Tick(DeltaTime);
}


void UMeshSpaceDeformerTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();

	AxisRenderer.BeginFrame(RenderAPI, CameraState);

	//Update the operator (pass it relevant data)
	UpdateOp();

	//Update the handle visualization and render it.
	Handle.SetBasis(PrincipalAxesObjectSpace[0], PrincipalAxesObjectSpace[1], PrincipalAxesObjectSpace[2]);
	Handle.UpdateDisplayData(SelectedAxis, LowerBoundsInterval, UpperBoundsInterval, PrincipalAxesHalfExtentCoeff);
	Handle.SetPosition(AxisCentroidWorldSpace);
	Handle.Render();


	AxisRenderer.EndFrame();
	GetToolManager()->PostInvalidation();
}



void FHandleWidget::UpdateHover(const FInputDeviceRay & DevicePos , const FTransform& WorldTransform, TFunction<bool(const FVector3d&, const FVector3d&)> ToleranceFunction)
{
	FVector3d LocalPosition, LocalNormal;
	FGeometrySet3::FNearest Nearest;

	//Reset all handle axes and vertex states
	for (unsigned k = 0; k < 3; ++k)
	{
		Axes[k].State = IHandleWidgetSelectable::Default;
		Axes[k].Vertices[0].State = IHandleWidgetSelectable::Default;
		Axes[k].Vertices[1].State = IHandleWidgetSelectable::Default;
	}

	//If we're hovering over a vertex, change its state
	bool bFound = GeometrySet.FindNearestPointToRay(DevicePos.WorldRay, Nearest, ToleranceFunction);
	if (bFound)
	{ // Found a hit point
		int j = Nearest.ID % 2;
		int i = (Nearest.ID) / 2;
		SelectedVertexID = j;
		Axes[i].Vertices[j].State = IHandleWidgetSelectable::Hovering;
		Axes[i].Vertices[(j + 1) % 2].State = IHandleWidgetSelectable::Default;
	}else
	{
		SelectedVertexID = -1;
	}
	
	//TODO: Do the same for the axes. The geometry set should already contain the axes, just need to decide how the UI will work.


}

void FHandleWidget::DragUpdateBounds(const FRay3d& WorldRay)
{
	FVector3d ClosestPtOnSegment;
	FHandleWidgetAxis* SelectedAxis = GetSelectedAxis();

	FVector3d V0 = SelectedAxis->WorldCenter + SelectedAxis->Axis * 99999.0;
	FVector3d V1 = SelectedAxis->WorldCenter - SelectedAxis->Axis * 99999.0;

	double SqDist = RayToSegmentSquareDist(WorldRay, V0, V1, &ClosestPtOnSegment, nullptr);

	FHandleWidgetVertex* SelectedVertex = GetSelectedVertex();
	SelectedVertex->State = IHandleWidgetSelectable::Selected;
	SelectedVertex->SetPosition(ClosestPtOnSegment);
}

#undef LOCTEXT_NAMESPACE

