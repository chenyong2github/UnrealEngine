// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/InteractiveToolsCommands.h"



/**
 * TInteractiveToolCommands implementation for this module that provides standard Editor hotkey support
 */
class FModelingToolActionCommands : public TInteractiveToolCommands<FModelingToolActionCommands>
{
public:
	FModelingToolActionCommands();

	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;

	/**
	 * interface that hides various per-tool action sets
	 */

	/**
	 * Register all Tool command sets. Call this in module startup
	 */
	static void RegisterAllToolActions();

	/**
	 * Unregister all Tool command sets. Call this from module shutdown.
	 */
	static void UnregisterAllToolActions();

	/**
	 * Add or remove commands relevant to Tool to the given UICommandList.
	 * Call this when the active tool changes (eg on ToolManager.OnToolStarted / OnToolEnded)
	 * @param bUnbind if true, commands are removed, otherwise added
	 */
	static void UpdateToolCommandBinding(UInteractiveTool* Tool, TSharedPtr<FUICommandList> UICommandList, bool bUnbind = false);

};


// Each TCommands can only have a single (chord,action) binding, regardless of whether these
// would ever be used at the same time. And we cannot create/register TCommands at runtime.
// So, we have to define a separate TCommands instance for each Tool. This is unfortunate.


class FSculptToolActionCommands : public TInteractiveToolCommands<FSculptToolActionCommands>
{
public:
	FSculptToolActionCommands();
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
};


class FTransformToolActionCommands : public TInteractiveToolCommands<FTransformToolActionCommands>
{
public:
	FTransformToolActionCommands();
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
};


class FDrawPolygonToolActionCommands : public TInteractiveToolCommands<FDrawPolygonToolActionCommands>
{
public:
	FDrawPolygonToolActionCommands();
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
};


class FMeshSelectionToolActionCommands : public TInteractiveToolCommands<FMeshSelectionToolActionCommands>
{
public:
	FMeshSelectionToolActionCommands();
	virtual void GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs) override;
};