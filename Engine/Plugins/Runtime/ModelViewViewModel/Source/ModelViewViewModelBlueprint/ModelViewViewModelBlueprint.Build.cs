// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Collections.Generic;
using UnrealBuildTool;

public class ModelViewViewModelBlueprint : ModuleRules 
{
	public ModelViewViewModelBlueprint(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"FieldNotification",
				"ModelViewViewModel",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"KismetCompiler",
				"PropertyEditor",
				"PropertyPath",
				"SlateCore",
				"Slate",
				"UMG",
				"UMGEditor",
				"UnrealEd",
			});

		PublicDefinitions.Add("UE_MVVM_WITH_VIEWMODEL_EDITOR=0");
	}
}
