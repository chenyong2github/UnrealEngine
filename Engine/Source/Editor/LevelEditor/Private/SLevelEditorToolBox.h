// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorModeUILayer.h"
#include "Textures/SlateIcon.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Editor.h"
#include "ILevelEditor.h"
#include "Misc/NotifyHook.h"
#include "StatusBarSubsystem.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"

class FExtender;
class SBorder;
class IToolkit;
class SDockTab;
class ILevelEditor;

class FLevelEditorModeUILayer : public FAssetEditorModeUILayer
{
public:
	FLevelEditorModeUILayer(const IToolkitHost* InToolkitHost);
	FLevelEditorModeUILayer() {};
	virtual ~FLevelEditorModeUILayer() override;
	virtual void OnToolkitHostingStarted(const TSharedRef<IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<IToolkit>& Toolkit) override;

	virtual TSharedPtr<FWorkspaceItem> GetModeMenuCategory() override;
	virtual const FName GetStatusBarName() const override
	{
		static const FName LevelEditorStatusBarName = "LevelEditor.StatusBar";
		return LevelEditorStatusBarName;
	}
protected:
	virtual void RegisterLayoutExtensions(FLayoutExtender& Extender) override;
};
