// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public abstract class TextureShareSDKBase : ModuleRules
{
	public TextureShareSDKBase(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDefinitions.Add("TEXTURE_SHARE_SDK_DLL");
		bRequiresImplementModule = false;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
					"Core",
					"TextureShareCore"
			}
		);

		string EnginePluginsDirectory = @"$(EngineDir)/Plugins/Runtime";

		PublicIncludePaths.AddRange(
			new string[] {
						Path.Combine(EnginePluginsDirectory, "TextureShare/Source/TextureShareCore/Public/Containers")
			});
	}

	public abstract string GetSDKRenderPlatform();
}

public class TextureShareSDK_D3D11 : TextureShareSDKBase
{
	public TextureShareSDK_D3D11(ReadOnlyTargetRules Target) : base(Target)
	{
	}

	public override string GetSDKRenderPlatform() { return "D3D11"; }
}
