// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingBaseComponent.h"

#if WITH_EDITOR
#include "Widgets/Layout/SConstraintCanvas.h"
#endif // WITH_EDITOR

#include "DMXPixelMappingOutputComponent.generated.h"

class UDMXEntityFixturePatch;
class SBox;


UENUM()
enum class EDMXPixelBlendingQuality : uint8
{
	/** 1 sample */
	Low,

    /** 5 samples ( 2 x 2 with center) */
    Medium,

	/** 9 samples ( 3 x 3 ) */
	High
};


/**
 * Base class for all Designer and configurable components
 */
UCLASS(Abstract)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingOutputComponent
	: public UDMXPixelMappingBaseComponent
{
	GENERATED_BODY()

	/*----------------------------------
		Types defenition
	----------------------------------*/
public:
	/** Default Constructor */
	UDMXPixelMappingOutputComponent();

	// ~Begin UObject Interface
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;

	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	// ~End UObject Interface

	/*----------------------------------
		UDMXPixelMappingOutputComponent Interface
	----------------------------------*/
#if WITH_EDITOR
	/** Whether component should be part of Palette view */
	virtual bool IsExposedToTemplate() { return false; }

	/** Returns the text of palette category*/
	virtual const FText GetPaletteCategory();

	/** Whether component should be visible in designer view */
	virtual bool IsVisibleInDesigner() const { return bVisibleInDesigner; }

	/** Rebuild widget for designer view */
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas);

	/** Change widget visuals whether it selected or not */
	virtual void ToggleHighlightSelection(bool bIsSelected) { bHighlighted = bIsSelected; };

	/** Update the content in designer widget */
	virtual void UpdateWidget() {}

	/** Whether component can be re-sized or re-position at the editor */
	virtual bool IsLockInDesigner() const { return bLockInDesigner; }

#endif // WITH_EDITOR

	/** Get rendering size of component */
	virtual FVector2D GetSize() const;

	/** Get rendering position of the component. Using for determining UV map input rendering offset */
	virtual FVector2D GetPosition() { return FVector2D(0.f); }

	/** Get pixel index in downsample texture */
	virtual int32 GetDownsamplePixelIndex() const { return 0; }

	/** Set rendering size of component */
	virtual void SetSize(const FVector2D& InSize);

	/** Get rendering component position */
	virtual void SetPosition(const FVector2D& InPosition);
	
	/** Queue rendering to downsample rendering target */
	virtual void QueueDownsample() {}

	/*----------------------------------
		Blueprint interface
	----------------------------------*/

	/*----------------------------------------------------------
		Non virtual functions, not intended to be overridden
	----------------------------------------------------------*/

	/** Queue rendering to downsample rendering target and send dmx */
	void QueueDownsampleAndSendDMX();

public:
#if WITH_EDITOR
	/** Get designer UI cached widget */
	TSharedPtr<SWidget> GetCachedWidget() const;

	/** Sets the ZOrder in the UI */
	virtual void SetZOrder(int32 NewZOrder);

	/** Returns the UI ZOrder */
	int32 GetZOrder() const { return ZOrder; }
#endif // WITH_EDITOR

protected:
#if WITH_EDITORONLY_DATA
	/** ZOrder in the UI */
	UPROPERTY()
	int32 ZOrder = 1;
#endif //WITH_EDITORONLY_DATA

protected:
	/** Helper function to get the correct word size of an attribute */
	uint8 GetNumChannelsOfAttribute(UDMXEntityFixturePatch* FixturePatch, const FName& AttributeName);
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", meta = (ClampMin = "1", UIMin = "1"))
	float SizeX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", meta = (ClampMin = "1", UIMin = "1"))
	float SizeY;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float PositionX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float PositionY;

    /** The quality level to use when averaging colors during downsampling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pixel Settings")
	EDMXPixelBlendingQuality CellBlendingQuality;

public:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Editor Settings")
	bool bLockInDesigner;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Editor Settings")
	bool bVisibleInDesigner;
#endif
	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

protected:
#if WITH_EDITORONLY_DATA
	/** A raw pointer to the slot to allow us to adjust the size, padding...etc. */
	SConstraintCanvas::FSlot* Slot;

	/** Cached designer widget */
	TSharedPtr<SBox> CachedWidget;

	/** Cached label box */
	TSharedPtr<SBox> CachedLabelBox;
#endif

public:
#if WITH_EDITOR
	const FLinearColor& GetEditorColor(bool bHighlight) const;
#endif

#if WITH_EDITORONLY_DATA
	/** The color displayed in editor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Editor Settings")
	FLinearColor EditorColor = FLinearColor::Blue;

	/** If true, the editor color is editable */
	bool bEditableEditorColor = false;

	/** If true is highlighted */
	bool bHighlighted = false;
#endif // WITH_EDITORONLY_DATA
};
