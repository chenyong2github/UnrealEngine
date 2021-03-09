// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseTools/MeshSurfacePointTool.h"
#include "BaseTools/BaseBrushTool.h"
#include "Sculpting/MeshBrushOpBase.h"
#include "BoxTypes.h"
#include "Properties/MeshMaterialProperties.h"
#include "Changes/ValueWatcher.h"
#include "BaseDynamicMeshComponent.h"
#include "MeshSculptToolBase.generated.h"


class UMaterialInstanceDynamic;
class UTransformGizmo;
class UTransformProxy;
class UPreviewMesh;


/** Mesh Sculpting Brush Types */
UENUM()
enum class EMeshSculptFalloffType : uint8
{
	Smooth,
	Linear,
	Inverse,
	Round,
	BoxSmooth,
	BoxLinear,
	BoxInverse,
	BoxRound,

	LastValue UMETA(Hidden)
};





UCLASS()
class MESHMODELINGTOOLS_API USculptBrushProperties : public UBrushBaseProperties
{
	GENERATED_BODY()
public:
	USculptBrushProperties()
	{
		this->BrushFalloffAmount = 0.5f;
	}

	/** Depth of Brush into surface along view ray or surface normal, depending on the Active Brush Type */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "-0.5", UIMax = "0.5", ClampMin = "-1.0", ClampMax = "1.0", DisplayPriority = 5, HideEditConditionToggle, EditConditionHides, EditCondition = "bShowPerBrushProps"))
	float Depth = 0;

	/** Allow the Brush to hit the back-side of the mesh */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (DisplayPriority = 6))
	bool bHitBackFaces = true;

	/** Brush stamps are applied at this time interval. 0 for a single stamp, 1 for continuous stamps, 0.5 is a stamp every half-second */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", DisplayPriority = 7, HideEditConditionToggle, EditConditionHides, EditCondition = "bShowFlowRate"))
	float FlowRate = 1.0f;

	/** Minimum world-space spacing between stamps, defined along the brush path. Zero spacing means continuous stamps. */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "0.0", UIMax = "4.0", ClampMin = "0.0", ClampMax = "1000.0", DisplayPriority = 8, HideEditConditionToggle, EditConditionHides, EditCondition = "bShowSpacing"))
	float Spacing = 0.0f;

	/** Lazy brush smooths out the brush path by averaging the cursor positions */
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0", DisplayPriority = 9, HideEditConditionToggle, EditConditionHides, EditCondition = "bShowLazyness"))
	float Lazyness = 0;

	/**  */
	UPROPERTY( meta = (TransientToolProperty))
	bool bShowPerBrushProps = true;

	/**  */
	UPROPERTY(meta = (TransientToolProperty))
	bool bShowLazyness = true;

	/**  */
	UPROPERTY(meta = (TransientToolProperty))
	bool bShowFlowRate = true;

	UPROPERTY(meta = (TransientToolProperty))
	bool bShowSpacing = true;

};




UCLASS()
class MESHMODELINGTOOLS_API UKelvinBrushProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Brush Fall off as fraction of brush size*/
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", ClampMin = "0.0"))
	float FallOffDistance = 1.f;

	/** How much the mesh resists shear */
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", ClampMin = "0.0"))
	float Stiffness = 1.f;

	/** How compressible the spatial region is: 1 - 2 x Poisson ratio */
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", UIMax = "0.1", ClampMin = "0.0", ClampMax = "1.0"))
	float Incompressiblity = 1.f;

	/** Integration steps*/
	UPROPERTY(EditAnywhere, Category = Kelvin, meta = (UIMin = "0.0", UIMax = "100", ClampMin = "0.0", ClampMax = "100"))
	int BrushSteps = 3;
};




