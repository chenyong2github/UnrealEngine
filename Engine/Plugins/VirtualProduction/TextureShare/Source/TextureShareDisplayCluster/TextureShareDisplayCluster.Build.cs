// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

[SupportedPlatforms("Win64")]
public class TextureShareDisplayCluster : ModuleRules
{
	public TextureShareDisplayCluster(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(EngineDirectory,"Plugins/VirtualProduction/TextureShare/Source/TextureShare/Private"),
				Path.Combine(EngineDirectory,"Plugins/VirtualProduction/TextureShare/Source/TextureShareCore/Private"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DisplayCluster",
				"DisplayClusterShaders",
				"DisplayClusterConfiguration",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"RHI",
				"RHICore",
				"Renderer",
				"RenderCore",
				"TextureShare",
				"TextureShareCore",
			});
	}
}
