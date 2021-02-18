// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementClickDragToolBase.h"

#include "PlacementPlaceSingleTool.generated.h"

UCLASS(Transient, MinimalAPI)
class UPlacementModePlaceSingleToolBuilder : public UPlacementToolBuilderBase
{
	GENERATED_BODY()

protected:
	virtual UPlacementBrushToolBase* FactoryToolInstance(UObject* Outer) const override;
};

UCLASS(config = EditorPerUserSettings)
class UPlacementModePlaceSingleSettings : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool", meta = (InlineEditConditionToggle))
	bool bAlignToCursor = false;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool", meta = (DisplayName = "Align to Cursor", EditCondition = "bAlignToCursor"))
	TEnumAsByte<EAxis::Type> AxisToAlignWithCursor = EAxis::Type::X;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool")
	bool bInvertCursorAxis = false;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool")
	bool bSnapToGridX = false;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool")
	bool bSnapToGridY = false;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool")
	bool bSnapToGridZ = false;

	UPROPERTY(config, EditAnywhere, Category = "Single Place Tool")
	float ScrollWheelOffsetScale = 0.05f;

	virtual bool CanEditChange(const FProperty* Property) const override;
};

UCLASS(MinimalAPI)
class UPlacementModePlaceSingleTool : public UPlacementClickDragToolBase
{
	GENERATED_BODY()

public:
	constexpr static TCHAR ToolName[] = TEXT("PlaceSingleTool");

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnEndDrag(const FRay& Ray) override;

protected:
	virtual FRotator GetFinalRotation(const FTransform& InTransform) override;
	TObjectPtr<UPlacementModePlaceSingleSettings> SingleToolSettings;
};
