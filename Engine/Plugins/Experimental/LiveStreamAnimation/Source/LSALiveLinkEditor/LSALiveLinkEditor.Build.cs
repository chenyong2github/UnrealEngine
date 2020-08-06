// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LSALiveLinkEditor : ModuleRules
	{
		public LSALiveLinkEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"LiveStreamAnimation",
					"LSALiveLink",
					"Core",
					"CoreUObject",
					"UnrealEd",
					"AssetTools",
					"LiveLinkInterface",
					"Engine",
					"PropertyEditor",
					"SlateCore",
					"Slate",
					"InputCore",
					"EditorStyle",
				}
			);
		}
	}
}
