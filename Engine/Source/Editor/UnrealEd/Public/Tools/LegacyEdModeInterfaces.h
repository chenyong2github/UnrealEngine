// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnrealWidgetFwd.h"
#include "Math/Axis.h"
#include "Math/Vector.h"
#include "UObject/Interface.h"

#include "LegacyEdModeInterfaces.generated.h"

class FEditorViewportClient;
struct FConvexVolume;
enum EModeTools : int8;
class FModeTool;
struct FBox;
struct FMatrix;

UINTERFACE(NotBlueprintable, MinimalAPI)
class ULegacyEdModeSelectInterface : public UInterface
{
	GENERATED_BODY()
};

class UNREALED_API ILegacyEdModeSelectInterface
{
	GENERATED_BODY()
public:
	/**
	 * Lets each mode/tool handle box selection in its own way.
	 *
	 * @param	InBox	The selection box to use, in worldspace coordinates.
	 * @return		true if something was selected/deselected, false otherwise.
	 */
	virtual bool BoxSelect(FBox& InBox, bool InSelect = true) = 0;

	/**
	 * Lets each mode/tool handle frustum selection in its own way.
	 *
	 * @param	InFrustum	The selection box to use, in worldspace coordinates.
	 * @return	true if something was selected/deselected, false otherwise.
	 */
	virtual bool FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect = true) = 0;
};

UINTERFACE(NotBlueprintable, MinimalAPI)
class ULegacyEdModeWidgetInterface : public UInterface
{
	GENERATED_BODY()
};

class UNREALED_API ILegacyEdModeWidgetInterface
{
	GENERATED_BODY()

public:
	/** If the EdMode is handling InputDelta (i.e., returning true from it), this allows a mode to indicated whether or not the Widget should also move. */
	virtual bool AllowWidgetMove() = 0;

	/** Check to see if the current widget mode can be cycled */
	virtual bool CanCycleWidgetMode() const = 0;

	virtual bool ShowModeWidgets() const = 0;
	/**
	 * Allows each mode to customize the axis pieces of the widget they want drawn.
	 *
	 * @param	InwidgetMode	The current widget mode
	 *
	 * @return					A bitfield comprised of AXIS_* values
	 */
	virtual EAxisList::Type GetWidgetAxisToDraw(UE::Widget::EWidgetMode InWidgetMode) const = 0;

	/**
	 * Allows each mode/tool to determine a good location for the widget to be drawn at.
	 */
	virtual FVector GetWidgetLocation() const = 0;

	/**
	 * Lets the mode determine if it wants to draw the widget or not.
	 */
	virtual bool ShouldDrawWidget() const = 0;

	/**
	 * Lets each tool determine if it wants to use the editor widget or not.  If the tool doesn't want to use it,
	 * it will be fed raw mouse delta information (not snapped or altered in any way).
	 */
	virtual bool UsesTransformWidget() const = 0;

	/**
	 * Lets each mode selectively exclude certain widget types.
	 */
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const = 0;

	virtual FVector GetWidgetNormalFromCurrentAxis(void* InData) = 0;

	/** @name Current widget axis. */
	//@{
	virtual void SetCurrentWidgetAxis(EAxisList::Type InAxis) = 0;
	virtual EAxisList::Type GetCurrentWidgetAxis() const = 0;
	//@}

	/**
	 * Lets each mode selectively enable widgets for editing properties tagged with 'Show 3D Widget' metadata.
	 */
	virtual bool UsesPropertyWidgets() const = 0;

	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) = 0;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) = 0;

	/** @return True if this mode allows the viewport to use a drag tool */
	virtual bool AllowsViewportDragTool() const = 0;
};

UINTERFACE(NotBlueprintable, MinimalAPI)
class ULegacyEdModeToolInterface : public UInterface
{
	GENERATED_BODY()
};

class UNREALED_API ILegacyEdModeToolInterface
{
	GENERATED_BODY()

public:
	// Tools
	virtual void SetCurrentTool(EModeTools InID) = 0;
	virtual void SetCurrentTool(FModeTool* InModeTool) = 0;
	virtual FModeTool* FindTool(EModeTools InID) = 0;

	virtual const TArray<FModeTool*>& GetTools() const = 0;

	/** Returns the current tool. */
	virtual FModeTool* GetCurrentTool() = 0;
	virtual const FModeTool* GetCurrentTool() const = 0;
};

UINTERFACE(NotBlueprintable, MinimalAPI)
class ULegacyEdModeDrawHelperInterface : public UInterface
{
	GENERATED_BODY()
};

class UNREALED_API ILegacyEdModeDrawHelperInterface
{
	GENERATED_BODY()

public:
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) = 0;
};
