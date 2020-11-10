// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyActor.h"
#include "WaterBodyRiverActor.generated.h"

class UMaterialInstanceDynamic;
class USplineMeshComponent;

// ----------------------------------------------------------------------------------

UCLASS(MinimalAPI)
class URiverGenerator : public UWaterBodyGenerator
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Reset() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;

	const TArray<USplineMeshComponent*>& GetSplineMeshComponents() const { return SplineMeshComponents; }

private:
	void GenerateMeshes();
	void UpdateSplineMesh(USplineMeshComponent* MeshComp, int32 SplinePointIndex);

private:
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<USplineMeshComponent*> SplineMeshComponents;
};

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API AWaterBodyRiver : public AWaterBody
{
	GENERATED_UCLASS_BODY()

public:
	/** AWaterBody Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::River; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual UMaterialInstanceDynamic* GetRiverToLakeTransitionMaterialInstance() override;
	virtual UMaterialInstanceDynamic* GetRiverToOceanTransitionMaterialInstance() override;

	/** IWaterBrushActorInterface Interface */
#if WITH_EDITOR
	virtual TArray<UPrimitiveComponent*> GetBrushRenderableComponents() const override;
#endif //WITH_EDITOR

	void SetLakeTransitionMaterial(UMaterialInterface* InMat);
	void SetOceanTransitionMaterial(UMaterialInterface* InMat);

protected:
	/** AWaterBody Interface */
	virtual void InitializeBody() override;
	virtual bool IsBodyInitialized() const override { return !!RiverGenerator; }
	virtual void UpdateMaterialInstances() override;
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) override;
#if WITH_EDITOR
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged) override;
#endif

	void CreateOrUpdateLakeTransitionMID();
	void CreateOrUpdateOceanTransitionMID();

	UPROPERTY(NonPIEDuplicateTransient)
	URiverGenerator* RiverGenerator;

	/** Material used when a river is overlapping a lake. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "River to Lake Transition"))
	UMaterialInterface* LakeTransitionMaterial;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "LakeTransitionMaterial"))
	UMaterialInstanceDynamic* LakeTransitionMID;

	/** This is the material used when a river is overlapping the ocean. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (DisplayName = "River to Ocean Transition"))
	UMaterialInterface* OceanTransitionMaterial;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "OceanTransitionMaterial"))
	UMaterialInstanceDynamic* OceanTransitionMID;
};