// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchEditor.h"
#include "Editor.h"
#include "Animation/AnimSequence.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"

void UPoseSearchBlueprintLibrary::BuildPoseSearchIndex(const UAnimSequence* AnimationSequence, const UPoseSearchIndexConfig* Config, const UPoseSearchSchema* Schema, UPoseSearchIndex* SearchIndex)
{
	if (!ensure(AnimationSequence))
	{
		return;
	}
	if (!ensure(Config))
	{
		return;
	}
	if (!ensure(Schema))
	{
		return;
	}
	if (!ensure(SearchIndex))
	{
		return;
	}

	PoseSearchBuildIndex(*AnimationSequence, *Config, *Schema, SearchIndex);
}

UPoseSearchSchemaFactory::UPoseSearchSchemaFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPoseSearchSchema::StaticClass();
}

UObject* UPoseSearchSchemaFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPoseSearchSchema* Schema = NewObject<UPoseSearchSchema>(InParent, InClass, InName, Flags | RF_Transactional);
	return Schema;
}

class FPoseSearchEditorModule : public IPoseSearchEditorModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAssetToolsModule::GetModule().Get().RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_PoseSearchSchema));
		//UAssetToolsHelpers::GetAssetTools()->RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_PoseSearchSchema));
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FPoseSearchEditorModule, PoseSearchEditor);
