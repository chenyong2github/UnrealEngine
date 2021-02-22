// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& DevicePos) override;

protected:
	void GeneratePreviewPlacementData(const FInputDeviceRay& DevicePos);
	void CreatePreviewElements(const FInputDeviceRay& DevicePos);
	void UpdatePreviewElements(const FInputDeviceRay& DevicePos);
	void DestroyPreviewElements();
	void EnterTweakState(TArrayView<const FTypedElementHandle> InElementHandles);
	void ExitTweakState(bool bClearSelectionSet);
	void UpdateElementTransforms(TArrayView<const FTypedElementHandle> InElements, const FTransform& InTransform, bool bLocalTransform = false);
	void NotifyMovementStarted(TArrayView<const FTypedElementHandle> InElements);
	void NotifyMovementEnded(TArrayView<const FTypedElementHandle> InElements);
	void SetupRightClickMouseBehavior();

	FQuat LastGeneratedRotation;
	TUniquePtr<FAssetPlacementInfo> PreviewPlacementInfo;
	TArray<FTypedElementHandle> PreviewElements;
	bool bIsTweaking;
};
