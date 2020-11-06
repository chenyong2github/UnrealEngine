// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class TextureShareSDK : ModuleRules
	{
		public TextureShareSDK(ReadOnlyTargetRules Target)
			: base(Target)
		{
			//bUseRTTI = true;
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
	}
}
