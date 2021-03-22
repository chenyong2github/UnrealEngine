// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NiagaraVersionMetaData.generated.h"

UENUM()
enum class ENiagaraPythonUpdateScriptReference : uint8
{
	None,
    ScriptAsset,
    DirectTextEntry
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraVersionMetaData : public UObject
{
	GENERATED_BODY()
public:
	UNiagaraVersionMetaData();

	/** If true then this version is exposed to the user and is used as the default version for new assets. */
	UPROPERTY(EditAnywhere, Category="Version Details", meta=(EditCondition="!bIsExposedVersion"))
	bool bIsExposedVersion;

	/** Changelist displayed to the user when upgrading to a new script version. */
	UPROPERTY(EditAnywhere, Category="Version Details", meta = (MultiLine = true))
	FText ChangeDescription;

	/** Internal version guid, mainly useful for debugging version conflicts. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category="Version Details")
	FGuid VersionGuid;

	// TODO: enable python script execution for version upgrades 
	
	/** Reference to a python script that is executed when the user updates from a previous version to this version. */
	//UPROPERTY(EditAnywhere, Category="Scripting")
	ENiagaraPythonUpdateScriptReference UpdateScriptExecution;

	/** Python script to run when updating to this script version. */
	//UPROPERTY(EditAnywhere, Category="Scripting", meta=(MultiLine = true, EditCondition="UpdateScriptExecution == ENiagaraPythonUpdateScriptReference::DirectTextEntry"))
	FText PythonUpdateScript;

	/** Asset reference to a python script to run when updating to this script version. */
	//UPROPERTY(EditAnywhere, Category="Scripting", meta=(EditCondition="UpdateScriptExecution == ENiagaraPythonUpdateScriptReference::ScriptAsset"))
	FString ScriptAssetPath;
};
