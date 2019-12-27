// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMergeModule.h"
#include "MeshMergeUtilities.h"
#include "Modules/ModuleManager.h"
#include "MeshMergeEditorExtensions.h"
#include "ToolMenus.h"

class FMeshMergeModule : public IMeshMergeModule
{
public:
	virtual const IMeshMergeUtilities& GetUtilities() const override
	{
		return *dynamic_cast<const IMeshMergeUtilities*>(&Utilities);
	}

	virtual IMeshMergeUtilities& GetUtilities() override
	{
		return Utilities;
	}

	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMeshMergeModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

protected:
	FMeshMergeUtilities Utilities;

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		FMeshMergeEditorExtensions::RegisterMenus();
	}
};


IMPLEMENT_MODULE(FMeshMergeModule, MeshMergeUtilities);