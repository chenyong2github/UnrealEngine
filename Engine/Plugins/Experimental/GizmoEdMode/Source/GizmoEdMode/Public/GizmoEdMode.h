// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UEdMode.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "UnrealWidget.h"
#include "AssetEditorGizmoFactory.h"

#include "GizmoEdMode.generated.h"

class UTransformGizmo;
class UInteractiveGizmo;
class UInteractiveGizmoManager;

UCLASS()
class GIZMOEDMODE_API UGizmoEdModeSettings : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class GIZMOEDMODE_API UGizmoEdMode : public UEdMode
{
	GENERATED_BODY()
public:
	UGizmoEdMode();

	void AddFactory(TScriptInterface<IAssetEditorGizmoFactory> GizmoFactory);

private:
	void ActorSelectionChangeNotify() override;
	void Enter() override;
	void Exit() override;
	bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;

	bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

	void RecreateGizmo();
	void DestroyGizmo();

	UPROPERTY()
	TArray<TScriptInterface<IAssetEditorGizmoFactory>> GizmoFactories;
	IAssetEditorGizmoFactory* LastFactory = nullptr;

	UPROPERTY()
	TArray<UInteractiveGizmo*> InteractiveGizmos;

	FDelegateHandle WidgetModeChangedHandle;

	bool bNeedInitialGizmos{false};
};
