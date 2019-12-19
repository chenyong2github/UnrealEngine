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
enum class EMakeMeshShapeType : uint32
{
	None			 = 0x000 UMETA(DisplayName = "None", Hidden),
	All				 = 0xfff UMETA(DisplayName = "All", Hidden), 
	Box				 = 0x001 UMETA(DisplayName = "Box"),
	Cylinder		 = 0x002 UMETA(DisplayName = "Cylinder"),
	Cone			 = 0x004 UMETA(DisplayName = "Cone"),
	Arrow			 = 0x008 UMETA(DisplayName = "Arrow"),
	Rectangle		 = 0x010 UMETA(DisplayName = "Rectangle"),
	RoundedRectangle = 0x020 UMETA(DisplayName = "Rounded Rectangle"),
	Disc			 = 0x040 UMETA(DisplayName = "Disc"),
	PuncturedDisc	 = 0x080 UMETA(DisplayName = "Punctured Disc"),
	Torus			 = 0x100 UMETA(DisplayName = "Torus"),
	Sphere			 = 0x200 UMETA(DisplayName = "Sphere"),
	SphericalBox	 = 0x400 UMETA(DisplayName = "Spherical Box")
	
};
ENUM_CLASS_FLAGS(EMakeMeshShapeType);

/** Placement Target Types */
UENUM()
enum class EMakeMeshPlacementType : uint8
{
	GroundPlane = 0,
	OnScene = 1
};

/** Placement Pivot Location */
UENUM()
enum class EMakeMeshPivotLocation : uint8
{
	Base,
	Centered,
	Top
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

	/** Radius of additional circular features of the shape (not implicitly defined by the width of the shape) */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Feature Radius", UIMin = "1.0", UIMax = "1000.0", ClampMin = "0.0001", ClampMax = "1000000.0"))
	float FeatureRadius;

	/** Rotation around up axis */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (DisplayName = "Rotation", UIMin = "0.0", UIMax = "360.0"))
	float Rotation;

	/** Type of shape to generate */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
	EMakeMeshPlacementType PlaceMode;

	/** Center shape at click point */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
    EMakeMeshPivotLocation PivotLocation;

	/** Align shape to placement surface */
	UPROPERTY(EditAnywhere, Category = ShapeSettings, meta = (EditCondition = "PlaceMode == EMakeMeshPlacementType::OnScene"))
	bool bAlignShapeToPlacementSurface = true;

	/** If the shape settings haven't changed, create instances of the last created asset rather than creating a whole new asset.  If false, all created actors will have separate underlying mesh assets. */
	UPROPERTY(EditAnywhere, Category = ShapeSettings)
	bool bInstanceLastCreatedAssetIfPossible = true;

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



UCLASS(Transient)
class MESHMODELINGTOOLS_API ULastActorInfo : public UObject
{
	GENERATED_BODY()

public:
	FString Label = "";

	UPROPERTY()
	AActor* Actor = nullptr;
	
	UPROPERTY()
	UStaticMesh* StaticMesh = nullptr;

	UPROPERTY()
	UProceduralShapeToolProperties* ShapeSettings;

	UPROPERTY()
	UNewMeshMaterialProperties* MaterialProperties;

	bool IsInvalid()
	{
		return Actor == nullptr || StaticMesh == nullptr || ShapeSettings == nullptr || MaterialProperties == nullptr;
	}
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


	/**
	 * Checks if the passed-in settings would create the same asset as the current settings
	 */
	bool IsEquivalentLastGeneratedAsset()
	{
		if (LastGenerated == nullptr || LastGenerated->IsInvalid())
		{
			return false;
		}
		// manual diff because not all shape setting changes result in a different asset (e.g. some just affect the transform)
		return
			LastGenerated->ShapeSettings->Subdivisions == ShapeSettings->Subdivisions &&
			LastGenerated->ShapeSettings->Slices == ShapeSettings->Slices &&
			LastGenerated->ShapeSettings->PivotLocation == ShapeSettings->PivotLocation &&
			LastGenerated->ShapeSettings->FeatureRadius == ShapeSettings->FeatureRadius &&
			LastGenerated->ShapeSettings->Height == ShapeSettings->Height &&
			LastGenerated->ShapeSettings->Width == ShapeSettings->Width &&
			LastGenerated->ShapeSettings->Shape == ShapeSettings->Shape &&
			LastGenerated->MaterialProperties->UVScale == MaterialProperties->UVScale &&
			LastGenerated->MaterialProperties->bWorldSpaceUVScale == MaterialProperties->bWorldSpaceUVScale
			;
	}


	UPROPERTY()
	UPreviewMesh* PreviewMesh;

	UPROPERTY()
	ULastActorInfo* LastGenerated;



protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	void UpdatePreviewPosition(const FInputDeviceRay& ClickPos);
	FFrame3f ShapeFrame;

	void UpdatePreviewMesh();

	void GenerateCylinder(FDynamicMesh3* OutMesh);
	void GenerateCone(FDynamicMesh3* OutMesh);
	void GenerateBox(FDynamicMesh3* OutMesh);
	void GenerateRectangle(FDynamicMesh3* OutMesh);
	void GenerateRoundedRectangle(FDynamicMesh3* OutMesh);
	void GenerateDisc(FDynamicMesh3* OutMesh);
	void GeneratePuncturedDisc(FDynamicMesh3* OutMesh);
	void GenerateTorus(FDynamicMesh3* OutMesh);
	void GenerateSphere(FDynamicMesh3* OutMesh);
	void GenerateSphericalBox(FDynamicMesh3* OutMesh);
	void GenerateArrow(FDynamicMesh3* OutMesh);
};
