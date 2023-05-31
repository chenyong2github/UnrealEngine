// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingPreprocessRenderer.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "IDMXPixelMappingRenderer.h"
#include "Library/DMXEntityReference.h"
#include "Templates/SubclassOf.h"

#include "DMXPixelMappingRendererComponent.generated.h"

enum class EDMXPixelMappingRendererType : uint8;
class UDMXPixelMappingLayoutScript;
class UDMXPixelMappingPreprocessRenderer;

class UMaterialInterface;
class UTexture;
class UUserWidget;
class UTextureRenderTarget2D;
class UWorld;


/** 
 * Component for rendering input texture.  
 */
UCLASS(BlueprintType)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingRendererComponent
	: public UDMXPixelMappingOutputComponent
{
	GENERATED_BODY()

public:
	/** Default Constructor */
	UDMXPixelMappingRendererComponent();

	//~ Begin UObject implementation
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
#endif // WITH_EDITOR
	//~ End UObject implementation

	//~ Begin UDMXPixelMappingBaseComponent implementation
	virtual const FName& GetNamePrefix() override;
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;
	virtual void ResetDMX() override;
	virtual void SendDMX() override;
	virtual void Render() final;
	virtual void RenderAndSendDMX() final;
	//~ End UDMXPixelMappingBaseComponent implementation

#if WITH_EDITOR
	/** Render all downsample pixel for editor preview texture */
	void RenderEditorPreviewTexture();
	
	/** Get target for preview, create new one if does not exists. */
	UTextureRenderTarget2D* GetPreviewRenderTarget();
#endif

	/** Gets the rendered input texture, or nullptr if no input texture is currently rendered */
	UTexture* GetRenderedInputTexture() const;

	/**
	 * Get pixel position in downsample buffer target based on pixel index
	 *
	 * @param InIndex Index of the pixel in buffer texture.
	 * @return FIntPoint X and Y position of the pixel in texture
	 */
	FIntPoint GetPixelPosition(int32 InIndex) const;

	/** Get active world. It could be editor or build world */
	UWorld* GetWorld() const;

	/** Get renderer interface */
	const TSharedPtr<IDMXPixelMappingRenderer>& GetRenderer() { return PixelMappingRenderer; }

#if WITH_EDITOR
	/**
	 * Take of container widget which is holds widget for all child components.
	 */
	UE_DEPRECATED(5.1, "Pixel Mapping Components no longer hold their own widget, in an effort to separate Views from Data.")
	TSharedRef<SWidget> TakeWidget();
#endif // WITH_EDITOR

	/*----------------------------------
		Blueprint interface
	----------------------------------*/

	/** Render input texture for downsampling */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	void RendererInputTexture();

	/** Create or update size of buffer target for rendering downsample pixels */
	void CreateOrUpdateDownsampleBufferTarget();

	/** 
	 * Add pixel params for downsampling set
	 *
	 * @param InDownsamplePixelParam pixel rendering params
	 */
	void AddPixelToDownsampleSet(FDMXPixelMappingDownsamplePixelParamsV2&& InDownsamplePixelParam);

	/** Get amount of downsample pixels */
	int32 GetDownsamplePixelNum();

	/**
	 * Pass the downsample CPU buffer from Render Thread to Game Thread and store 
	 * 
	 * @param InDownsampleBuffer CPU buffer
	 * @param InRect buffer X and Y dimension
	 */
	void SetDownsampleBuffer(TArray<FLinearColor>&& InDownsampleBuffer, FIntRect InRect);

	/** Get Pixel color by given downsample pixel index. Returns false if no color value could be acquired */
	bool GetDownsampleBufferPixel(const int32 InDownsamplePixelIndex, FLinearColor& OutLinearColor);

	/** Get Pixels color by given downsample pixel range. Returns false if no color values could be acquired */
	bool GetDownsampleBufferPixels(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd, TArray<FLinearColor>& OutLinearColors);

	/** Reset the color by given downsample pixel index */
	bool ResetColorDownsampleBufferPixel(const int32 InDownsamplePixelIndex);

	/** Reset the color by given downsample pixel range */
	bool ResetColorDownsampleBufferPixels(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd);

	/** Remove all pixels from DownsampleBuffer */
	void EmptyDownsampleBuffer();

private:
	/** Resize output texture for editor preview */
	void ResizePreviewRenderTarget(uint32 InSizeX, uint32 InSizeY);

	/** Initialize all textures and creation or loading asset */
	void Initialize();

	/** Create a render target with unique name */
	UTextureRenderTarget2D* CreateRenderTarget(const FName& InBaseName);

public:
	/**
	 * Returns the Modulators of the component corresponding to the patch specified. 
	 * Note, this node does a lookup on all fixture patches in use, hence may be slow and shouldn't be called on tick. 
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetPixelMappingComponentModulators(FDMXEntityFixturePatchRef FixturePatchRef, TArray<UDMXModulator*>& DMXModulators);

	/** Returns the preprocess renderer */
	UDMXPixelMappingPreprocessRenderer* GetPreprocessRenderer() { return PreprocessRenderer; }

#if WITH_EDITOR
	/** Returns the component canvas used for this widget */
	FORCEINLINE TSharedPtr<SConstraintCanvas> GetComponentsCanvas() const { return ComponentsCanvas; }
#endif // WITH_EDITOR

	/** Type of rendering, Texture, Material, UMG, etc... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	EDMXPixelMappingRendererType RendererType;

	/** The texture used for pixel mapping */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	TObjectPtr<UTexture> InputTexture;

	/** The material used for pixel mapping */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings", Meta = (DisplayName = "User Interface Material"))
	TObjectPtr<UMaterialInterface> InputMaterial;

	/** The UMG widget used for pixel mapping */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	TSubclassOf<UUserWidget> InputWidget;

	/** The brightness of the renderer */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings", Meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Brightness = 1.f;

	/** Layout script for the children of this component (hidden in customizations and displayed in its own panel). */
	UPROPERTY(EditAnywhere, Instanced, Category = "Layout")
	TObjectPtr<UDMXPixelMappingLayoutScript> LayoutScript;

private:
	/** Retrieve total count of all output targets that support shared rendering and updates a counter. O(n) */
	int32 GetTotalDownsamplePixelCount();

	/** Helper function checks the downsample pixel range */
	bool IsPixelRangeValid(const int32 InDownsamplePixelIndexStart, const int32 InDownsamplePixelIndexEnd) const;

	UPROPERTY(VisibleAnywhere, Instanced, Category = "Filtering")
	TObjectPtr<UDMXPixelMappingPreprocessRenderer> PreprocessRenderer;

	/** Reference to renderer */
	TSharedPtr<IDMXPixelMappingRenderer> PixelMappingRenderer;

	/** UMG widget for downsampling */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> UserWidget;

#if WITH_EDITORONLY_DATA
	/** Canvas for all UI downsamping component widgets */
	TSharedPtr<SConstraintCanvas> ComponentsCanvas;
#endif // WITH_EDITORONLY_DATA

	/** GPU downsample pixel buffer target texture */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UTextureRenderTarget2D> DownsampleBufferTarget;

#if WITH_EDITORONLY_DATA
	/** Editor preview output target */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UTextureRenderTarget2D> PreviewRenderTarget;
#endif // WITH_EDITORONLY_DATA

	/** CPU downsample pixel buffer */
	TArray<FLinearColor> DownsampleBuffer;

	/** Counter for all pixels from child components */
	int32 DownsamplePixelCount = 0;

	/** Critical section for set, update and get color array */
	mutable FCriticalSection DownsampleBufferCS;

	/** Hold the params of the pixels for downsamle rendering */
	TArray<FDMXPixelMappingDownsamplePixelParamsV2> DownsamplePixelParams;

	/** True once the first render pass completed */
	bool bWasEverRendered = false;

	/** Initial texture color */
	static const FLinearColor ClearTextureColor;

public:
	/** Max downsample target size */
	static const FIntPoint MaxDownsampleBufferTargetSize;
};
