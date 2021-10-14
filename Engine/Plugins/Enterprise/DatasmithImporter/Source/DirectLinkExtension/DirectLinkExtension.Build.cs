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
					"EditorWidgets",
					"Engine",
					"InputCore",
					"MainFrame",
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