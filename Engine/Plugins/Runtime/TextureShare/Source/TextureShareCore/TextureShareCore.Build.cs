// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class TextureShareCore : ModuleRules
{
	public TextureShareCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[]
			{
				"TextureShareCore/Private",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		if (Target.bShouldCompileAsDLL)
		{
			PublicDefinitions.Add("TEXTURESHARECORE_RHI=0");
		}
		else
		{
			PublicDefinitions.Add("TEXTURESHARECORE_RHI=1");
			PrivateDependencyModuleNames.AddRange(
						new string[]
						{
							"Core",
							"Engine",
							"RHI",
							"RenderCore",
							"TextureShareD3D11",
							"TextureShareD3D12",
						});
		}

		string TextureShareLibPlatformName = Target.Platform.ToString();

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicDefinitions.Add("TEXTURESHARELIB_PLATFORM_WINDOWS=1");
			TextureShareLibPlatformName = "Windows";

			// Configure supported render devices for windows platform:
			PublicDefinitions.Add("TEXTURESHARELIB_USE_D3D11=1");
			PublicDefinitions.Add("TEXTURESHARELIB_USE_D3D12=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDefinitions.Add("TEXTURESHARELIB_PLATFORM_MAC=1");
			TextureShareLibPlatformName = "Mac";
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicDefinitions.Add("TEXTURESHARELIB_PLATFORM_LINUX=1");
			TextureShareLibPlatformName = "Linux";
		}

		PublicDefinitions.Add("TEXTURESHARELIB_PLATFORM=" + TextureShareLibPlatformName);
	}
}
