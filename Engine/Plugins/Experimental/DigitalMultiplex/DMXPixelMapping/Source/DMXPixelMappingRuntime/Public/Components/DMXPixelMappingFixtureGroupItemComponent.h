// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingExtraAttribute.h"

#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Library/DMXEntityReference.h"

#include "Misc/Attribute.h"

#include "DMXPixelMappingFixtureGroupItemComponent.generated.h"

class UTextureRenderTarget2D;
enum class EDMXColorMode : uint8;

class STextBlock;

/**
 * Fixture Item pixel component
 */
UCLASS()
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingFixtureGroupItemComponent
	: public UDMXPixelMappingOutputDMXComponent
{
	GENERATED_BODY()
public:
	/** Default Constructor */
	UDMXPixelMappingFixtureGroupItemComponent();

protected:
	//~ Begin UObject implementation
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	//~ End UObject implementation

public:
	//~ Begin UDMXPixelMappingBaseComponent implementation
	virtual const FName& GetNamePrefix() override;
	virtual void ResetDMX() override;
	virtual void SendDMX() override;
	virtual void Render() override;
	virtual void RenderAndSendDMX() override;
	virtual void PostParentAssigned() override;

#if WITH_EDITOR
	virtual FString GetUserFriendlyName() const override;
#endif
	//~ End UDMXPixelMappingBaseComponent implementation

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas) override;
	virtual void ToggleHighlightSelection(bool bIsSelected) override;
	virtual bool IsVisibleInDesigner() const override;

	virtual void UpdateWidget() override;
#endif // WITH_EDITOR	

	virtual UTextureRenderTarget2D* GetOutputTexture() override;
	virtual FVector2D GetSize() const override;
	virtual FVector2D GetPosition() override;
	virtual void SetPosition(const FVector2D& InPosition) override;
	virtual void SetSize(const FVector2D& InSize) override;
	//~ End UDMXPixelMappingOutputComponent implementation

	//~ Begin UDMXPixelMappingOutputDMXComponent implementation
	virtual void RenderWithInputAndSendDMX() override;
	virtual void RendererOutputTexture() override;
	//~ End UDMXPixelMappingOutputDMXComponent implementation

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

	void SetPositionFromParent(const FVector2D& InPosition);

private:
	/** Set position of Fixture Pixel inside Fixture Group Boundary Box */
	void SetPositionInBoundaryBox(const FVector2D& InPosition);

	/** Set size of Fixture Pixel inside Fixture Group Boundary Box */
	void SetSizeWithinBoundaryBox(const FVector2D& InSize);

#if WITH_EDITOR
	FMargin GetLabelPadding() const;
#endif

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selected Patch")
	FDMXEntityFixturePatchRef FixturePatchRef;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings")
	EDMXColorMode ColorMode;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "R"))
	bool AttributeRExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "G"))
	bool AttributeGExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "B"))
	bool AttributeBExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Expose"))
	bool bMonochromeExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert R"))
	bool AttributeRInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert G"))
	bool AttributeGInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert B"))
	bool AttributeBInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert"))
	bool bMonochromeInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "R Attribute"))
	FDMXAttributeName AttributeR;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "G Attribute"))
	FDMXAttributeName AttributeG;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "B Attribute"))
	FDMXAttributeName AttributeB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Intensity Attribute"))
	FDMXAttributeName MonochromeIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayAfter = "MonochromeIntensity"))
	TArray<FDMXPixelMappingExtraAttribute> ExtraAttributes;

#if WITH_EDITORONLY_DATA
	/** The X position relative to the parent group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float RelativePositionX;

	/** The Y position relative to the parent group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float RelativePositionY;
#endif // WITH_EDITORONLY_DATA

private:
#if WITH_EDITOR
	/** Maps attributes that exist in the patch to the attributes of the group items. Clears those that don't exist. */
	void AutoMapAttributes();
#endif // WITH_EDITOR

	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;

	TSharedPtr<STextBlock> PatchNameWidget;
#endif // WITH_EDITORONLY_DATA

private:
	static const FVector2D MixPixelSize;

protected:
	bool CheckForDuplicateFixturePatch(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent, FDMXEntityFixturePatchRef InFixturePatchRef);
};
