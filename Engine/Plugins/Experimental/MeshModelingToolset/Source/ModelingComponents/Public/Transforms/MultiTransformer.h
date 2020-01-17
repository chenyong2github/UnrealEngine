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

 */
UCLASS()
class MODELINGCOMPONENTS_API UMultiTransformer : public UObject
{
	GENERATED_BODY()
public:
	
	virtual void Setup(UInteractiveGizmoManager* GizmoManager);
	virtual void Shutdown();

	virtual void Tick(float DeltaTime);

	virtual void SetGizmoPositionFromWorldFrame(const FFrame3d& Frame);
	virtual void SetGizmoPositionFromWorldPos(const FVector& Position, const FVector& Normal);

	virtual const FFrame3d& GetCurrentGizmoFrame() const { return ActiveGizmoFrame; }
	virtual bool InGizmoEdit() const { return bInGizmoEdit;	}

	virtual EMultiTransformerMode GetMode() const { return ActiveMode; }
	virtual void SetMode(EMultiTransformerMode NewMode);

	virtual void SetGizmoVisibility(bool bVisible);

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
	UInteractiveGizmoManager* GizmoManager;

	EMultiTransformerMode ActiveMode;

	bool bShouldBeVisible = true;
	FFrame3d ActiveGizmoFrame;

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