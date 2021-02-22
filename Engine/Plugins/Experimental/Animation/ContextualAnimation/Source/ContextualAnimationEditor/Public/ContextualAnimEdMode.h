// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "EdMode.h"

class FContextualAnimEdModeToolkit;

class FContextualAnimEdMode : public FEdMode
{
public:

	const static FEditorModeID EM_ContextualAnimEdModeId;

	FContextualAnimEdMode();
	virtual ~FContextualAnimEdMode();

	// FEdMode interface
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool UsesToolkits() const override;
	// End of FEdMode interface

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	static FContextualAnimEdMode& Get();

	TSharedPtr<FContextualAnimEdModeToolkit> GetContextualAnimEdModeToolkit() const;

	class UContextualAnimPreviewManager* PreviewManager;

	bool GetHitResultUnderCursor(FHitResult& OutHitResult, FEditorViewportClient* InViewportClient, const FViewportClick& Click) const;
};
