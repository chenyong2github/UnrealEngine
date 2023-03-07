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
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (WorldContext = "Parent", ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "MetaSound Asset") TScriptInterface<IMetaSoundDocumentInterface> BuildToAsset(UMetaSoundBuilderBase* InBuilder, const FString& Author, const FString& AssetName, const FString& PackagePath, EMetaSoundBuilderResult& OutResult);
};
