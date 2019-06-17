// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "EdMode.h"

class IPropertyHandle;

class FGeometryCollectionSelectRigidBodyEdMode : public FEdMode
{
public:
	static const FEditorModeID EditorModeID;

	/* Activate this editor mode */
	static void ActivateMode(TSharedRef<IPropertyHandle> PropertyHandleId);

	/* Activate this editor mode */
	static void DeactivateMode();

	/* Return whether Activate this editor mode */
	static bool IsModeActive();

	FGeometryCollectionSelectRigidBodyEdMode() : bIsHoveringGeometryCollection(false) {}
	virtual ~FGeometryCollectionSelectRigidBodyEdMode() {}

	/* FEdMode interface */
	virtual void Enter() override { EnableTransformSelectionMode(true); }
	virtual void Exit() override { EnableTransformSelectionMode(false); PropertyHandleId.Reset(); }
	virtual bool IsCompatibleWith(FEditorModeID /*OtherModeID*/) const override { return false; }

	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const { OutCursor = bIsHoveringGeometryCollection ? EMouseCursor::EyeDropper: EMouseCursor::SlashedCircle; return true; }

	virtual bool UsesTransformWidget() const override { return false; }
	virtual bool UsesTransformWidget(FWidget::EWidgetMode /*CheckMode*/) const override { return false; }

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;

	virtual bool IsSelectionAllowed(AActor* /*InActor*/, bool /*bInSelection */) const override { return false; }

private:
	void EnableTransformSelectionMode(bool bEnable);

private:
	static const int32 MessageKey;
	TWeakPtr<IPropertyHandle> PropertyHandleId;  // Handle of the property that will get updated with the selected rigid body id
	bool bIsHoveringGeometryCollection;
};

#endif
