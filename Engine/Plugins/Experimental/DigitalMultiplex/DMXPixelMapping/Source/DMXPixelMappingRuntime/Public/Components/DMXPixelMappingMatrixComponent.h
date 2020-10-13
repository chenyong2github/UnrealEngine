// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingExtraAttribute.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingMatrixComponent.generated.h"

class UTextureRenderTarget2D;
class UDMXLibrary;
class STextBlock;
enum class EDMXColorMode : uint8;

/**
 * DMX Matrix group component
 */
UCLASS()
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingMatrixComponent
	: public UDMXPixelMappingOutputComponent
{
	GENERATED_BODY()
public:
	/** Default Constructor */
	UDMXPixelMappingMatrixComponent();

	// ~Begin UObject interface
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	// ~End UObject interface
	
	/**  Logs properties that were changed in underlying fixture patch or fixture type  */
	void LogInvalidProperties();

public:
	// ~Begin UDMXPixelMappingBaseComponent interface
	virtual const FName& GetNamePrefix() override;
	virtual void ResetDMX() override;
	virtual void SendDMX() override;
	virtual void Render() override;
	virtual void RenderAndSendDMX() override;
	virtual void PostParentAssigned() override;

#if WITH_EDITOR
	virtual FString GetUserFriendlyName() const override;
#endif
	// ~End UDMXPixelMappingBaseComponent interface

	// ~Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const { return true; }
	// ~End FTickableGameObject interface

	// ~Begin UDMXPixelMappingOutputComponent interface
#if WITH_EDITOR
	virtual void RenderEditorPreviewTexture() override;
	virtual const FText GetPaletteCategory() override;
	virtual bool IsExposedToTemplate() { return true; }
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas) override;
	virtual void ToggleHighlightSelection(bool bIsSelected) override;

	virtual void UpdateWidget() override;
#endif // WITH_EDITOR

	virtual UTextureRenderTarget2D* GetOutputTexture() override;
	virtual FVector2D GetSize() const override;
	virtual FVector2D GetPosition() override;
	virtual void SetSize(const FVector2D& InSize) override;
	virtual void SetPosition(const FVector2D& InPosition) override;

#if WITH_EDITOR
	virtual void SetZOrder(int32 NewZOrder) override;
#endif // WITH_EDITOR
	// ~End UDMXPixelMappingOutputComponent interface


	/** Resize the target to max available size, it is driven by children components */
	void SetSizeWithinMaxBoundaryBox();

	void SetPositionBasedOnRelativePixel(UDMXPixelMappingMatrixCellComponent* InMatrixPixelComponent, FVector2D InDelta);

	void SetNumCells(const FIntPoint& InNumCells);

	void SetChildSizeAndPosition(UDMXPixelMappingMatrixCellComponent* InMatrixPixelComponent, const FIntPoint& InPixelCoordinate);

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
	/** Set size of the rendering texture and designer widget */
	void SetSizeInternal(const FVector2D& InSize);

	/** Resize rendering texture */
	void ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY);

	void SetPositionWithChildren();

public:
	UPROPERTY(EditAnywhere, Category = "Matrix Settings")
	FDMXEntityFixturePatchRef FixturePatchMatrixRef;

	/** Extra attributes for the whole Matrix Fixture */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings")
	TArray<FDMXPixelMappingExtraAttribute> ExtraAttributes;

	/** Extra attributes for each Matrix Fixture Cell */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Output Settings")
	TArray<FDMXPixelMappingExtraAttribute> ExtraCellAttributes;

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

	UPROPERTY()
	FIntPoint NumCells;

	UPROPERTY()
	FVector2D PixelSize;

	UPROPERTY()
	EDMXPixelMappingDistribution Distribution;

private:
#if WITH_EDITOR
	/** Updates num cells from the fixture patch ref */
	void UpdateNumCells();

	/** Maps attributes that exist in the patch to the attributes of the matrix. Clears those that don't exist. */
	void AutoMapAttributes();
#endif // WITH_EDITOR

	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;

	bool bIsUpdateWidgetRequested;

	TSharedPtr<STextBlock> PatchNameWidget;
#endif

	float PositionXCached;

	float PositionYCached;

private:
	static const FVector2D MinSize;
	static const FVector2D DefaultSize;

	FLinearColor PreviousEditorColor = FLinearColor::Blue;
};
