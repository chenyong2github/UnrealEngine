// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorUtilityBlueprint.h"
#include "Modules/ModuleManager.h"

/////////////////////////////////////////////////////
// UEditorUtilityBlueprint

UEditorUtilityBlueprint::UEditorUtilityBlueprint(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITORONLY_DATA
void UEditorUtilityBlueprint::LoadModulesRequiredForCompilation()
{
	Super::LoadModulesRequiredForCompilation();

	static const FName ModuleName(TEXT("Blutility"));
	FModuleManager::Get().LoadModule(ModuleName);
}
#endif //WITH_EDITORONLY_DATA