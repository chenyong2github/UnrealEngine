// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"

#include "SequencerToolsEditMode.generated.h"


UCLASS()
class USequencerToolsEditMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()
public:
	static FEditorModeID ModeName;

	USequencerToolsEditMode();
	virtual ~USequencerToolsEditMode();

	// UBaseLegacyWidgetEdMode interface
	
	virtual bool UsesToolkits() const  override { return false; }
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;

	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;

};
