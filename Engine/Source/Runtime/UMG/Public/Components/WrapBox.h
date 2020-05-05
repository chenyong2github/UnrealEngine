// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Components/PanelWidget.h"
#include "WrapBox.generated.h"

class UWrapBoxSlot;

/**
 * Arranges widgets left-to-right or top-to-bottom dependently of the orientation.  When the widgets exceed the wrapSize it will place widgets on the next line.
 * 
 * * Many Children
 * * Flows
 * * Wraps
 */
UCLASS()
class UMG_API UWrapBox : public UPanelWidget
{
	GENERATED_UCLASS_BODY()

public:
	/** The inner slot padding goes between slots sharing borders */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Content Layout")
	FVector2D InnerSlotPadding;

	/** DEPRECATED value replaced by WrapSize, When this width is exceeded, elements will start appearing on the next line. */
	UPROPERTY()
	float WrapWidth_DEPRECATED;

	/** When this size is exceeded, elements will start appearing on the next line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content Layout", meta=(EditCondition = "bExplicitWrapSize"))
	float WrapSize;

	/** DEPRECATED value replaced by bExplicitWrapSize, Use explicit wrap width whenever possible. It greatly simplifies layout calculations and reduces likelihood of "wiggling UI" */
	UPROPERTY()
	bool bExplicitWrapWidth_DEPRECATED;

	/** Use explicit wrap size whenever possible. It greatly simplifies layout calculations and reduces likelihood of "wiggling UI" */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content Layout")
	bool bExplicitWrapSize;

	/** Determines if the Wrap Box should arranges the widgets left-to-right or top-to-bottom */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content Layout")
	TEnumAsByte<EOrientation> Orientation = EOrientation::Orient_Horizontal;

	/** Sets the inner slot padding goes between slots sharing borders */
	UFUNCTION(BlueprintCallable, Category="Content Layout")
	void SetInnerSlotPadding(FVector2D InPadding);

public:

	UE_DEPRECATED(4.22, "Deprecated, please use AddChildToWrapBox() instead")
	UWrapBoxSlot* AddChildWrapBox(UWidget* Content);

	UFUNCTION(BlueprintCallable, Category="Panel")
	UWrapBoxSlot* AddChildToWrapBox(UWidget* Content);

#if WITH_EDITOR
	// UWidget interface
	virtual const FText GetPaletteCategory() override;
	// End UWidget interface
#endif

	virtual void PostLoad() override;

protected:

	// UPanelWidget
	virtual UClass* GetSlotClass() const override;
	virtual void OnSlotAdded(UPanelSlot* Slot) override;
	virtual void OnSlotRemoved(UPanelSlot* Slot) override;
	// End UPanelWidget

	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

	// UVisual interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual interface

protected:

	TSharedPtr<class SWrapBox> MyWrapBox;

protected:
	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget interface
};
