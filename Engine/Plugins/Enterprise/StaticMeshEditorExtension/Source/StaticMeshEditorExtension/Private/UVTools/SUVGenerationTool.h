// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "Components/Widget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateColor.h"
#include "UVGenerationSettings.h"
#include "SUVGenerationTool.generated.h"

class SMultiLineEditableTextBox;
class IStaticMeshEditor;
class FArguments;
class FUVGenerationToolbar;
class FUVGenerationTool;
struct FVertexInstanceID;

typedef FName FEditorModeID;

/** Dummy object class needed to use the FUVGenerationSettings custom UI */
UCLASS()
class UGenerateUVSettingsUIHolder : public UObject
{
	GENERATED_BODY()

public:
	virtual void PostEditUndo() override;

	UPROPERTY(EditAnywhere, category = "Projection Settings")
	FUVGenerationSettings GenerateUVSettings;

	DECLARE_EVENT(UGenerateUVSettingsUIHolder, FOnUVSettingsRefreshNeeded);
	FOnUVSettingsRefreshNeeded& OnUVSettingsRefreshNeeded() { return OnUVSettingsRefreshNeededEvent; };

private:
	FOnUVSettingsRefreshNeeded OnUVSettingsRefreshNeededEvent;
};

/**
 * Window that handles UV Generation, settings and controls.
 */
class SGenerateUV : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGenerateUV)
	{}
		SLATE_ARGUMENT(TSharedPtr<FUVGenerationToolbar>, UVGenerationTool)
		SLATE_ARGUMENT(TWeakPtr<IStaticMeshEditor>, StaticMeshEditorPtr)
	SLATE_END_ARGS()

	SGenerateUV();
	virtual ~SGenerateUV();
	void Construct(const FArguments& InArgs);

	/** Changes the target channel for the next empty UV slot */
	void SetNextValidTargetChannel();

private:

	bool GenerateUVTexCoords(TMap<FVertexInstanceID, FVector2D>& OutTexCoords) const;

	FReply OnApplyUV() const;

	FReply OnShowGizmoButtonPressed();

	void SetPreviewModeActivated(bool bActive);

	void OnGenerateUVPreviewModeDeactivated();

	void OnEditorModeChanged(const FEditorModeID& ModeChangedID, bool bIsEnteringMode);

	FSlateColor ShowGizmoButtonColorAndOpacity() const;

	bool IsCustomUVInspectorBoxEnabled() const;

	bool IsTargetingLightMapUVChannel() const;

	void FitSettings();

	void OnFinishedChangingProjectionPropertiesDetailView(const FPropertyChangedEvent& PropertyChangedEvent) const;

	void OnWidgetChangedShapeSettings(const FVector& Position, const FVector& Size, const FRotator& Rotation);

	/** Updates the gizmo preview if we are displaying it */
	void UpdateUVPreview() const;

	/** Returns the number of UV Channels */
	int32 GetNumberOfUVChannels();

	/** Returns the text that should be displayed in the warning box, if some parameters are invalid */
	FText GetWarningText();

	/** Helper function used to convert the Translation component of the UI transform and change it to the SRT Transform format used by the API */
	FVector ConvertTranslationToAPIFormat(const FVector& InTranslation, const FRotator& InRotation) const;

	/** Helper function used to convert the Translation component of the API and change it to a displayable UI value that follows the standard FTransform behavior */
	FVector ConvertTranslationToUIFormat(const FVector& InTranslation, const FRotator& InRotation) const;

private:

	/** The Static Mesh Editor this tool is associated with. */
	TWeakPtr<IStaticMeshEditor> StaticMeshEditorPtr;

	/** Pointer to the structure holding the displayed settings */
	FUVGenerationSettings* GenerateUVSettings;

	/** Dummy object used to customize the UI of FUVGenerationSettings */
	UGenerateUVSettingsUIHolder* SettingObjectUIHolder;
	
	/** Detail view of SettingObjectUIHolder */
	TSharedPtr<class IDetailsView> DetailsView;

	/** Pointer to the UV tool, keeping it alive as long as we hold it */
	TSharedPtr<FUVGenerationToolbar> UVGenerationTool;

	/** The error hint widget used to display errors */
	TSharedPtr<SMultiLineEditableTextBox> ErrorHintWidget;

	TWeakPtr<FUVGenerationTool> GenerateUVPreviewMode;

	FDelegateHandle OnWidgetChangedShapeSettingsHandle;

	FDelegateHandle OnEditorModeChangedHandle;

	bool bIsInPreviewUVMode = false;

	bool bAreDelegatesRegistered = false;
};