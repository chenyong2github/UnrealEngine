// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "DMXAttribute.h"
#include "Library/DMXEntityReference.h"

#include "DMXPixelMappingMatrixCellComponent.generated.h"


class SUniformGridPanel;
class UTextureRenderTarget2D;
enum class EDMXColorMode : uint8;

/**
 * Matrix pixel component
 */
UCLASS()
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingMatrixCellComponent
	: public UDMXPixelMappingOutputDMXComponent
{
	GENERATED_BODY()

public:
	/** Default Constructor */
	UDMXPixelMappingMatrixCellComponent();

	//~ Begin UObject implementation
	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;

	//~ End UObject implementation
#endif // WITH_EDITOR

	//~ Begin UDMXPixelMappingBaseComponent implementation
	virtual const FName& GetNamePrefix() override;
	virtual void ResetDMX() override;
	virtual void SendDMX() override;
	virtual void PostParentAssigned() override;

#if WITH_EDITOR
	virtual FString GetUserFriendlyName() const override;
#endif
	//~ End UDMXPixelMappingBaseComponent implementation

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas) override;
	virtual void ToggleHighlightSelection(bool bIsSelected) override;

	virtual void UpdateWidget() override;
#endif // WITH_EDITOR	

	virtual FVector2D GetSize() const override;

	virtual FVector2D GetPosition() override;
	virtual int32 GetDownsamplePixelIndex() const override { return DownsamplePixelIndex; }
	virtual void SetPosition(const FVector2D& InPosition) override;
	virtual void SetSize(const FVector2D& InSize) override;

	virtual void QueueDownsample() override;
	//~ End UDMXPixelMappingOutputComponent implementation

	//~ Begin UDMXPixelMappingOutputDMXComponent implementation
	virtual void RenderWithInputAndSendDMX() override;
	//~ End UDMXPixelMappingOutputDMXComponent implementation

	void SetPositionFromParent(const FVector2D& InPosition);
	void SetSizeFromParent(const FVector2D& InSize);

	void SetPixelCoordinate(FIntPoint InPixelCoordinate);
	const FIntPoint& GetPixelCoordinate() { return CellCoordinate; }

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
	/** Helper that returns the renderer component this component belongs to */
	UDMXPixelMappingRendererComponent* GetRendererComponent() const;

	void SetPositionInBoundaryBox(const FVector2D& InPosition);

	void SetSizeWithinBoundaryBox(const FVector2D& InSize);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pixel Settings")
	int32 CellID;

	UPROPERTY()
	FDMXEntityFixturePatchRef FixturePatchMatrixRef;

#if WITH_EDITORONLY_DATA
	/** The X position relative to the parent group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float RelativePositionX;

	/** The Y position relative to the parent group */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings")
	float RelativePositionY;
#endif // WITH_EDITORONLY_DATA

	/** Matrix cell R offset */
	TOptional<uint8> ByteOffsetR;

	/** Matrix cell R offset */
	TOptional<uint8> ByteOffsetG;

	/** Matrix cell R offset */
	TOptional<uint8> ByteOffsetB;

	/** Matrix cell R offset */
	TOptional<uint8> ByteOffsetM;

private:
	UPROPERTY()
	FIntPoint CellCoordinate;

	/** Index of the cell pixel in downsample target buffer */
	int32 DownsamplePixelIndex;

	/** Store binding of attribute and DMX channel */
	TMap<FDMXAttributeName, int32> AttributeNameChannelMap;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;
#endif

private:
	static const FVector2D MixPixelSize;
};
