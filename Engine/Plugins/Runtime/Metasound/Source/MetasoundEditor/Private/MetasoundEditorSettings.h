// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "MetasoundEditorSettings.generated.h"

UENUM()
enum class EMetasoundMemberDefaultWidget : uint8
{
	None,
	Slider,
	RadialSlider UMETA(DisplayName = "Knob"),
};

UENUM()
enum class EMetasoundActiveDetailView : uint8
{
	Metasound,
	General
};

UCLASS(config=EditorPerProjectUserSettings)
class METASOUNDEDITOR_API UMetasoundEditorSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Whether to pin the MetaSound asset type when creating new assets. */
	UPROPERTY(EditAnywhere, config, DisplayName = "Pin MetaSound in Asset Menu", Category = AssetMenu)
	bool bPinMetaSoundInAssetMenu = false;

	/** Whether to pin the MetaSound Source asset type when creating new assets. */
	UPROPERTY(EditAnywhere, config, DisplayName = "Pin MetaSound Source in Asset Menu", Category = AssetMenu)
	bool bPinMetaSoundSourceInAssetMenu = true;

	/** Default author title to use when authoring a new
	  * MetaSound.  If empty, uses machine name by default.
	  */
	UPROPERTY(EditAnywhere, config, Category=General)
	FString DefaultAuthor;

	/** Default pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor DefaultPinTypeColor;

	/** Audio pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor AudioPinTypeColor;

	/** Boolean pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor BooleanPinTypeColor;

	/** Double pin type color */
	//UPROPERTY(EditAnywhere, config, Category = PinColors)
	//FLinearColor DoublePinTypeColor;

	/** Floating-point pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor FloatPinTypeColor;

	/** Integer pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor IntPinTypeColor;

	/** Integer64 pin type color */
	//UPROPERTY(EditAnywhere, config, Category = PinColors)
	//FLinearColor Int64PinTypeColor;

	/** Object pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor ObjectPinTypeColor;

	/** String pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor StringPinTypeColor;

	/** Time pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor TimePinTypeColor;

	/** Trigger pin type color */
	UPROPERTY(EditAnywhere, config, Category=PinColors)
	FLinearColor TriggerPinTypeColor;

	/** Native node class title color */
	UPROPERTY(EditAnywhere, config, Category = NodeTitleColors)
	FLinearColor NativeNodeTitleColor;

	/** Title color for references to MetaSound assets */
	UPROPERTY(EditAnywhere, config, Category = NodeTitleColors)
	FLinearColor AssetReferenceNodeTitleColor;

	/** Input node title color */
	UPROPERTY(EditAnywhere, config, Category = NodeTitleColors)
	FLinearColor InputNodeTitleColor;

	/** Output node title color */
	UPROPERTY(EditAnywhere, config, Category = NodeTitleColors)
	FLinearColor OutputNodeTitleColor;

	/** Variable node title color */
	UPROPERTY(EditAnywhere, config, Category = NodeTitleColors)
	FLinearColor VariableNodeTitleColor;

	/** Whether or not animated connections are enabled. */
	UPROPERTY(EditAnywhere, config, Category = GraphAnimation, meta = (DisplayName = "Animate Connections (Experimental)"))
	bool bAnimateConnections;

	/** Thickness of default envelope analyzer wire thickness when connection analyzer is active. */
	UPROPERTY(EditAnywhere, config, Category = GraphAnimation, meta = (EditCondition = "bAnimateConnections", UIMin = 1, UIMax = 2, ClampMin = 1))
	float ActiveAnalyzerEnvelopeWireThickness;

	/** Thickness of default numeric analyzer wire thickness when connection analyzer is active. */
	UPROPERTY(EditAnywhere, config, Category = GraphAnimation, meta = (EditCondition = "bAnimateConnections", UIMin = 1, UIMax = 10, ClampMin = 1))
	float ActiveAnalyzerNumericWireThickness;

	/** Minimum height scalar of wire signal analyzers (ex. audio, triggers). */
	UPROPERTY(EditAnywhere, config, Category = GraphAnimation, meta = (EditCondition = "bAnimateConnections", UIMin = 1, UIMax = 5, ClampMin = 1))
	float ActiveAnalyzerWireScalarMin;

	/** Maximum height scalar of wire signal analyzers (ex. audio, triggers). */
	UPROPERTY(EditAnywhere, config, Category = GraphAnimation, meta = (EditCondition = "bAnimateConnections", UIMin = 1, UIMax = 5, ClampMin = 1))
	float ActiveAnalyzerWireScalarMax;

	/** Widget type to show on input nodes by default */
	UPROPERTY(EditAnywhere, config, Category = General)
	EMetasoundMemberDefaultWidget DefaultInputWidgetType = EMetasoundMemberDefaultWidget::RadialSlider;

	/** Determines which details view to show in Metasounds Editor */
	UPROPERTY(Transient)
	EMetasoundActiveDetailView DetailView = EMetasoundActiveDetailView::Metasound;
};
