// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "AssetTypeActions/AssetTypeActions_AnimationAsset.h"
#include "Engine/EngineTypes.h"
#include "Factories/Factory.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearch.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

#include "PoseSearchEditor.generated.h"


class UAnimSequence;

UCLASS()
class POSESEARCHEDITOR_API UPoseSearchBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Compiles a pose search index */
	UFUNCTION(BlueprintCallable, Category = "AnimationBlueprintLibrary|PoseSearch")
	static void BuildPoseSearchIndex(
	    const UAnimSequence* AnimationSequence,
	    const UPoseSearchIndexConfig* SearchConfig,
	    const UPoseSearchSchema* SearchSchema,
	    UPoseSearchIndex* SearchIndex);
};

UCLASS()
class POSESEARCHEDITOR_API UPoseSearchSchemaFactory : public UFactory
{
	GENERATED_BODY()
public:
	UPoseSearchSchemaFactory(const FObjectInitializer& ObjectInitializer);
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

class POSESEARCHEDITOR_API FAssetTypeActions_PoseSearchSchema : public FAssetTypeActions_AnimationAsset
{
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "PoseSearchEditor", "Pose Search Schema"); }
	virtual UClass* GetSupportedClass() const override { return UPoseSearchSchema::StaticClass(); }
	virtual bool CanFilter() override { return true; }
};

class IPoseSearchEditorModuleInterface : public IModuleInterface
{
};