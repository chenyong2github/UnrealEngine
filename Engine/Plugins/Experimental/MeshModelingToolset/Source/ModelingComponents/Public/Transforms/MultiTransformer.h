// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolContextInterfaces.h"
#include "ToolDataVisualizer.h"
#include "FrameTypes.h"
#include "InteractiveGizmo.h"
#include "BaseGizmos/TransformGizmo.h"
#include "InteractiveGizmoManager.h"
#include "MultiTransformer.generated.h"


UENUM()
enum class EMultiTransformerMode
{
	DefaultGizmo = 1,
	QuickAxisTranslation = 2
};


/**
 * UMultiTransformer abstracts both a default TRS Gizmo, and the "Quick" translate/rotate Gizmos.
 */
UCLASS()
class MODELINGCOMPONENTS_API UMultiTransformer : public UObject
{
	GENERATED_BODY()
public:
	
	virtual void Setup(UInteractiveGizmoManager* GizmoManager, IToolContextTransactionProvider* TransactionProviderIn);
	virtual void Shutdown();

	virtual void Tick(float DeltaTime);

	virtual void InitializeGizmoPositionFromWorldFrame(const FFrame3d& Frame, bool bResetScale = true);
	virtual void UpdateGizmoPositionFromWorldFrame(const FFrame3d& Frame, bool bResetScale = true);

	virtual void ResetScale();

	virtual const FFrame3d& GetCurrentGizmoFrame() const { return ActiveGizmoFrame; }
	virtual const FVector3d& GetCurrentGizmoScale() const { return ActiveGizmoScale; }
	virtual bool InGizmoEdit() const { return bInGizmoEdit;	}

	virtual EMultiTransformerMode GetMode() const { return ActiveMode; }
	virtual void SetMode(EMultiTransformerMode NewMode);

	virtual void SetGizmoVisibility(bool bVisible);

	virtual void SetOverrideGizmoCoordinateSystem(EToolContextCoordinateSystem CoordSystem);

	virtual void SetEnabledGizmoSubElements(ETransformGizmoSubElements EnabledSubElements);

	void SetSnapToWorldGridSourceFunc(TUniqueFunction<bool()> EnableSnapFunc);

public:
	DECLARE_MULTICAST_DELEGATE(FMultiTransformerEvent);

	/** This delegate is fired when a drag is started */
	FMultiTransformerEvent OnTransformStarted;

	/** This delegate is fired when a drag is updated */
	FMultiTransformerEvent OnTransformUpdated;

	/** This delegate is fired when the drag is completed */
	FMultiTransformerEvent OnTransformCompleted;


public:
	UPROPERTY()
	UInteractiveGizmoManager* GizmoManager;

	IToolContextTransactionProvider* TransactionProvider;

	EMultiTransformerMode ActiveMode;

	ETransformGizmoSubElements ActiveGizmoSubElements = ETransformGizmoSubElements::FullTranslateRotateScale;

	EToolContextCoordinateSystem GizmoCoordSystem = EToolContextCoordinateSystem::World;
	bool bForceGizmoCoordSystem = false;

	bool bShouldBeVisible = true;
	FFrame3d ActiveGizmoFrame;
	FVector3d ActiveGizmoScale;

	UPROPERTY()
	UTransformGizmo* TransformGizmo;

	UPROPERTY()
	UTransformProxy* TransformProxy;

	TUniqueFunction<bool()> EnableSnapToWorldGridFunc;

	// called on PlaneTransformProxy.OnTransformChanged
	void OnProxyTransformChanged(UTransformProxy* Proxy, FTransform Transform);
	void OnBeginProxyTransformEdit(UTransformProxy* Proxy);
	void OnEndProxyTransformEdit(UTransformProxy* Proxy);
	bool bInGizmoEdit = false;

	void UpdateShowGizmoState(bool bNewVisibility);


};