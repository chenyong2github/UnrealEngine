// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterfacePropertyTypeCustomization.h"

namespace UE::ChooserEditor
{

class FAssetTypeActions_ChooserTable;

class FModule : public IModuleInterface
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<UE::ChooserEditor::FPropertyTypeIdentifier> InterfacePropertyTypeIdentifier;
};

}