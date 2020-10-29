// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputComponent.h"
#include "Templates/SubclassOf.h"
#include "DMXPixelMappingRendererComponent.generated.h"

class UMaterialInterface;
class UTexture;
class UUserWidget;
class IDMXPixelMappingRenderer;
class UTextureRenderTarget2D;
class UWorld;

enum class EMapChangeType : uint8;

enum class EDMXPixelMappingRendererType : uint8;

/**
 * Component for rendering input texture
 */
UCLASS(BlueprintType, Blueprintable)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingRendererComponent
	: public UDMXPixelMappingOutputComponent
{
	GENERATED_BODY()

public:
	/** Default Constructor */
	UDMXPixelMappingRendererComponent();

	~UDMXPixelMappingRendererComponent();

	//~ Begin UObject implementation
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

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
	//~ End UDMXPixelMappingBaseComponent implementation

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	//~ Output Texutre is only for preivew and should be controlled in editor only
	virtual UTextureRenderTarget2D* GetOutputTexture() override;
	virtual FVector2D GetSize() const override;

	virtual void RenderEditorPreviewTexture() override;
#endif
	//~ End UDMXPixelMappingOutputComponent implementation

	/** Get reference to the active input texture */
	UTexture* GetRendererInputTexture() const;

	/** Get renderer interfece */
	const TSharedPtr<IDMXPixelMappingRenderer>& GetRenderer() { return PixelMappingRenderer; }

	/** Get active world. It could be editor or build world */
	UWorld* GetWorld() const;

#if WITH_EDITOR
	/**
	 * Take of container widget which is holds widget for all child components.
	 */
	TSharedRef<SWidget> TakeWidget();
#endif // WITH_EDITOR

	/*----------------------------------
		Blueprint interface
	----------------------------------*/

	/** Render input texture for downsampling */
	UFUNCTION(BlueprintCallable, Category = "DMX|PixelMapping")
	void RendererInputTexture();

private:
	void ResizeMaterialRenderTarget(uint32 InSizeX, uint32 InSizeY);

	/** Generate new input widget based on UMG */
	void UpdateInputWidget(TSubclassOf<UUserWidget> InInputWidget);

#if WITH_EDITOR
	/** Resize output texture for editor preview */
	void ResizeOutputTarget(uint32 InSizeX, uint32 InSizeY);

	void OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType);
#endif

	/** Initialize all textures and creation or loading asset */
	void Initialize();
public:
	/** Type of rendering, Texture, Material, UMG, etc... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	EDMXPixelMappingRendererType RendererType;

	/** Texture to Downsampling */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	UTexture* InputTexture;

	/** Material to Downsampling */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings", meta = (DisplayName = "User Interface Material"))
	UMaterialInterface* InputMaterial;

	/** UMG to Downsampling */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings")
	TSubclassOf<UUserWidget> InputWidget;
	
	/** Master brightness of the renderer */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Settings", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Brightness;

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
#if WITH_EDITORONLY_DATA
	/** Editor preview output target */
	UPROPERTY(Transient)
	UTextureRenderTarget2D* OutputTarget;
#endif

	/** Material of UMG texture to downsample */
	UPROPERTY(Transient)
	UTextureRenderTarget2D* InputRenderTarget;

	/** Reference to renderer */
	TSharedPtr<IDMXPixelMappingRenderer> PixelMappingRenderer;

	/** UMG widget for downsampling */
	UPROPERTY(Transient)
	UUserWidget* UserWidget;

#if WITH_EDITORONLY_DATA
	/** Canvas for all UI downsamping component witgets */
	TSharedPtr<SConstraintCanvas> ComponentsCanvas;

	FDelegateHandle OnChangeLevelHandle;
#endif
};