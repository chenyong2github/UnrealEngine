// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Widgets/SWidget.h"
#include "Engine/MeshMerging.h"
#include "IMergeActorsTool.h"

#include "MeshInstancingTool.generated.h"

class SMeshInstancingDialog;

/** Singleton wrapper to allow for using the setting structure in SSettingsView */
UCLASS(config = Engine)
class UMeshInstancingSettingsObject : public UObject
{
	GENERATED_BODY()
public:
	UMeshInstancingSettingsObject()
	{
	}

	static UMeshInstancingSettingsObject* Get()
	{
		if (!bInitialized)
		{
			DefaultSettings = DuplicateObject(GetMutableDefault<UMeshInstancingSettingsObject>(), nullptr);
			DefaultSettings->AddToRoot();
			bInitialized = true;
		}

		return DefaultSettings;
	}

	static void Destroy()
	{
		if (bInitialized)
		{
			if (UObjectInitialized() && DefaultSettings)
			{
				DefaultSettings->RemoveFromRoot();
				DefaultSettings->MarkPendingKill();
			}

			DefaultSettings = nullptr;
			bInitialized = false;
		}
	}

protected:
	static bool bInitialized;
	// This is a singleton, duplicate default object
	static UMeshInstancingSettingsObject* DefaultSettings;

public:
	UPROPERTY(EditAnywhere, meta = (ShowOnlyInnerProperties), Category = MergeSettings)
	FMeshInstancingSettings Settings;
};

/**
 * Mesh Instancing Tool
 */
class FMeshInstancingTool : public IMergeActorsTool
{
	friend class SMeshInstancingDialog;

public:

	FMeshInstancingTool();
	~FMeshInstancingTool();

	// IMergeActorsTool interface
	virtual TSharedRef<SWidget> GetWidget() override;
	virtual FName GetIconName() const override { return "MergeActors.MeshInstancingTool"; }
	virtual FText GetTooltipText() const override;
	virtual FString GetDefaultPackageName() const override;
	virtual bool CanMerge() const override;
	virtual bool RunMerge(const FString& PackageName) override;

	/** Runs the merging logic to determine predicted results */
	FText GetPredictedResultsText();
private:
	/** Pointer to the mesh instancing dialog containing settings for the merge */
	TSharedPtr<SMeshInstancingDialog> InstancingDialog;

	/** Pointer to singleton settings object */
	UMeshInstancingSettingsObject* SettingsObject;
};
