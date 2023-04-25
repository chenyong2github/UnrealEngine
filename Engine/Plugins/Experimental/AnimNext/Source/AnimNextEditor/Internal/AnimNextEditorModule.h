// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace UE::AnimNext::Editor
{

class FAssetTypeActions_AnimNextGraph;
class FAnimNextPropertyTypeCustomization;
class FPropertyTypeIdentifier;
struct FParameterPickerArgs;

class FModule : public IModuleInterface
{
public:
	// Create a parameter picker
	TSharedPtr<SWidget> CreateParameterPicker(const FParameterPickerArgs& InArgs);
	
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FAssetTypeActions_AnimNextGraph> AssetTypeActions_AnimNextGraph;
	TSharedPtr<FPropertyTypeIdentifier> AnimNextPropertyTypeIdentifier;
};

}