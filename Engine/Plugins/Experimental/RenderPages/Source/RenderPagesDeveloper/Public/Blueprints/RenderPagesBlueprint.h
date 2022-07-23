// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "RenderPagesBlueprint.generated.h"


/**
 * A UBlueprint child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS(BlueprintType, Meta = (IgnoreClassThumbnail))
class RENDERPAGESDEVELOPER_API URenderPagesBlueprint : public UBlueprint
{
	GENERATED_BODY()

public:
	//~ Begin UBlueprint Interface
	virtual UClass* GetBlueprintClass() const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return true; }
	virtual bool SupportsFunctions() const override { return true; }
	virtual bool SupportsMacros() const override { return true; }
	virtual bool SupportsDelegates() const override { return true; }
	virtual bool SupportsEventGraphs() const override { return true; }
	virtual bool SupportsAnimLayers() const override { return false; }

	virtual void PostLoad() override;
	//~ End UBlueprint Interface
};
