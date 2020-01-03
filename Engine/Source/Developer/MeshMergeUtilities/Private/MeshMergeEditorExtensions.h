// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"
#include "Modules/ModuleManager.h"
#include "Delegates/IDelegateInstance.h"

class UStaticMesh;
class UObject;
class UMeshComponent;
class FToolBarBuilder;
class FUICommandList;
class FExtender;
class ISkeletalMeshEditor;

/** Editor extension for adding Bake Material button in various places (SMA instances, Static Mesh editor, Skeletal Mesh Editor) */
class FMeshMergeEditorExtensions
{
public:
	static void OnModulesChanged(FName InModuleName, EModuleChangeReason InChangeReason);
	static void RemoveExtenders();
	static void RegisterMenus();
	
	/** Callback functionality for static mesh editor extension */
	static void AddStaticMeshEditorToolbarExtender();
	static void RemoveStaticMeshEditorToolbarExtender();
	static void HandleAddStaticMeshActionExtenderToToolbar(FToolBarBuilder& ParentToolbarBuilder, UStaticMesh* Mesh);
	static TSharedRef<FExtender> GetStaticMeshEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects);

	static FDelegateHandle StaticMeshEditorExtenderHandle;
};