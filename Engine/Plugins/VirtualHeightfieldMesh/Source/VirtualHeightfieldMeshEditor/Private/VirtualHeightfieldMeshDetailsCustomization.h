// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Widgets/Text/STextBlock.h"
#include "Input/Reply.h"

/** UI customization for UVirtualHeightfieldMeshComponent */
class FVirtualHeightfieldMeshComponentDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FVirtualHeightfieldMeshComponentDetailsCustomization();

	/** Callback for Set Bounds button */
	FReply SetBounds();

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	class UVirtualHeightfieldMeshComponent* VirtualHeightfieldMeshComponent;
};
