// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingBaseComponent.h"

#if WITH_EDITOR
#include "Widgets/Layout/SConstraintCanvas.h"
#endif

#include "DMXPixelMappingOutputComponent.generated.h"

#if WITH_EDITOR
enum class EDMXPixelMappingComponentLabelAlignment : uint8;
class FDMXPixelMappingComponentWidget;
#endif
class UDMXEntityFixturePatch;
class UDMXPixelMappingRendererComponent;

class SBox;


/** Definition for Default colors */
struct FDMXOutputComponentColors
{
	// Note, for the default color, use the editor color

	/** The color used when no the component is selected */
	virtual const FLinearColor& GetSelectedColor() { return SelectedColor; }

private:
	static const FLinearColor SelectedColor;
};

/** Enum that defines the quality of how pixels are rendered */
UENUM()
enum class EDMXPixelBlendingQuality : uint8
{
	/** 1 sample */
	Low,

    /** 5 samples ( 2 x 2 with center) */
    Medium,

	/** 9 samples ( 3 x 3 ) */
	High
};

/**
 * Base class for all Designer and configurable components
 */
UCLASS(Abstract)
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingOutputComponent
	: public UDMXPixelMappingBaseComponent
{
	GENERATED_BODY()

public:
	/** Default Constructor */
	UDMXPixelMappingOutputComponent();

protected:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
#endif // WITH_EDITOR
	virtual void BeginDestroy() override;
	//~ End UObject Interface

public:
	//~ Begin DMXPixelMappingBaseComponent interface
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;
	virtual void AddChild(UDMXPixelMappingBaseComponent* InComponent) override;
	virtual void RemoveChild(UDMXPixelMappingBaseComponent* InComponent) override;
	//~ End DMXPixelMappingBaseComponent interface
 

	/*----------------------------------
		UDMXPixelMappingOutputComponent Interface
	----------------------------------*/
#if WITH_EDITOR
	/** Whether component should be part of Palette view */
	virtual bool IsExposedToTemplate() { return false; }

	/** Returns the text of palette category*/
	virtual const FText GetPaletteCategory();

	/** Rebuild widget for designer view */
	virtual TSharedRef<FDMXPixelMappingComponentWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas);

	/** Whether component should be visible */
	virtual bool IsVisible() const;

	/** Whether component can be re-sized or re-position at the editor */
	virtual bool IsLockInDesigner() const { return bLockInDesigner; }

	/** Sets the ZOrder in the UI */
	virtual void SetZOrder(int32 NewZOrder);

	/** Returns the UI ZOrder */
	virtual int32 GetZOrder() const { return ZOrder; }

	/** Returns an editor color for the widget */
	virtual FLinearColor GetEditorColor() const { return EditorColor; }
#endif // WITH_EDITOR

	/** Returns true if the the component's over all its parents. */
	virtual bool IsOverParent() const;

	/** Returns true if the component is over specified position */
	virtual bool IsOverPosition(const FVector2D& Position) const;

	/** Returns true if the component overlaps the other */
	virtual bool OverlapsComponent(UDMXPixelMappingOutputComponent* Other) const;

	/** Get pixel index in downsample texture */
	virtual int32 GetDownsamplePixelIndex() const { return 0; }

	/** Queue rendering to downsample rendering target */
	virtual void QueueDownsample() {}

	/** Sets the position */
	virtual void SetPosition(const FVector2D& NewPosition);

	/** Returns the position */
	FVector2D GetPosition() const { return FVector2D(PositionX, PositionY); }

	/** Sets the size */
	virtual void SetSize(const FVector2D& NewSize);

	/** Get the size of the component */
	FVector2D GetSize() const { return FVector2D(SizeX, SizeY); }

	/** Helper that returns render component if available */
	UDMXPixelMappingRendererComponent* FindRendererComponent() const;
	/** Updates children to match the size of this instance */

#if WITH_EDITOR
	/** Makes the component the highest ZOrdered of components in the component rectangle, updates childs if needed */
	void MakeHighestZOrderInComponentRect();
#endif // WITH_EDITOR

public:
#if WITH_EDITOR
	/** Returns the component widget */
	FORCEINLINE const TSharedPtr<FDMXPixelMappingComponentWidget> GetComponentWidget() { return ComponentWidget; }

protected:
	/** Returns the canvas of the render component if available */
	TSharedPtr<SConstraintCanvas> FindRendererComponentCanvas() const;

	/** Udpates the component widget. If bWithChildrenRecursive, updates child Components' Component Widget recursively */
	void UpdateComponentWidget(EVisibility NewVisibility = EVisibility::Visible, bool bWithChildrenRecursive = false);
#endif // WITH_EDITOR

public:
#if WITH_EDITORONLY_DATA
	/** ZOrder in the UI */
	UPROPERTY()
	int32 ZOrder = 1;

	/** The color displayed in editor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Editor Settings")
	FLinearColor EditorColor = FLinearColor::Blue;

	/** Where the component's label is shown, if it uses one. */
	EDMXPixelMappingComponentLabelAlignment LabelAlignment;

protected:
	/** Children available PreEditUndo, useful to hide all removed ones in post edit undo */
	TArray<UDMXPixelMappingBaseComponent*> PreEditUndoChildren;

	/** The widget shown for this component */
	TSharedPtr<FDMXPixelMappingComponentWidget> ComponentWidget;
#endif // WITH_EDITORONLY_DATA


	/*----------------------------------
		Blueprint interface
	----------------------------------*/
public:
#if WITH_EDITOR
	/** Returns the bIsLockInDesigner property name */
	FORCEINLINE static FName GetLockInDesignerPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, bLockInDesigner); }

	/** Returns the bIsVisibleInDesigner property name */
	FORCEINLINE static FName GetVisibleInDesignerPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, bVisibleInDesigner); }
#endif

	/** The quality level to use when averaging colors during downsampling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pixel Settings")
	EDMXPixelBlendingQuality CellBlendingQuality;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", Meta = (EditCondition = "!bLockInDesigner"))
	float PositionX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", Meta = (EditCondition = "!bLockInDesigner"))
	float PositionY;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", Meta = (ClampMin = 0, UIMin = 0, EditCondition = "!bLockInDesigner"))
	float SizeX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Common Settings", Meta = (ClampMin = 0, UIMin = 0, EditCondition = "!bLockInDesigner"))
	float SizeY;

protected:
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Editor Settings")
	bool bLockInDesigner;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Editor Settings")
	bool bVisibleInDesigner;
#endif
};
