// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class AActor;
class FContextualAnimViewModel;

struct FSelectionCriterionHitProxyData
{
	FName RoleName = NAME_None;
	int32 VariantIdx = INDEX_NONE;
	int32 CriterionIdx = INDEX_NONE;
	int32 DataIdx = INDEX_NONE;

	FSelectionCriterionHitProxyData(){}
	FSelectionCriterionHitProxyData(const FName& InRoleName, int32 InVariantIdx, int32 InCriterionIdx, int32 InDataIdx)
		: RoleName(InRoleName)
		, VariantIdx(InVariantIdx)
		, CriterionIdx(InCriterionIdx)
		, DataIdx(InDataIdx)
	{}

	void Reset()
	{
		RoleName = NAME_None;
		VariantIdx = INDEX_NONE;
		CriterionIdx = INDEX_NONE;
		DataIdx = INDEX_NONE;
	}

	bool IsValid() const 
	{ 
		return	RoleName != NAME_None && 
				VariantIdx != INDEX_NONE && 
				CriterionIdx != INDEX_NONE; 
	}
};

struct HSelectionCriterionHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	HSelectionCriterionHitProxy(const FSelectionCriterionHitProxyData& InData, EHitProxyPriority InPriority = HPP_Wireframe)
		: HHitProxy(InPriority)
		, Data(InData)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }

	FSelectionCriterionHitProxyData Data;
};

class FContextualAnimEdMode : public FEdMode
{
public:

	const static FEditorModeID EdModeId;

	FContextualAnimEdMode();
	virtual ~FContextualAnimEdMode();

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI);
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool AllowWidgetMove() override;
	virtual bool ShouldDrawWidget() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData);
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData);

	bool GetHitResultUnderCursor(FHitResult& OutHitResult, FEditorViewportClient* InViewportClient, const FViewportClick& Click) const;

	void DrawIKTargetsForBinding(class FPrimitiveDrawInterface& PDI, const struct FContextualAnimSceneBinding& Binding) const;

private:

	FContextualAnimViewModel* ViewModel = nullptr;

	TWeakObjectPtr<AActor> SelectedActor;

	FSelectionCriterionHitProxyData SelectedSelectionCriterionData;
};
