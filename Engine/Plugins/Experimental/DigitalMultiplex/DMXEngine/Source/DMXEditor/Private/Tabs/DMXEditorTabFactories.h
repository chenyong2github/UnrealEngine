// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Widgets/SWidget.h"
#include "Textures/SlateIcon.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

#define LOCTEXT_NAMESPACE "DMXEditorTabFactories"

class FAssetEditorToolkit;
class SDockTab;

struct FDMXEditorPropertyTabSummoner : public FWorkflowTabFactory
{
public:
	FDMXEditorPropertyTabSummoner(const FName& InIdentifier, TSharedPtr<class FAssetEditorToolkit> InHostingApp)
		: FWorkflowTabFactory(InIdentifier, InHostingApp)
	{
	}

	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;
};

struct FDMXEditorControllersSummoner : public FDMXEditorPropertyTabSummoner
{
public:
	FDMXEditorControllersSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("DMXControllersTab", "Controllers");
	}
};

struct FDMXEditorFixtureTypesSummoner : public FDMXEditorPropertyTabSummoner
{
public:
	FDMXEditorFixtureTypesSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("DMXFixtureTypesTab", "Fixture Types");
	}
};

struct FDMXEditorFixturePatchSummoner : public FDMXEditorPropertyTabSummoner
{
public:
	FDMXEditorFixturePatchSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("DMXFixturePatchTab", "Fixture Patch");
	}
};

struct FDMXEditorInputConsoleSummoner : public FWorkflowTabFactory
{
public:
	FDMXEditorInputConsoleSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("DMXInputConsoleTab", "Monitor");
	}
};

struct FDMXEditorOutputConsoleSummoner : public FWorkflowTabFactory
{
public:
	FDMXEditorOutputConsoleSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("DMXOutputConsoleTab", "Output Console");
	}
};

#undef LOCTEXT_NAMESPACE