UCLASS()
class MESHMODELINGTOOLS_API UWorkPlaneProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY( meta = (TransientToolProperty) )
	bool bPropertySetEnabled = true;

	/** Toggle whether Work Plane Positioing Gizmo is visible */
	UPROPERTY(EditAnywhere, Category = TargetPlane, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	bool bShowGizmo = true;

	/** Toggle whether Work Plane snaps to grid when using Gizmo */
	UPROPERTY(EditAnywhere, Category = TargetPlane, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	bool bSnapToGrid = true;

	UPROPERTY(EditAnywhere, Category = TargetPlane, AdvancedDisplay, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = TargetPlane, AdvancedDisplay, meta = (HideEditConditionToggle, EditCondition = "bPropertySetEnabled == true"))
	FQuat Rotation = FQuat::Identity;
};




UCLASS()
class MESHMODELINGTOOLS_API USculptMaxBrushProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	/** Specify maximum displacement height (relative to brush size) */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush, meta = (UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float MaxHeight = 0.5;

	/** Use maximum height from last brush stroke, regardless of brush size. Note that spatial brush falloff still applies.  */
	UPROPERTY(EditAnywhere, Category = SculptMaxBrush)
	bool bFreezeCurrentHeight = false;
};




/**
 * Base Tool for mesh sculpting tools, provides some shared functionality
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSculptToolBase : public UMeshSurfacePointTool
{
	GENERATED_BODY()
protected:
	using FVector3d = UE::Geometry::FVector3d;
	using FVector3f = UE::Geometry::FVector3f;
	using FVector2d = UE::Geometry::FVector2d;
	using FVector2f = UE::Geometry::FVector2f;
	using FTransform3d = UE::Geometry::FTransform3d;
	using FFrame3d = UE::Geometry::FFrame3d;
	using FRay3d = UE::Geometry::FRay3d;
public:

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void SetWorld(UWorld* World);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	// UMeshSurfacePointTool API
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	// end UMeshSurfacePointTool API

protected:
	virtual void OnTick(float DeltaTime) override;

	
	virtual void OnCompleteSetup();
	virtual void OnBeginStroke(const FRay& WorldRay) { check(false); }
	virtual void OnEndStroke() { check(false); }

public:
	/** Properties that control brush size/etc */
	UPROPERTY()
	USculptBrushProperties* BrushProperties;

	/** Properties for 3D workplane / gizmo */
	UPROPERTY()
	UWorkPlaneProperties* GizmoProperties;


protected:
	UWorld* TargetWorld;		// required to spawn UPreviewMesh/etc
	FViewCameraState CameraState;

	/** Initial transformation on target mesh */
	UE::Geometry::FTransform3d InitialTargetTransform;
	/** Active transformation on target mesh, includes baked scale */
	UE::Geometry::FTransform3d CurTargetTransform;

	FRay3d GetLocalRay(const FRay& WorldRay) const;


	/**
	 * Subclass must implement this and return relevant rendering component
	 */
	virtual UBaseDynamicMeshComponent* GetSculptMeshComponent() { check(false); return nullptr; }

	virtual FDynamicMesh3* GetSculptMesh() { return GetSculptMeshComponent()->GetMesh(); }
	virtual const FDynamicMesh3* GetSculptMesh() const { return const_cast<UMeshSculptToolBase*>(this)->GetSculptMeshComponent()->GetMesh(); }

	virtual FDynamicMesh3* GetBaseMesh() { check(false); return nullptr; }
	virtual const FDynamicMesh3* GetBaseMesh() const { check(false); return nullptr; }


	/**
	 * Subclass calls this to set up editing component
	 */
	void InitializeSculptMeshComponent(UBaseDynamicMeshComponent* Component);


	/**
	 * Subclass can override this to change what results are written.
	 * Default is to apply a default vertex positions update to the target object.
	 */
	virtual void CommitResult(UBaseDynamicMeshComponent* Component);


	//
	// Brush Types
	//
protected:
	UPROPERTY()
	TMap<int32, UMeshSculptBrushOpProps*> BrushOpPropSets;

	TMap<int32, TUniquePtr<FMeshSculptBrushOpFactory>> BrushOpFactories;

	UPROPERTY()
	TMap<int32, UMeshSculptBrushOpProps*> SecondaryBrushOpPropSets;

	TMap<int32, TUniquePtr<FMeshSculptBrushOpFactory>> SecondaryBrushOpFactories;

	void RegisterBrushType(int32 Identifier, TUniquePtr<FMeshSculptBrushOpFactory> Factory, UMeshSculptBrushOpProps* PropSet);
	void RegisterSecondaryBrushType(int32 Identifier, TUniquePtr<FMeshSculptBrushOpFactory> Factory, UMeshSculptBrushOpProps* PropSet);

	virtual void SaveAllBrushTypeProperties(UInteractiveTool* SaveFromTool);
	virtual void RestoreAllBrushTypeProperties(UInteractiveTool* RestoreToTool);

protected:
	TUniquePtr<FMeshSculptBrushOp> PrimaryBrushOp;
	UMeshSculptBrushOpProps* PrimaryVisiblePropSet = nullptr;		// BrushOpPropSets prevents GC of this

	TUniquePtr<FMeshSculptBrushOp> SecondaryBrushOp;
	UMeshSculptBrushOpProps* SecondaryVisiblePropSet = nullptr;

	bool bBrushOpPropsVisible = true;

	void SetActivePrimaryBrushType(int32 Identifier);
	void SetActiveSecondaryBrushType(int32 Identifier);
	TUniquePtr<FMeshSculptBrushOp>& GetActiveBrushOp();
	void SetBrushOpPropsVisibility(bool bVisible);

	//
	// Falloff types
	//
protected:
	TSharedPtr<FMeshSculptFallofFunc> PrimaryFalloff;

	void SetPrimaryFalloffType(EMeshSculptFalloffType Falloff);




	//
	// Brush Size
	//
protected:
	UE::Geometry::FInterval1d BrushRelativeSizeRange;
	double CurrentBrushRadius = 1.0;
	virtual void InitializeBrushSizeRange(const UE::Geometry::FAxisAlignedBox3d& TargetBounds);
	virtual void CalculateBrushRadius();
	virtual double GetCurrentBrushRadius() const { return CurrentBrushRadius; }

	double CurrentBrushFalloff = 0.5;
	virtual double GetCurrentBrushFalloff() const { return CurrentBrushFalloff; }

	double ActivePressure = 1.0;
	virtual double GetActivePressure() const { return ActivePressure; }

	virtual double GetCurrentBrushStrength();
	virtual double GetCurrentBrushDepth();

public:
	virtual void IncreaseBrushRadiusAction();
	virtual void DecreaseBrushRadiusAction();
	virtual void IncreaseBrushRadiusSmallStepAction();
	virtual void DecreaseBrushRadiusSmallStepAction();


	// client currently needs to implement these...
	virtual void IncreaseBrushSpeedAction() {}
	virtual void DecreaseBrushSpeedAction() {}
	virtual void NextBrushModeAction() {}
	virtual void PreviousBrushModeAction() {}



	//
	// Brush/Stroke stuff
	//
protected:
	FFrame3d LastBrushFrameWorld;
	FFrame3d LastBrushFrameLocal;
	int32 LastBrushTriangleID;

	const FFrame3d& GetBrushFrameWorld() const { return LastBrushFrameWorld; }
	const FFrame3d& GetBrushFrameLocal() const { return LastBrushFrameLocal; }
	int32 GetBrushTriangleID() const { return LastBrushTriangleID; }
	void UpdateBrushFrameWorld(const FVector3d& NewPosition, const FVector3d& NewNormal);
	void AlignBrushToView();

	bool GetBrushCanHitBackFaces() const { return BrushProperties->bHitBackFaces; }

	/** @return hit triangle at ray position - subclass must implement this */
	virtual int32 FindHitSculptMeshTriangle(const FRay3d& LocalRay) { check(false); return IndexConstants::InvalidID; }
	/** @return hit triangle at ray position - subclass should implement this for most brushes */
	virtual int32 FindHitTargetMeshTriangle(const FRay3d& LocalRay) { check(false); return IndexConstants::InvalidID;	}

	virtual bool UpdateBrushPositionOnActivePlane(const FRay& WorldRay);
	virtual bool UpdateBrushPositionOnTargetMesh(const FRay& WorldRay, bool bFallbackToViewPlane);
	virtual bool UpdateBrushPositionOnSculptMesh(const FRay& WorldRay, bool bFallbackToViewPlane);



	//
	// Brush Target Plane is plane that some brushes move on
	//
protected:
	FFrame3d ActiveBrushTargetPlaneWorld;
	virtual void UpdateBrushTargetPlaneFromHit(const FRay& WorldRay, const FHitResult& Hit);


	//
	// Stroke Modifiers
	//
protected:
	bool bInStroke = false;
	bool bSmoothing = false;
	bool bInvert = false;
	virtual void SaveActiveStrokeModifiers();
	virtual bool InStroke() const { return bInStroke; }
	virtual bool GetInSmoothingStroke() const { return bSmoothing; }
	virtual bool GetInInvertStroke() const { return bInvert; }

	// when in a stroke, this function determines when a new stamp should be emitted, based on spacing and flow rate settings
	virtual void UpdateStampPendingState();

	// for tracking stroke time and length, to apply spacing and flow rate settings
	double ActiveStrokeTime = 0.0;
	double ActiveStrokePathArcLen = 0.0;
	int LastFlowTimeStamp = 0;
	int LastSpacingTimestamp = 0;
	virtual void ResetStrokeTime();
	virtual void AccumulateStrokeTime(float DeltaTime);

	//
	// Stamps
	//
protected:
	bool bIsStampPending = false;
	FRay PendingStampRay;
	FSculptBrushStamp HoverStamp;
	FSculptBrushStamp CurrentStamp;
	FSculptBrushStamp LastStamp;
	virtual void UpdateHoverStamp(const FFrame3d& StampFrame);
	virtual bool IsStampPending() const { return bIsStampPending; }
	virtual const FRay& GetPendingStampRayWorld() const { return PendingStampRay;  }


	//
	// Stamp ROI Plane is a plane used by some brush ops
	//
protected:
	FFrame3d StampRegionPlane;
	virtual FFrame3d ComputeStampRegionPlane(const FFrame3d& StampFrame, const TArray<int32>& StampTriangles, bool bIgnoreDepth, bool bViewAligned, bool bInvDistFalloff = true);
	virtual FFrame3d ComputeStampRegionPlane(const FFrame3d& StampFrame, const TSet<int32>& StampTriangles, bool bIgnoreDepth, bool bViewAligned, bool bInvDistFalloff = true);


	// Stroke plane is a plane used by some brush ops
	//
protected:
	FFrame3d StrokePlane;
	virtual const FFrame3d& GetCurrentStrokeReferencePlane() const { return StrokePlane; }

	virtual void UpdateStrokeReferencePlaneForROI(const FFrame3d& StampFrame, const TArray<int32>& TriangleROI, bool bViewAligned);
	virtual void UpdateStrokeReferencePlaneFromWorkPlane();


	//
	// Display / Material
	//
public:
	UPROPERTY()
	UMeshEditingViewProperties* ViewProperties;

	UPROPERTY()
	UMaterialInstanceDynamic* ActiveOverrideMaterial;

protected:
	virtual void SetViewPropertiesEnabled(bool bNewValue);
	virtual void UpdateWireframeVisibility(bool bNewValue);
	virtual void UpdateMaterialMode(EMeshEditingMaterialModes NewMode);
	virtual void UpdateFlatShadingSetting(bool bNewValue);
	virtual void UpdateColorSetting(FLinearColor NewColor);
	virtual void UpdateImageSetting(UTexture2D* NewImage);



	//
	// brush indicator
	//
protected:
	// subclasses should call this to create indicator in their ::Setup()
	virtual void InitializeIndicator();

	virtual bool GetIsVolumetricIndicator();

	virtual void ConfigureIndicator(bool bVolumetric);

	virtual void SetIndicatorVisibility(bool bVisible);
	virtual bool GetIndicatorVisibility() const;

protected:
	UPROPERTY()
	UBrushStampIndicator* BrushIndicator;

	UPROPERTY()
	bool bIsVolumetricIndicator;


	UPROPERTY()
	UMaterialInstanceDynamic* BrushIndicatorMaterial;

	UPROPERTY()
	UPreviewMesh* BrushIndicatorMesh;

	// creates default sphere indicator
	UPreviewMesh* MakeDefaultIndicatorSphereMesh(UObject* Parent, UWorld* World, int Resolution = 32);




	//
	// Work Plane
	//
public:
	// plane gizmo
	UPROPERTY()
	UTransformGizmo* PlaneTransformGizmo;

	UPROPERTY()
	UTransformProxy* PlaneTransformProxy;

protected:
	virtual void UpdateWorkPlane();
	virtual bool ShowWorkPlane() const { return false; };

protected:
	TValueWatcher<FVector> GizmoPositionWatcher;
	TValueWatcher<FQuat> GizmoRotationWatcher;

	virtual void UpdateGizmoFromProperties();
	virtual void PlaneTransformChanged(UTransformProxy* Proxy, FTransform Transform);

	enum class EPendingWorkPlaneUpdate
	{
		NoUpdatePending,
		MoveToHitPositionNormal,
		MoveToHitPosition,
		MoveToHitPositionViewAligned
	};
	EPendingWorkPlaneUpdate PendingWorkPlaneUpdate;
	virtual void SetFixedSculptPlaneFromWorldPos(const FVector& Position, const FVector& Normal, EPendingWorkPlaneUpdate UpdateType);
	virtual void UpdateFixedSculptPlanePosition(const FVector& Position);
	virtual void UpdateFixedSculptPlaneRotation(const FQuat& Rotation);
	virtual void UpdateFixedPlaneGizmoVisibility(bool bVisible);

};