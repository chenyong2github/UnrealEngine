// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "DynamicMeshAABBTree3.h"
#include "Drawing/ToolDataVisualizer.h"
#include "Transforms/QuickAxisTranslater.h"
#include "Transforms/QuickAxisRotator.h"
#include "Changes/MeshVertexChange.h"
#include "GroupTopology.h"
#include "Spatial/GeometrySet3.h"
#include "Selection/GroupTopologySelector.h"
#include "Operations/GroupTopologyDeformer.h"
#include "Curves/CurveFloat.h"
#include "SpaceDeformerOps/MeshSpaceDeformerOp.h"
#include "SpaceDeformerOps/BendMeshOp.h"
#include "SpaceDeformerOps/TwistMeshOp.h"
#include "SpaceDeformerOps/FlareMeshOp.h"
#include "SimpleDynamicMeshComponent.h"

#include "ModelingOperators/Public/ModelingTaskTypes.h"


#include "MeshSpaceDeformerTool.generated.h"

class FMeshVertexChangeBuilder;

/**
 * ToolBuilder
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSpaceDeformerToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	UMeshSpaceDeformerToolBuilder()
	{
	}

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};

/** ENonlinearOperation determines which type of nonlinear deformation will be applied*/
UENUM()
enum class  ENonlinearOperationType : int8
{
	Bend		UMETA(DisplayName = "Bend"),
	Flare		UMETA(DisplayName = "Flare"),
	Twist		UMETA(DisplayName = "Twist"),
	//Sinusoid	UMETA(DisplayName = "Sinusoid"),
	//Wave		UMETA(DisplayName = "Wave"),
	//Squish	UMETA(DisplayName = "Squish")
};

/**
* \brief IHandleWidgetSelectable is a pure virtual base class for elements of the HandleWidget which may be selectable (i.e. the end points of line segments or the axes themselves)
*/
class IHandleWidgetSelectable
{
public:
	IHandleWidgetSelectable() = delete;
	IHandleWidgetSelectable(const FLinearColor& DefaultColor, const FLinearColor& HoveringColor, const FLinearColor& SelectedColor) : StateColors{ DefaultColor,HoveringColor,SelectedColor } 
	{
	};
	virtual ~IHandleWidgetSelectable() 
	{
	};

	/**
	* \brief Sets the renderer to be used by the tool
	*/
	virtual inline void SetRenderer(FToolDataVisualizer* Target) 
	{ 
		Renderer = Target;
	};

	/**
	* \brief Sets the world position of the tool
	*/ 
	virtual inline void SetPosition(const FVector3d& Pos) 
	{
		WorldCenter = Pos;
	};

	/**
	* \brief Gets the color depnding on the state of the selectable element
	*/
	virtual inline const FLinearColor& GetColor() const 
	{
		return StateColors[static_cast<int8>(State)];
	}

	/**
	* \brief Sets the visibility of the specified widget element
	*/
	virtual inline void SetVisible(bool Enable) 
	{
		Visible = Enable;
	};

	FToolDataVisualizer* Renderer{nullptr};

	enum EDisplayState : int8
	{
		Default,  // Unselected, the default display color
		Hovering, // The element is being hovered over
		Selected  // The element is selected
	};

	EDisplayState State = EDisplayState::Default;
	FLinearColor StateColors[3];

	FVector3d WorldCenter;
	bool Visible{false};
	virtual void Render() = 0;
};

/**
* \brief This object is located on the ends of the line segments which extend from the center of the widget. 
*/
class FHandleWidgetVertex : public IHandleWidgetSelectable
{
public:
	FHandleWidgetVertex() : IHandleWidgetSelectable
	{
		FLinearColor{1.f,1.f,1.f,1.f},	// White
		FLinearColor{0.f,1.f,1.f,1.f},	// Yellow
		FLinearColor{1.f,0.0f,1.f,1.f}	// Pink
	} {};
	virtual void Render() override
	{
		if (Visible)
		{
			float Width = State != Default ? 4.0 : 2.0;
			Renderer->DrawViewFacingCircle(WorldCenter,4.0, 16, GetColor(), Width,false);
		}
	};
};


/**
* \brief Represents an axis of the handle widget, each axis is selectable and has two vertices located at the end points
*/
class FHandleWidgetAxis : public IHandleWidgetSelectable
{
public:
	FHandleWidgetAxis() : IHandleWidgetSelectable
	{
		FLinearColor{0.2f,0.2f,1.f,1.f}, // Light-blue
		FLinearColor{0.f,1.f,1.f,1.f},	 // Yellow
		FLinearColor{1.f,0.f,1.f,1.f}	 // Pink
	} {};

	double LowerExtentLength;
	double UpperExtentLength;
	
	//Identity
	FMatrix3d Basis{ 
		FVector3d{1.0,0.0,0.0}, 
		FVector3d{0.0,1.0,0.0}, 
		FVector3d{0.0,0.0,1.0}, 
		false };

	//The direction vector representing the local space direction of the line segment this handle axis represents
	FVector3d Axis;

	void SetAxis(const FVector3d& TargetAxis) {Axis = TargetAxis;};

