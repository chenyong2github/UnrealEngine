// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Reply.h"

/** UI customization for UBoundsCopyComponent */
class FBoundsCopyComponentDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FBoundsCopyComponentDetailsCustomization();

	/** Callback for Copy Rotation button */
	FReply SetRotation();
	/** Callback for Copy Bounds button */
	FReply SetTransformToBounds();

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	class UBoundsCopyComponent* BoundsCopyComponent;
};
