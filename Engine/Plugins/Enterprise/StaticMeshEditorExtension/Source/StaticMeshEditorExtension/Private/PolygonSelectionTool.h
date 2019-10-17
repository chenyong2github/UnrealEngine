// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "EditorModeTools.h"
#include "EdMode.h"
#include "MeshElement.h"

class FPolygonSelectionToolbar;
class FMeshEditingUIContext;
class UEditableMesh;
class UMeshEditorSelectionModifier;
class UOverlayComponent;
class UViewportWorldInteraction;
class UWorld;

struct FElementIDRemappings;
struct FIntersectionData;
struct FQuadIntersectionData;

class FPolygonSelectionTool : public FEdMode
{
public:
	enum class ESelectionMode : uint8
	{
		SelectByFace,
		SelectByConnectedFaces,
		SelectByMeshElement
	};

public:
	static const FEditorModeID EM_PolygonSelection;

public:
	FPolygonSelectionTool();
	virtual ~FPolygonSelectionTool();

	void SetContext(const TSharedPtr<FMeshEditingUIContext>& InEditingContext);

	FName GetSelectionModeName() const { return SelectionModeName; }
	void SetSelectionModeName(FName InSelectionModeName) { SelectionModeName = InSelectionModeName; }
	void SetIncludeBackfaces(bool bInIncludeBackfaces) { bIncludeBackfaces = bInIncludeBackfaces; }

	TArray<FMeshElement> GetSelectedMeshElements(const FMeshElement& MeshElement);

	// Begin FEdMode interface
	virtual void Exit() override;
	virtual bool InputKey( FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event ) override;
	virtual void Tick( FEditorViewportClient* ViewportClient, float DeltaTime ) override;
	virtual bool MouseEnter( FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y );
	virtual bool MouseLeave( FEditorViewportClient* ViewportClient,FViewport* Viewport );
	virtual bool MouseMove(FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y) override;
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const override { OutCursor = EMouseCursor::Crosshairs; return true; }
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool GetPivotForOrbit(FVector& Pivot) const override;
	// End FEdMode interface

	// FEditorCommonDrawHelper interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override { };

protected:
	FIntersectionData BuildIntersectionData(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY);
	FQuadIntersectionData BuildQuadIntersectionData(FEditorViewportClient* ViewportClient, FViewport* Viewport, FIntPoint MinPoint, FIntPoint MaxPoint);

private:
	TSharedPtr<FMeshEditingUIContext> EditingContext;

	TMap<FName, UMeshEditorSelectionModifier*> SelectionModifierMap;

	FName SelectionModeName;

	/** The interactive action currently being performed (and previewed).  These usually happen over multiple frames, and
	    result in a 'final' application of the change that performs a more exhaustive (and more expensive) update. */
	FName ActiveAction;

	FMeshElement HoveredMeshElement;

	FIntPoint StartPoint;
	FIntPoint EndPoint;
	bool bWindowSelectionEnabled;

	bool bIncludeBackfaces;
};