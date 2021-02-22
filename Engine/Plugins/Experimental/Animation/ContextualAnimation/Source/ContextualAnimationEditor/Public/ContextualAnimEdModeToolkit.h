// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class UContextualAnimEdModeSettings;
class FContextualAnimEdMode;

class FContextualAnimEdModeToolkit : public FModeToolkit, public FGCObject
{
public:

	FContextualAnimEdModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	FContextualAnimEdMode* GetContextualAnimEdMode() const;

	UContextualAnimEdModeSettings* GetSettings() const { return Settings; }

private:

	UContextualAnimEdModeSettings* Settings;

	TSharedPtr<SWidget> ToolkitWidget;

	TSharedPtr<IDetailsView> EdModeSettingsWidget;

	TSharedPtr<IDetailsView> PreviewManagerWidget;
};