	void SetBasis(const FVector3d& V0, const FVector3d& V1, const FVector3d& V2 ) { Basis = FMatrix3d{ V0, V1, V2, false }; };

	FHandleWidgetVertex Vertices[2];
	double SelectedWidth = 2.5;
	double DefaultWidth = 1.5;

	/**
	* \brief Sets the center position from the center of the handle. This, in turn, sets the position of its associated vertices
	*/
	virtual inline void SetPosition(const FVector3d& Center) override
	{
		WorldCenter = Center;

		const FVector3d Positions[2]
		{
			WorldCenter + Axis * LowerExtentLength,
			WorldCenter + Axis * UpperExtentLength
		};

		for (uint8 i = 0; i < 2; ++i)
		{
			Vertices[i].SetPosition(Positions[i]);
		}

	};

	virtual void Render() override
	{
		double Width = State == EDisplayState::Default ? DefaultWidth : SelectedWidth;

		for (uint8 i = 0; i < 2; ++i)
		{
			Vertices[i].Render();
		}

		if (Visible)
		{
			Renderer->DrawLine(Vertices[0].WorldCenter, Vertices[1].WorldCenter, GetColor(), Width, false);
		}
	};

};

/**
* \brief This purpose of this "widget" is to visualize the world space center of the transformation being applied by a Mesh Space Deformer operator. 
  \par This is meant to be an analogous visualizer to the "handle" widgets used by popular modeling tools i.e. maya
*/
class FHandleWidget
{
public:
	FHandleWidget(FToolDataVisualizer* Renderer) 
	{
		for (unsigned i=0;i<3;++i)
		{
			Axes[i].SetRenderer(Renderer);


			for (unsigned j=0;j<2;++j)
			{
				Axes[i].Vertices[j].SetRenderer(Renderer);
			}
			const FVector3d& A = Axes[i].Vertices[0].WorldCenter;
			const FVector3d& B = Axes[i].Vertices[1].WorldCenter;
			GeometrySet.AddPoint(i * 2 , A);
			GeometrySet.AddPoint(i * 2 + 1, B);
			GeometrySet.AddCurve(i, FPolyline3d{ TArray<FVector3d>{A,B} });

		}


	};
	virtual ~FHandleWidget() {};

	/**
	* \brief Returns a pointer to the Handle Widget Axis related object as specified by the Selection
	*/
	FHandleWidgetAxis* GetSelectedAxis() { return &Axes[SelectedAxisID];};

	/**
	* \brief Returns a pointer to the Handle Widget Axis related object as specified by the Selection
	*/
	FHandleWidgetVertex* GetSelectedVertex() { return SelectedVertexID == -1 ? nullptr : &Axes[SelectedAxisID].Vertices[SelectedVertexID]; };
	int SelectedAxisID;
	int SelectedVertexID;

	FHandleWidgetAxis Axes[3];
	FVector3d WorldCenter;

	/** Given a ray, tests the geometry collection and updates the corresponding selectable handle elements */
	void UpdateHover(const FInputDeviceRay& DevicePos, const FTransform& WorldTransform, TFunction<bool(const FVector3d&, const FVector3d&)> ToleranceFunction);
	void SetPosition(const FVector3d& Center)
	{
		WorldCenter = Center;


		for (unsigned i = 0; i < 3; ++i)
		{
			Axes[i].SetPosition(Center);
			const FVector3d& A = Axes[i].Vertices[0].WorldCenter;
			const FVector3d& B = Axes[i].Vertices[1].WorldCenter;
			GeometrySet.UpdatePoint(i * 2 , A);
			GeometrySet.UpdatePoint(i * 2 + 1, B);
			GeometrySet.UpdateCurve(i, FPolyline3d{ TArray<FVector3d>{A,B} });
		}
	};

	void SetBasis(const FVector3d& Axis0, const FVector3d& Axis1, const FVector3d& Axis2 )
	{
		Axes[0].SetAxis(Axis0);
		Axes[1].SetAxis(Axis1);
		Axes[2].SetAxis(Axis2);
	};
	void UpdateDisplayData(int SelectedAxis, double LowerBoundsInterval, double UpperBoundsInterval, const FVector3d& HalfExtents)
	{

		SelectedAxisID = SelectedAxis;
		for (unsigned i = 0; i < 3; ++i)
		{
			if ( i == SelectedAxis )
			{
				Axes[i].State = IHandleWidgetSelectable::EDisplayState::Selected;
				Axes[i].SetVisible(true);
				Axes[i].Vertices[0].SetVisible(true);
				Axes[i].Vertices[1].SetVisible(true);
				Axes[i].LowerExtentLength = HalfExtents[i] * LowerBoundsInterval;
				Axes[i].UpperExtentLength = HalfExtents[i] * UpperBoundsInterval;
			}else
			{
				Axes[i].SetVisible(true);
				Axes[i].Vertices[0].SetVisible(false);
				Axes[i].Vertices[1].SetVisible(false);
				Axes[i].LowerExtentLength = -HalfExtents[i] ;
				Axes[i].UpperExtentLength = HalfExtents[i] ;
			}

		}
	}

