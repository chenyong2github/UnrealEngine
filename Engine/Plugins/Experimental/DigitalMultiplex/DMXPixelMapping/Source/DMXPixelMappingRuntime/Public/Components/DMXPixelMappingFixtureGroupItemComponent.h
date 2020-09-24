// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingExtraAttribute.h"
#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Library/DMXEntityReference.h"
#include "DMXPixelMappingFixtureGroupItemComponent.generated.h"

class UTextureRenderTarget2D;
enum class EDMXColorMode : uint8;

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

	//~ Begin UObject implementation
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	//~ End UObject implementation

	//~ Begin UDMXPixelMappingBaseComponent implementation
	virtual const FName& GetNamePrefix() override;
	virtual void ResetDMX() override;
	virtual void SendDMX() override;
	virtual void Render() override;
	virtual void RenderAndSendDMX() override;
	virtual void PostParentAssigned() override;
	//~ End UDMXPixelMappingBaseComponent implementation

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas) override;
	virtual void ToggleHighlightSelection(bool bIsSelected) override;
	virtual bool IsVisibleInDesigner() const override;
	virtual void UpdateWidget() override;
#endif // WITH_EDITOR	
	virtual UTextureRenderTarget2D* GetOutputTexture() override;
	virtual FVector2D GetSize() override;
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

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Selected Patch")
	FDMXEntityFixturePatchRef FixturePatchRef;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Setting")
	TArray<FDMXPixelMappingExtraAttribute> ExtraAttributes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Setting")
	EDMXColorMode ColorMode;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "R"))
	bool AttributeRExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "G"))
	bool AttributeGExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "B"))
	bool AttributeBExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Expose"))
	bool MonochromeExpose;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert R"))
	bool AttributeRInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert G"))
	bool AttributeGInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert B"))
	bool AttributeBInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Invert"))
	bool MonochromeInvert;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "R Attribute"))
	FDMXAttributeName AttributeR;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "G Attribute"))
	FDMXAttributeName AttributeG;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "B Attribute"))
	FDMXAttributeName AttributeB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings", meta = (DisplayName = "Intensity Attribute"))
	FDMXAttributeName MonochromeIntensity;

private:
	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;
#endif

private:
	static const FVector2D MixPixelSize;

protected:
	bool CheckForDuplicateFixturePatch(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent, FDMXEntityFixturePatchRef InFixturePatchRef);
};
