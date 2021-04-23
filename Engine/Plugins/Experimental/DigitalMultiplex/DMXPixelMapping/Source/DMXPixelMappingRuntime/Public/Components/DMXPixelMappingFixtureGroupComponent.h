// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DMXPixelMappingOutputComponent.h"
#include "Library/DMXEntityReference.h"
#include "DMXPixelMappingFixtureGroupComponent.generated.h"


class UDMXLibrary;
class STextBlock;
class SUniformGridPanel;

/**
 * Container component for Fixture Items
 */
UCLASS()
class DMXPIXELMAPPINGRUNTIME_API UDMXPixelMappingFixtureGroupComponent
	: public UDMXPixelMappingOutputComponent
{
	GENERATED_BODY()
public:
	/** Default Constructor */
	UDMXPixelMappingFixtureGroupComponent();

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
	virtual void PostParentAssigned() override;

#if WITH_EDITOR
	virtual FString GetUserFriendlyName() const override;
#endif
	//~ End UDMXPixelMappingBaseComponent implementation

	//~ Begin UDMXPixelMappingOutputComponent implementation
#if WITH_EDITOR
	virtual bool IsExposedToTemplate() { return true; }
	virtual const FText GetPaletteCategory() override;
	virtual TSharedRef<SWidget> BuildSlot(TSharedRef<SConstraintCanvas> InCanvas) override;
	virtual void ToggleHighlightSelection(bool bIsSelected) override;

	virtual void UpdateWidget() override;
#endif // WITH_EDITOR

	virtual FVector2D GetSize() const override;
	virtual FVector2D GetPosition() override;
	virtual void SetPosition(const FVector2D& InPosition) override;
	virtual void SetSize(const FVector2D& InSize) override;

	virtual void QueueDownsample() override;

#if WITH_EDITOR
	virtual void SetZOrder(int32 NewZOrder) override;
#endif // WITH_EDITOR
	//~ End UDMXPixelMappingOutputComponent implementation

	/** Check if a Component can be moved under another one (used for copy/move/duplicate) */
	virtual bool CanBeMovedTo(const UDMXPixelMappingBaseComponent* Component) const override;

private:
	void SetPositionWithChildren();

	void SetSizeWithinMinBoundaryBox();

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture List")
	UDMXLibrary* DMXLibrary;

	UPROPERTY(Transient)
	TSet<FDMXEntityFixturePatchRef> SelectedFixturePatchRef;

private:
#if WITH_EDITORONLY_DATA
	TSharedPtr<SUniformGridPanel> GridPanel;

	FSlateBrush Brush;

	TSharedPtr<STextBlock> LibraryNameWidget;
#endif

	float PositionXCached;

	float PositionYCached;

private:
	static const FVector2D MinGroupSize;
};