	/** Called when updating a click-and-drag transaction, updates the positions of the selected vertices based on the new mouse input position*/
	void DragUpdateBounds(const FRay3d& WorldRay);

	void Render() 
	{
		for (unsigned i=0;i<3;++i)
		{
			Axes[i].Render();
		}
	};

	/** The set of geometry containing the vertices which are draggable and the axes objects which are selectable */
	FGeometrySet3 GeometrySet;

};


/**
 * Applies non-linear deformations to a mesh 
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSpaceDeformerTool : public UMeshSurfacePointTool
{
	GENERATED_BODY()

public:
	UMeshSpaceDeformerTool();

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	FMatrix3d CalculateBestAxis(FDynamicMesh3 * Mesh, const TArray<FVector3d>& PositionBuffer, FTransform WorldTransform, FVector3d * ExtentsOut, FVector3d * OriginOut );

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	
	//
	// Input behavior
	//
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;

	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	void UpdateOp();
public:

	float VisualAngleSnapThreshold = 1.5f;

protected:

	//Options
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Operation Type"))
	ENonlinearOperationType SelectedOperationType;

	/** The upper bounds interval corresponds to the region of space which the selected operator will affect. A setting of -1.0 should envelope all points in the "lower" half of the mesh given the axis has been auto-detected. The corresponding upper value of 1 will cover the entire mesh. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Lower Bound", UIMin = "-1.5", UIMax = "0.0", ClampMin = "-1.5", ClampMax = "0.0"))
	double LowerBoundsInterval{ -1.0 };

	/** The upper bounds interval corresponds to the region of space which the selected operator will affect. A setting of 1.0 should envelope all points in the "upper" half of the mesh given the axis has been auto-detected. The corresponding lower value of -1 will cover the entire mesh. */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Upper Bound", UIMin = "0.0", UIMax = "1.5", ClampMin = "0.0", ClampMax = "1.5"))
	double UpperBoundsInterval{ 1.0 };

	/** As each operator has a range of values (i.e. curvature, angle of twist, scale), this represents the percentage passed to the operator as a parameter. In the future, for more control, this should be separated into individual settings for each operator for more precise control */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Modifier Percent", UIMin = "-100.0", UIMax = "100.0", ClampMin = "-100.0", ClampMax = "100.0"))
	double ModifierPercent{ 0.0 };

	/** Automatically detects the principal axes of the point cloud of the mesh, as well as determining the lengths of the maximal elements in those directions, setting the intervals accordingly. */
	UFUNCTION(CallInEditor, Category = Options, meta = (DisplayName = "Auto-detect Axis", ToolTip = "Attempts to automatically align the handle with principal axis."))
	void AutoDetectAxes();

	/** Swaps the secondary axis upon which the operator will work. I.e. bending */
	UFUNCTION(CallInEditor, Category = Options, meta = (DisplayName = "Swap secondary axis"))
	void SwapSecondaryAxis();

	//Calculate the updated object-space position which gets passed to the handle widget.
	void UpdateObjectSpaceAxisCentroid();

	//Updates the upper and lower intervals from the drag update
	void UpdateIntervalsFromDrag();


	UPROPERTY()
	USimpleDynamicMeshComponent* DynamicMeshComponent;

	FViewCameraState CameraState;

	//For buffer re-use and avoiding recurrent dynamic allocation
	TArray<double> PCAVertexPositionBuffer;

	//Array of the original positions of each vertex
	TArray<FVector3d> OriginalPositions;

	//Index of the axes being used. 
	uint8 SelectedAxis{ 2 };
	uint8 SecondaryAxis{ 1 };
	uint8 ThirdAxis{0};

	//Provided PCA, these are the direction vectors representing the principal axes to best align the mesh
	FVector3d PrincipalAxesWorldSpace[3];
	FVector3d PrincipalAxesObjectSpace[3];

	// These represent the distance from the centroid to the farthest elements in the direction (respectively) of the PCA bases. 
	// Specifically, the X element is the shortest of the PCA axes, the Y element is the middle axis, and the Z element represents the axis with the largest degree of variance, meaning Z is the greatest half extent.
	FVector3d PrincipalAxesHalfExtentCoeff;

	//Where the handle widget will be rendered
	FVector3d AxisCentroidWorldSpace;
	FVector3d AxisCentroidObjectSpace;

	FToolDataVisualizer AxisRenderer;
	FHandleWidget Handle{&AxisRenderer};

	FBendMeshOp BendOp;
	FTwistMeshOp TwistOp;
	FFlareMeshOp FlareOp;

	TArray<FMeshSpaceDeformerOp*> Operators
	{
		&BendOp,
		&FlareOp,
		&TwistOp
		//Sinusoid
		//Wave
		//Squish
	};


	// realtime visualization
	void OnDynamicMeshComponentChanged();
	FDelegateHandle OnDynamicMeshComponentChangedHandle;
	
	//
	// Update	
	//
	/** True for the duration of UI click+drag */
	bool bInDrag;
	
private:
};

