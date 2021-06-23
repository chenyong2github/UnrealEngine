// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "CameraNodalOffsetAlgo.generated.h"

struct FGeometry;
struct FNodalPointOffset;
struct FPointerEvent;
struct FTransform;

class UNodalOffsetTool;
class SWidget;

/**
 * UCameraNodalOffsetAlgo defines the interface that any nodal calibration point algorithm should implement
 * in order to be used and listed by the Nodal Offset Tool. 
 */
UCLASS(Abstract)
class CAMERACALIBRATIONCORE_API UCameraNodalOffsetAlgo : public UObject
{
	GENERATED_BODY()

public:

	/** Make sure you initialize before using the object */
	virtual void Initialize(UNodalOffsetTool* InNodalOffsetTool) {};

	/** Clean up resources and don't use NodalOffsetTool anymore */
	virtual void Shutdown() {};

	/** Called every frame */
	virtual void Tick(float DeltaTime) {};

	/** Callback when viewport is clicked. Returns false if the event was not handled. */
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return false;  };

	/** Returns the UI of this calibrator. Expected to only be called once */
	virtual TSharedRef<SWidget> BuildUI() { return SNullWidget::NullWidget; };

	/** Returns the most recently calibrated nodal offset transform, with an error metric */
	virtual bool GetNodalOffset(FNodalPointOffset& OutNodalOffset, float& OutFocus, float& OutZoom, float& OutError, FText& OutErrorMessage) { return false; };

	/** Returns a descriptive name/title of this nodal offset algorithm */
	virtual FName FriendlyName() const { return TEXT("Invalid Name"); };

	/** Called when the current offset was saved */
	virtual void OnSavedNodalOffset() { };

	/** Called to present the user with instructions on how to this this algo */
	virtual TSharedRef<SWidget> BuildHelpWidget() { return SNew(STextBlock).Text(FText::FromString(TEXT("Coming soon!")));  };
};
