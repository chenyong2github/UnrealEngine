// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXPixelMappingScreenComponent.generated.h"

class UTextureRenderTarget2D;
class SDMXPixelMappingScreenLayout;
enum class EDMXCellFormat : uint8;

/**
 * DMX Screen(Grid) rendering component
 */
UCLASS()
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingScreenComponent
	: public UDMXPixelMappingOutputDMXComponent
{
	GENERATED_BODY()
public:
	/** Default Constructor */
	UDMXPixelMappingScreenComponent();

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

	//~ Begin FTickableGameObject begin
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const { return true; }
	//~ End FTickableGameObject end

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual bool IsExposedToTemplate() { return true; }
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

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
#if WITH_EDITOR
	/** Constract the screen grid widget */
	TSharedRef<SWidget> ConstructGrid();

#endif // WITH_EDITOR

	/** Set size of the rendering texture and designer widget */
	void SetSizeInternal(const FVector2D& InSize);

	/** Resize rendering texture */
	void ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY);

	/** Prepare the final color to send */
	void AddColorToSendBuffer(const FColor& Color, TArray<uint8>& OutDMXSendBuffer);

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping Settings", meta = (DisplayName = "X Cells", ClampMin = "1", ClampMax = "1000", UIMin = "1", UIMax = "1000", DisplayPriority = "1"))
	int32 NumXCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping Settings", meta = (DisplayName = "Y Cells", ClampMin = "1", ClampMax = "1000", UIMin = "1", UIMax = "1000", DisplayPriority = "1"))
	int32 NumYCells;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mapping Settings")
	FDMXProtocolName ProtocolName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (ClampMin = "1", ClampMax = "100000", UIMin = "1", UIMax = "100000", DisplayPriority = "1"))
	int32 RemoteUniverse;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (ClampMin = "1", ClampMax = "512", UIMin = "1", UIMax = "512", DisplayPriority = "1"))
	int32 StartAddress;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (DisplayName = "Color Space", DisplayPriority = "1"))
	EDMXCellFormat PixelFormat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (DisplayPriority = "1"))
	EDMXPixelMappingDistribution Distribution;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (DisplayPriority = "1"))
	bool bIgnoreAlphaChannel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (ClampMin = "0", ClampMax = "255", UIMin = "0", UIMax = "255", DisplayPriority = "2"))
	float PixelIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (ClampMin = "0", ClampMax = "255", UIMin = "0", UIMax = "255", DisplayPriority = "2"))
	float AlphaIntensity;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (DisplayPriority = "3"))
	bool bShowAddresses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Patch Settings", meta = (DisplayPriority = "3"))
	bool bShowUniverse;
#endif

private:
	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;

#if WITH_EDITORONLY_DATA
	FSlateBrush Brush;

	bool bIsUpdateWidgetRequested;
#endif

private:
	static const FVector2D MixGridSize;

#if WITH_EDITORONLY_DATA
	static const uint32 MaxGridUICells;
#endif
};
