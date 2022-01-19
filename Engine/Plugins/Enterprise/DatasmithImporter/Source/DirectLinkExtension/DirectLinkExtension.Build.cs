// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DirectLinkExtension : ModuleRules
	{
		public DirectLinkExtension(ReadOnlyTargetRules Target)
			: base(Target)
		{

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ContentBrowserData",
					"EditorStyle",
					"EditorWidgets",
					"Engine",
					"InputCore",
					"MainFrame",
					"Projects",
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UnrealEd",
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"ContentBrowser",
					"Core",
					"CoreUObject",
					"DirectLink",
					"ExternalSource",
				}
			);
		}
	}
}