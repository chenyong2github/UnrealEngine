// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AssetRegistry/AssetData.h"
#include "EditorSubsystem.h"
#include "MetasoundDocumentInterface.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundEditorSubsystem.generated.h"


// Forward Declarations
class UMetaSoundBuilderBase;

/** The subsystem in charge of editor MetaSound functionality */
UCLASS()
class METASOUNDEDITOR_API UMetaSoundEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// Build the given builder to a MetaSound asset
	// @param TemplateSoundWave - SoundWave settings such as attenuation, modulation, and sound class will be copied from the optional TemplateSoundWave. 
	// For preset builders, TemplateSoundWave will override the template values from the referenced asset
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (WorldContext = "Parent", ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "MetaSound Asset") TScriptInterface<IMetaSoundDocumentInterface> BuildToAsset(UMetaSoundBuilderBase* InBuilder, const FString& Author, const FString& AssetName, const FString& PackagePath, EMetaSoundBuilderResult& OutResult, const USoundWave* TemplateSoundWave = nullptr);
	
	// Initialize the UObject asset, with an optional MetaSound to be referenced if the asset is a preset
	void InitAsset(UObject& InNewMetaSound, UObject* InReferencedMetaSound = nullptr);

	// Initialize UMetasoundEditorGraph for a given MetaSound object
	void InitEdGraph(UObject& InMetaSound);

	// Wraps RegisterGraphWithFrontend logic in Frontend with any additional logic required to refresh editor & respective editor object state.
	// @param InMetaSound - MetaSound to register
	// @param bInForceSynchronize - Forces the synchronize flag for all open graphs being registered by this call (all referenced graphs and
	// referencing graphs open in editors)
	void RegisterGraphWithFrontend(UObject& InMetaSound, bool bInForceViewSynchronization = false);

	// Get the default author for a MetaSound asset
	const FString GetDefaultAuthor();

	static UMetaSoundEditorSubsystem& GetChecked();
	static const UMetaSoundEditorSubsystem& GetConstChecked(); 

private:
	// Copy over sound wave settings such as attenuation, modulation, and sound class from the template sound wave to the MetaSound 
	void SetSoundWaveSettingsFromTemplate(USoundWave& NewMetasound, const USoundWave& TemplateSoundWave) const;
};
