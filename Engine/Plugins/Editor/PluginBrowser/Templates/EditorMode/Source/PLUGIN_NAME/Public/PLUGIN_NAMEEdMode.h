// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UEdMode.h"
#include "PLUGIN_NAMEEdMode.generated.h"


UCLASS()
class UPLUGIN_NAMEEdMode : public UEdMode
{
public:
	GENERATED_BODY()

	const static FEditorModeID EM_PLUGIN_NAMEEdModeId;
public:
	UPLUGIN_NAMEEdMode();

	// UEdMode interface - see interface for more overrides
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void CreateToolkit() override;
	//virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	//virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	//virtual void ActorSelectionChangeNotify() override;
	// End of UEdMode interface
};
