// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextGraphEditor : ModuleRules
	{
		public AnimNextGraphEditor(ReadOnlyTargetRules Target) : base(Target)
		{

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AssetTools",
					"AnimNext",
					"AnimNextGraph",
					"AnimNextGraphUncookedOnly",
					"UnrealEd",
					"SlateCore",
					"Slate",
					"PropertyEditor",
					"RigVM",
					"RigVMDeveloper",
					"ControlRig",
					"ControlRigDeveloper",
					"ControlRigEditor",
					"GraphEditor",
				}
			);
		}
	}
}