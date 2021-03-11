// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

class IMeshBuilderModule : public IModuleInterface
{
public:
	static inline IMeshBuilderModule& GetForPlatform(const ITargetPlatform* TargetPlatform)
	{
		check(TargetPlatform);
		return FModuleManager::LoadModuleChecked<IMeshBuilderModule>(TargetPlatform->GetMeshBuilderModuleName());
	}

	static inline IMeshBuilderModule& GetForRunningPlatform()
	{
		const ITargetPlatform* TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		return GetForPlatform(TargetPlatform);
	}

	virtual void AppendToDDCKey(FString& DDCKey) { }

	virtual bool BuildMesh(class FStaticMeshRenderData& OutRenderData, class UObject* Mesh, const class FStaticMeshLODGroup& LODGroup) { return false; }
	virtual bool BuildSkeletalMesh(const struct FSkeletalMeshBuildParameters& SkeletalMeshBuildParameters) { return false; }
	virtual void PostBuildSkeletalMesh(class FSkeletalMeshRenderData* SkeletalMeshRenderData, class USkeletalMesh* SkeletalMesh) { }
};
