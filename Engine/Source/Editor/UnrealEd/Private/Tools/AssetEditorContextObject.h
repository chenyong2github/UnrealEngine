// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementAssetEditorToolkitHostMixin.h"
#include "Tools/AssetEditorContextInterface.h"
#include "UObject/Object.h"

#include "AssetEditorContextObject.generated.h"

UCLASS(Transient)
class UAssetEditorContextObject : public UObject, public IAssetEditorContextInterface, public FTypedElementAssetEditorToolkitHostMixin
{
	GENERATED_BODY()

public:
	// Begin UAssetEditorContextObject interface
	const UTypedElementSelectionSet* GetSelectionSet() const override;
	UTypedElementSelectionSet* GetMutableSelectionSet() override;
	UTypedElementCommonActions* GetCommonActions() override;
	UWorld* GetEditingWorld() const override;
	// End UAssetEditorContextObject interface

	// Begin UObject interface
	UWorld* GetWorld() const override;
	// End UObject interface
};