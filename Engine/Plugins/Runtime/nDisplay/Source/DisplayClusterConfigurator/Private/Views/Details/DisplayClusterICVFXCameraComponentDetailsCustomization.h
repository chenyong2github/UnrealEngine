// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class UDisplayClusterICVFXCameraComponent;
class ACineCameraActor;
class IDetailLayoutBuilder;

class FDisplayClusterICVFXCameraComponentDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	// IDetailCustomization interface
	virtual void CustomizeDetails(class IDetailLayoutBuilder& InLayoutBuilder) override;

protected:
	TWeakObjectPtr<UDisplayClusterICVFXCameraComponent> EditedObject;
	/** Keep a reference to force refresh the layout */
	IDetailLayoutBuilder* DetailLayout = nullptr;
};
