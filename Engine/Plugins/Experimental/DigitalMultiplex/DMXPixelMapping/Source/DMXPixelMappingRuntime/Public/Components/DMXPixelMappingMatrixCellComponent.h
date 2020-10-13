// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXEntityReference.h"

#include "DMXPixelMappingMatrixCellComponent.generated.h"

class SUniformGridPanel;
class UTextureRenderTarget2D;
class FProperty;
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
	virtual void PostLoad() override;

	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;

	//~ End UObject implementation
#endif // WITH_EDITOR

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

	void SetPositionFromParent(const FVector2D& InPosition);
	void SetSizeFromParent(const FVector2D& InSize);

	void SetPixelCoordinate(FIntPoint InPixelCoordinate) { CellCoordinate = InPixelCoordinate; }
	const FIntPoint& GetPixelCoordinate() { return CellCoordinate; }

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
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

private:
	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;

	UPROPERTY()
	FIntPoint CellCoordinate;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;
#endif

private:
	static const FVector2D MixPixelSize;
};
