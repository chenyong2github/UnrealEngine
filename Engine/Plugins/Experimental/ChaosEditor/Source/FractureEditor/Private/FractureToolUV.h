// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolUV.generated.h"

class FFractureToolContext;

UENUM()
enum class EAutoUVTextureResolution : int32
{
	Resolution16 = 16 UMETA(DisplayName = "16 x 16"),
	Resolution32 = 32 UMETA(DisplayName = "32 x 32"),
	Resolution64 = 64 UMETA(DisplayName = "64 x 64"),
	Resolution128 = 128 UMETA(DisplayName = "128 x 128"),
	Resolution256 = 256 UMETA(DisplayName = "256 x 256"),
	Resolution512 = 512 UMETA(DisplayName = "512 x 512"),
	Resolution1024 = 1024 UMETA(DisplayName = "1024 x 1024"),
	Resolution2048 = 2048 UMETA(DisplayName = "2048 x 2048"),
	Resolution4096 = 4096 UMETA(DisplayName = "4096 x 4096"),
	Resolution8192 = 8192 UMETA(DisplayName = "8192 x 8192")
};


/** Settings specifically related to the one-time destructive fracturing of a mesh **/
UCLASS(config = EditorPerProjectUserSettings)
class UFractureAutoUVSettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureAutoUVSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{}

	/** The pixel resolution of the generated map */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	EAutoUVTextureResolution Resolution = EAutoUVTextureResolution::Resolution512;

	/** Space to leave between UV islands, measured in texels */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = "1", ClampMax = "10", UIMax = "4"))
	int32 GutterSize = 2;

	/** Max distance to search for the outer mesh surface */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (UIMin = "1", UIMax = "100", ClampMin = ".01", ClampMax = "1000"))
	double MaxDistance = 50;

	/** The resulting automatically-generated texture map */
	UPROPERTY(VisibleAnywhere, Category = MapSettings)
	UTexture2D* Result;

	/** Whether to prompt user for an asset name for each generated texture, or automatically place them next to the source geometry collections */
	UPROPERTY(EditAnywhere, Category = MapSettings)
	bool bPromptToSave = true;
};


UCLASS(DisplayName = "AutoUV Tool", Category = "FractureTools")
class UFractureToolAutoUV : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolAutoUV(const FObjectInitializer& ObjInit);

	///
	/// UFractureModalTool Interface
	///

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const override { return FText(NSLOCTEXT("AutoUV", "ExecuteAutoUV", "AutoUV")); }
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void FractureContextChanged() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	/** Executes function that generates new geometry. Returns the first new geometry index. */
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;
	virtual bool CanExecute() const override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Gets the UI command info for this command */
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo() const;

	virtual TArray<FFractureToolContext> GetFractureToolContexts() const override;


protected:
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureAutoUVSettings> AutoUVSettings;

	bool SaveGeneratedTexture(UTexture2D* GeneratedTexture, FString ObjectBaseName, const UObject* RelativeToAsset, bool bPromptToSave);
};
