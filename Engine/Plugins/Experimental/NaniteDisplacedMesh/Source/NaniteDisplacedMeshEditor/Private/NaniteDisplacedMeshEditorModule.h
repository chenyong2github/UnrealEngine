// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

class FAssetTypeActions_NaniteDisplacedMesh;
class NaniteDisplacedMeshTransientPackage;

class FNaniteDisplacedMeshEditorModule : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FNaniteDisplacedMeshEditorModule& GetModule();

	UPackage* GetNaniteDisplacementMeshTransientPackage() const;

private:
	FAssetTypeActions_NaniteDisplacedMesh* NaniteDisplacedMeshAssetActions;
	UPackage* NaniteDisplacedMeshTransientPackage;
};
