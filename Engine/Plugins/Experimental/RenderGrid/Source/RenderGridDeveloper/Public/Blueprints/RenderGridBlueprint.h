// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "RenderGrid/RenderGrid.h"
#include "RenderGridBlueprint.generated.h"


namespace UE::RenderGrid
{
	/** A delegate for URenderGridBlueprint::PropagateToInstances. */
	DECLARE_DELEGATE_OneParam(FRenderGridBlueprintRunOnInstancesCallback, URenderGrid* /*Instance*/);
}


/**
 * A UBlueprint child class for the RenderGrid modules.
 *
 * Required in order for a RenderGrid to be able to have a blueprint graph.
 */
UCLASS(BlueprintType, Meta=(IgnoreClassThumbnail))
class RENDERGRIDDEVELOPER_API URenderGridBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	URenderGridBlueprint();

	//~ Begin UBlueprint Interface
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return true; }
	virtual bool SupportsFunctions() const override { return true; }
	virtual bool SupportsMacros() const override { return true; }
	virtual bool SupportsDelegates() const override { return true; }
	virtual bool SupportsEventGraphs() const override { return true; }
	virtual bool SupportsAnimLayers() const override { return false; }
	virtual bool ShouldBeMarkedDirtyUponTransaction() const override { return false; }
	virtual UClass* GetBlueprintClass() const override;
	virtual void PostLoad() override;
	//~ End UBlueprint Interface

private:
	void RunOnInstances(const UE::RenderGrid::FRenderGridBlueprintRunOnInstancesCallback& Callback);

public:
	void Load();
	void Save();

	void PropagateJobsToInstances();
	void PropagateAllPropertiesExceptJobsToInstances();
	void PropagateAllPropertiesToInstances();

	void PropagateJobsToAsset(URenderGrid* Instance);
	void PropagateAllPropertiesExceptJobsToAsset(URenderGrid* Instance);
	void PropagateAllPropertiesToAsset(URenderGrid* Instance);

public:
	/** Returns the RenderGrid reference that this RenderGrid asset contains. */
	UFUNCTION(BlueprintPure, Category="Render Grid")
	URenderGrid* GetRenderGrid() const { return RenderGrid; }

private:
	virtual void OnPostVariablesChange(UBlueprint* InBlueprint);

private:
	UPROPERTY()
	TObjectPtr<URenderGrid> RenderGrid;
};
