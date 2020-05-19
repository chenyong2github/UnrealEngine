// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Templates/SharedPointer.h"
#include "Framework/Docking/TabManager.h"
#include "UAssetEditor.generated.h"

class FBaseAssetToolkit;
class SDockTab;
class FSpawnTabArgs;
class SEditorViewport;
class FLayoutExtender;


/**
 * Base class for all editor modes.
 */
UCLASS(Abstract)
class UNREALED_API UAssetEditor : public UObject, public IAssetEditorInstance
{
	GENERATED_BODY()

public:
	UAssetEditor();

	virtual FName GetEditorName() const override;
	virtual void FocusWindow(UObject* ObjectToFocusOn = nullptr) override;
	virtual bool CloseWindow() override;
	virtual bool IsPrimaryEditor() const override
	{
		return true;
	}
	virtual void InvokeTab(const struct FTabId& TabId) override {}
	virtual FName GetToolbarTabId() const override;
	virtual TSharedPtr<class FTabManager> GetAssociatedTabManager() override;
	virtual double GetLastActivationTime() override
	{
		return 0.0;
	}
	virtual void RemoveEditingAsset(UObject* Asset) override {}

	void Initialize();
	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit);
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit();
	void OnToolkitClosed();

protected:
	FBaseAssetToolkit* ToolkitInstance;

};
