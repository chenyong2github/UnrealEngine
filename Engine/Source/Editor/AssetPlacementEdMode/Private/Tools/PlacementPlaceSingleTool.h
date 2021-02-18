// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveToolBuilder.h"
#include "Tools/PlacementClickDragToolBase.h"

#include "PlacementPlaceSingleTool.generated.h"

struct FAssetPlacementInfo;

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

UCLASS(Transient)
class UPlacementModePlaceSingleTool : public UPlacementClickDragToolBase
{
	GENERATED_BODY()

public:
	UPlacementModePlaceSingleTool();
	~UPlacementModePlaceSingleTool();
	UPlacementModePlaceSingleTool(FVTableHelper& Helper);
	
	constexpr static TCHAR ToolName[] = TEXT("PlaceSingleTool");

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnClickPress(const FInputDeviceRay& Ray) override;

	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

protected:
	void GeneratePreviewPlacementData(const FInputDeviceRay& DevicePos);
	void CreatePreviewElements(const FInputDeviceRay& DevicePos);
	void UpdatePreviewElements(const FInputDeviceRay& DevicePos);
	void DestroyPreviewElements();

	UPROPERTY()
	TObjectPtr<UPlacementModePlaceSingleSettings> SingleToolSettings;

	TUniquePtr<FAssetPlacementInfo> PreviewPlacementInfo;
	TArray<FTypedElementHandle> PreviewElements;
};
