// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MaterialShared.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/SingleClickTool.h"
#include "PreviewMesh.h"
#include "Properties/MeshMaterialProperties.h"
#include "AddPrimitiveTool.generated.h"

class FDynamicMesh3;

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UAddPrimitiveToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI;

	UAddPrimitiveToolBuilder() 
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

/** Shape Types */
UENUM()
enum class EMakeMeshShapeType : uint8
{
	None     = 0x00 UMETA(DisplayName = "None", Hidden),
	All      = 0xff UMETA(DisplayName = "All", Hidden), 
	Box      = 0x01 UMETA(DisplayName = "Box"),
	Cylinder = 0x02 UMETA(DisplayName = "Cylinder"),
	Cone     = 0x04 UMETA(DisplayName = "Cone"),
	Plane    = 0x08 UMETA(DisplayName = "Plane"),
	Sphere   = 0x10 UMETA(DisplayName = "Sphere")
};
ENUM_CLASS_FLAGS(EMakeMeshShapeType);

/** Placement Target Types */
UENUM()
enum class EMakeMeshPlacementType : uint8
{
	OnPlane = 0,
	OnScene = 1
};

/** Placement Pivot Location */
UENUM()
enum class EMakeMeshPivotLocation : uint8
{
	Base,
	Centered
};

UCLASS()
class MESHMODELINGTOOLS_API UProceduralShapeToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UProceduralShapeToolProperties();

	// UObject interface
#if WITH_EDITOR
	virtual bool CanEditChange( const UProperty* InProperty ) const override;
#endif // WITH_EDITOR	
	// End of UObject interface

	/** Type of shape to generate */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
	EMakeMeshShapeType Shape;

	/** Width of Shape */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Width", UIMin = "1.0", UIMax = "1000.0", ClampMin = "0.0001", ClampMax = "1000000.0"))
	float Width;

	/** Height of Shape */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Height", UIMin = "1.0", UIMax = "1000.0", ClampMin = "0.0001", ClampMax = "1000000.0"))
	float Height;

	/** Rotation around up axis */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Rotation", UIMin = "0.0", UIMax = "360.0"))
	float Rotation;

	/** Type of shape to generate */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
	EMakeMeshPlacementType PlaceMode;

	/** Center shape at click point */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
    EMakeMeshPivotLocation PivotLocation;

	///** Start Angle of Shape */
	//UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Start Angle", UIMin = "0.0", UIMax = "360.0", ClampMin = "-10000", ClampMax = "10000.0"))
	//float StartAngle;

	///** End Angle of Shape */
	//UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "End Angle", UIMin = "0.0", UIMax = "360.0", ClampMin = "-10000", ClampMax = "10000.0"))
	//float EndAngle;


	/** Number of Slices */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Slices", UIMin = "3", UIMax = "128", ClampMin = "3", ClampMax = "999"))
	int Slices;

	/** Subdivisions */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Subdivisions", UIMin = "0", UIMax = "100", ClampMin = "0", ClampMax = "4000"))
	int Subdivisions;


	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};







/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UAddPrimitiveTool : public USingleClickTool, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	virtual void OnPropertyModified(UObject* PropertySet, UProperty* Property) override;

	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;


	// IHoverBehaviorTarget interface
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;


protected:
	UPROPERTY()
	UProceduralShapeToolProperties* ShapeSettings;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;



	UPROPERTY()
	UPreviewMesh* PreviewMesh;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void UpdatePreviewPosition(const FInputDeviceRay& ClickPos);
	FFrame3f ShapeFrame;

	void UpdatePreviewMesh();

	void GenerateCylinder(FDynamicMesh3* OutMesh);
	void GenerateCone(FDynamicMesh3* OutMesh);
	void GenerateBox(FDynamicMesh3* OutMesh);
	void GeneratePlane(FDynamicMesh3* OutMesh);
	void GenerateSphere(FDynamicMesh3* OutMesh);
};
