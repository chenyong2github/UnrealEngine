// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "EditorUndoClient.h"
#include "Toolkits/IToolkitHost.h"
#include "Interfaces/ITextureEditorToolkit.h"
#include "IDetailsView.h"
#include "TextureEditorSettings.h"

class STextBlock;
class STextureEditorViewport;
class UFactory;
class UTexture;

enum class ETextureChannelButton : uint8
{
	Red,
	Green,
	Blue,
	Alpha
};

/**
 * Implements an Editor toolkit for textures.
 */
class FTextureEditorToolkit
	: public ITextureEditorToolkit
	, public FEditorUndoClient
	, public FGCObject
{
public:
	FTextureEditorToolkit();

	/**
	 * Destructor.
	 */
	virtual ~FTextureEditorToolkit( );

public:

	/**
	 * Edits the specified Texture object.
	 *
	 * @param Mode The tool kit mode.
	 * @param InitToolkitHost 
	 * @param ObjectToEdit The texture object to edit.
	 */
	void InitTextureEditor( const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit );

public:
	// FAssetEditorToolkit interface
	virtual FString GetDocumentationLink( ) const override;
	virtual void RegisterTabSpawners( const TSharedRef<class FTabManager>& TabManager ) override;
	virtual void UnregisterTabSpawners( const TSharedRef<class FTabManager>& TabManager ) override;
	virtual bool UsesCustomToolbarPlacement() const { return false; }

	// ITextureEditorToolkit interface
	virtual void CalculateTextureDimensions( uint32& Width, uint32& Height, uint32& Depth, uint32& ArraySize ) const override;
	virtual ESimpleElementBlendMode GetColourChannelBlendMode( ) const override;
	virtual int32 GetMipLevel( ) const override;
	virtual int32 GetLayer() const override;
	virtual UTexture* GetTexture( ) const override;
	virtual bool HasValidTextureResource( ) const override;
	virtual bool GetUseSpecifiedMip( ) const override;
	virtual double GetCustomZoomLevel( ) const override;
	virtual void SetCustomZoomLevel(double ZoomValue) override;
	virtual void PopulateQuickInfo( ) override;
	virtual void SetZoomMode( const ETextureEditorZoomMode ZoomMode ) override;
	virtual ETextureEditorZoomMode GetZoomMode() const override;
	virtual double CalculateDisplayedZoomLevel() const override;
	virtual void OffsetZoom( double OffsetValue, bool bSnapToStepSize = true );
	virtual void ZoomIn( ) override;
	virtual void ZoomOut( ) override;
	virtual float GetVolumeOpacity( ) const override;
	virtual void SetVolumeOpacity( float VolumeOpacity ) override;
	virtual const FRotator& GetVolumeOrientation( ) const override;
	virtual void SetVolumeOrientation( const FRotator& InOrientation ) override;
	virtual int32 GetExposureBias() const override
	{
		return ExposureBias;
	}


	// IToolkit interface
	virtual FText GetBaseToolkitName( ) const override;
	virtual FName GetToolkitFName( ) const override;
	virtual FLinearColor GetWorldCentricTabColorScale( ) const override;
	virtual FString GetWorldCentricTabPrefix( ) const override;

	// FGCObject interface
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	
protected:
	// FEditorUndoClient interface
	virtual void PostUndo( bool bSuccess ) override;
	virtual void PostRedo( bool bSuccess ) override;

	/**
	 * Binds the UI commands to delegates.
	 */
	void BindCommands( );

	/**
	 * Creates the texture properties details widget.
	 *
	 * @return The widget.
	 */
	TSharedRef<SWidget> BuildTexturePropertiesWidget( );

	/**
	 * Creates all internal widgets for the tabs to point at.
	 */
	void CreateInternalWidgets( );

	/**
	 * Builds the toolbar widget for the Texture editor.
	 */
	void ExtendToolBar( );

	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	/**
	 * Gets the highest mip map level that this texture supports.
	 *
	 * @return Mip map level.
	 */
	TOptional<int32> GetMaxMipLevel( ) const;

	TOptional<int32> GetMaxLayer() const;

	/**
	 * Checks whether the texture being edited is a cube map texture.
	 */
	bool IsCubeTexture( ) const;

	/**
	 * Checks whether the texture being edited is a volume texture.
	 */
	bool IsVolumeTexture() const;

	/**
	 * Checks whether the texture being edited is a texture 2D array.
	 */
	bool Is2DArrayTexture() const;

	TSharedRef<SWidget> OnGenerateMipMapLevelMenu();
	TSharedRef<SWidget> OnGenerateSettingsMenu();
private:
	bool IsAlphaChannelButtonEnabled() const;
	FSlateColor GetChannelButtonBackgroundColor(ETextureChannelButton Button) const;
	FSlateColor GetChannelButtonForegroundColor(ETextureChannelButton Button) const;
	void OnChannelButtonCheckStateChanged(ETextureChannelButton Button);
	ECheckBoxState OnGetChannelButtonCheckState(ETextureChannelButton Button) const;

	// Callback for toggling the Checkered Background action.
	void HandleCheckeredBackgroundActionExecute( ETextureEditorBackgrounds Background );

	// Callback for getting the checked state of the Checkered Background action.
	bool HandleCheckeredBackgroundActionIsChecked( ETextureEditorBackgrounds Background );

	// Callback for toggling the volume display action.
	void HandleVolumeViewModeActionExecute( ETextureEditorVolumeViewMode InViewMode );

	// Callback for getting the checked state of the volume display action.
	bool HandleVolumeViewModeActionIsChecked( ETextureEditorVolumeViewMode InViewMode );

	// Callback for toggling the Compress Now action.
	void HandleCompressNowActionExecute( );

	// Callback for getting the checked state of the Compress Now action.
	bool HandleCompressNowActionCanExecute( ) const;

	// Callback for executing the Fit To Viewport action.
	void HandleFitToViewportActionExecute( );

	// Callback for executing the Fill To Viewport action.
	void HandleFillToViewportActionExecute( );

	virtual bool IsFitToViewport() const;
	virtual bool IsFillToViewport() const;
	
	// Callback for executing the Fit To 100% action.
	void HandleZoomToNaturalActionExecute( );

	// Callback for changing the checked state of the MipMap check box.
	void HandleMipLevelCheckBoxCheckedStateChanged( ECheckBoxState InNewState );

	// Callback for getting the checked state of the MipMap check box.
	ECheckBoxState HandleMipLevelCheckBoxIsChecked( ) const;

	// Callback for determining whether the MipMap check box is enabled.
	bool HandleMipLevelCheckBoxIsEnabled( ) const;

	// Callback for changing the value of the mip map level entry box.
	void HandleMipLevelChanged( int32 NewMipLevel );

	// Callback for getting the value of the mip map level entry box.
	TOptional<int32> HandleMipLevelEntryBoxValue( ) const;

	FReply HandleMipMapMinusButtonClicked();

	FReply HandleMipMapPlusButtonClicked();

	void HandleLayerEntryBoxChanged(int32 NewMipLevel);
	TOptional<int32> HandleLayerEntryBoxValue() const;

	bool HasLayers() const;

	// Callback for determining whether the Reimport action can execute.
	bool HandleReimportActionCanExecute( ) const;

	// Callback for executing the Reimport action.
	void HandleReimportActionExecute( );

	// Callback that is executed after the reimport manager reimported an asset.
	void HandleReimportManagerPostReimport( UObject* InObject, bool bSuccess );
	
	// Callback that is executed before the reimport manager reimported an asset.
	void HandleReimportManagerPreReimport( UObject* InObject );

	// Callback that is executed once an asset is imported
	void HandleAssetPostImport(UFactory* InFactory, UObject* InObject);

	// Callback for toggling the Desaturation channel action.
	void HandleDesaturationChannelActionExecute( );

	// Callback for getting the checked state of the Desaturation channel action.
	bool HandleDesaturationChannelActionIsChecked( ) const;

	// Callback for determining whether the Settings action can execute.
	void HandleSettingsActionExecute( );

	// Callback for spawning the Properties tab.
	TSharedRef<SDockTab> HandleTabSpawnerSpawnProperties( const FSpawnTabArgs& Args );

	// Callback for spawning the Viewport tab.
	TSharedRef<SDockTab> HandleTabSpawnerSpawnViewport( const FSpawnTabArgs& Args );

	// Callback for toggling the Texture Border action.
	void HandleTextureBorderActionExecute( );

	// Callback for getting the checked state of the Texture Border action.
	bool HandleTextureBorderActionIsChecked( ) const;

	// Callback for getting the visibility of the exposure bias widget.
	EVisibility HandleExposureBiasWidgetVisibility() const;

	// Callback for getting the exposure bias.
	TOptional<int32> HandleExposureBiasBoxValue() const;

	// Callback for changing the exposure bias.
	void HandleExposureBiasBoxValueChanged(int32 NewExposure);

	// Callback for changes in the zoom slider.
	void HandleOpacitySliderChanged(float NewValue);

	// Callback for getting the zoom slider's value.
	TOptional<float> HandleOpacitySliderValue() const;

	// Callback for clicking an item in the 'Zoom' menu.
	void HandleZoomMenuEntryClicked(double ZoomValue);

	// Callback for clicking the 'Fill' item in the 'Zoom' menu.
	void HandleZoomMenuFillClicked();

	// Callback for setting the checked state of the 'Fill' item in the 'Zoom' menu.
	bool IsZoomMenuFillChecked() const;

	// Callback for clicking the 'Fit' item in the 'Zoom' menu.
	void HandleZoomMenuFitClicked();

	// Callback for setting the checked state of the 'Fit' item in the 'Zoom' menu.
	bool IsZoomMenuFitChecked() const;

	// Callback for getting the zoom percentage text.
	FText HandleZoomPercentageText() const;

	// Callback for changes in the zoom slider.
	void HandleZoomSliderChanged(float NewValue);

	// Callback for getting the zoom slider's value.
	float HandleZoomSliderValue() const;

	// Callback for clicking the View Options menu button.
	FReply HandleViewOptionsMenuButtonClicked();

	TSharedRef<SWidget> MakeChannelControlWidget();
	TSharedRef<SWidget> MakeLODControlWidget();
	TSharedRef<SWidget> MakeLayerControlWidget();
	TSharedRef<SWidget> MakeExposureContolWidget();
	TSharedRef<SWidget> MakeOpacityControlWidget();
	TSharedRef<SWidget> MakeZoomControlWidget();
private:

	/** The Texture asset being inspected */
	UTexture* Texture;

	/** Viewport */
	TSharedPtr<STextureEditorViewport> TextureViewport;

	/** Properties tab */
	TSharedPtr<SVerticalBox> TextureProperties;

	/** Properties tree view */
	TSharedPtr<class IDetailsView> TexturePropertiesWidget;

	/** Quick info text blocks */
	TSharedPtr<STextBlock> ImportedText;
	TSharedPtr<STextBlock> CurrentText;
	TSharedPtr<STextBlock> MaxInGameText;
	TSharedPtr<STextBlock> SizeText;
	TSharedPtr<STextBlock> MethodText;
	TSharedPtr<STextBlock> FormatText;
	TSharedPtr<STextBlock> LODBiasText;
	TSharedPtr<STextBlock> HasAlphaChannelText;
	TSharedPtr<STextBlock> NumMipsText;
	TSharedPtr<STextBlock> MipLevelTextBlock;

	// Holds the anchor for the view options menu.
	TSharedPtr<SMenuAnchor> ViewOptionsMenuAnchor;

	/** If true, displays the red channel */
	bool bIsRedChannel;

	/** If true, displays the green channel */
	bool bIsGreenChannel;

	/** If true, displays the blue channel */
	bool bIsBlueChannel;

	/** If true, displays the alpha channel */
	bool bIsAlphaChannel;

	/** If true, desaturates the texture */
	bool bIsDesaturation;

	/** The maximum width/height at which the texture will render in the preview window */
	uint32 PreviewEffectiveTextureWidth;
	uint32 PreviewEffectiveTextureHeight;

	/** Which mip level should be shown */
	int32 SpecifiedMipLevel;
	/* When true, the specified mip value is used. Top mip is used when false.*/
	bool bUseSpecifiedMipLevel;

	int32 SpecifiedLayer;

	/** During re-import, cache this setting so it can be restored if necessary */
	bool SavedCompressionSetting;

	/** The texture's zoom factor. */
	double Zoom;

	// Which exposure level should be used, in FStop e.g. 0:original, -1:half as bright, 1:2x as bright, 2:4x as bright.
	int32 ExposureBias;

	/** This toolkit's current zoom mode **/
	ETextureEditorZoomMode ZoomMode;

	// For volume texture, defines an opacity to see through the volume when tracing.
	float VolumeOpacity;

	// For volume texture, the orientation when tracing.
	FRotator VolumeOrientation;

	bool bIsVolumeTexture;
private:

	// The name of the Viewport tab.
	static const FName ViewportTabId;

	// The name of the Properties tab.
	static const FName PropertiesTabId;
};
