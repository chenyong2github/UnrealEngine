// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

namespace UE::AnimNext::GraphEditor
{

class FAssetTypeActions_AnimNextGraph;
class FAnimNextPropertyTypeCustomization;
class FPropertyTypeIdentifier;

class FModule : public IModuleInterface
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FAssetTypeActions_AnimNextGraph> AssetTypeActions_AnimNextGraph;
	TSharedPtr<FPropertyTypeIdentifier> AnimNextPropertyTypeIdentifier;
};

}