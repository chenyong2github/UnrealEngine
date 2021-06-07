// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

using System.IO;
using System.Collections.Generic;
using UnrealBuildTool;

public class BinkMediaPlayer : ModuleRules 
{
	// Platform Extensions need to override these
	protected virtual string LibRootDirectory { get { return ModuleDirectory; } }
	protected virtual string LibName { get { return null; } }

	protected virtual string SdkBaseDirectory { get { return Path.Combine(LibRootDirectory, "..", "SDK"); } }
	protected virtual string LibDirectory { get { return Path.Combine(SdkBaseDirectory, "lib"); } }

	protected virtual string IncDirectory { get { return Path.Combine(ModuleDirectory, "..", "SDK", "include"); } }

    public BinkMediaPlayer(ReadOnlyTargetRules Target) : base(Target)
    {
		bAllowConfidentialPlatformDefines = true;

        PublicDependencyModuleNames.Add("Core");
        PublicDependencyModuleNames.Add("CoreUObject");
        PublicDependencyModuleNames.Add("Engine");
        PublicDependencyModuleNames.Add("InputCore");
        PublicDependencyModuleNames.Add("RenderCore");
        PublicDependencyModuleNames.Add("RHI");
        PublicDependencyModuleNames.Add("MoviePlayer");
        //PublicDependencyModuleNames.Add("MediaAssets");

        PrivatePCHHeaderFile = "Private/BinkMediaPlayerPCH.h";

        if (Target.bBuildEditor == true)
        {
			PublicDependencyModuleNames.Add("Slate");
			PublicDependencyModuleNames.Add("SlateCore");
			PublicDependencyModuleNames.Add("DesktopWidgets");
			PublicDefinitions.Add("BINKPLUGIN_UE4_EDITOR=1");
            PrivateDependencyModuleNames.AddRange(new string[] { "PropertyEditor", "DesktopPlatform", "SourceControl", "EditorStyle", "UnrealEd" });
        }
        else
        {
            PublicDefinitions.Add("BINKPLUGIN_UE4_EDITOR=0");
        }

		PublicDefinitions.Add("BUILDING_FOR_UNREAL_ONLY=1");
		RuntimeDependencies.Add("$(ProjectDir)/Content/Movies/..."); // For chunked streaming

		string Lib = LibName;
		string Platform = Target.Platform.ToString();

		if(Lib == null)
		{
			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Microsoft))
			{
				Lib = "BinkUnreal" + Platform + ".lib";
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				Lib = "BinkUnreal" + Platform + ".a";
				PublicDependencyModuleNames.Add("MetalRHI");
				PublicFrameworks.Add("AudioToolbox");
			}
            else if (Target.Platform == UnrealTargetPlatform.IOS)
            {
                Lib = "libBinkUnreal" + Platform + ".a";
                PublicDependencyModuleNames.Add("MetalRHI");
            }
            else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Apple))
			{
				Lib = "BinkUnreal" + Platform + ".a";
				PublicDependencyModuleNames.Add("MetalRHI");
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
			{
				PublicDependencyModuleNames.Add("Launch");
				PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "libBinkUnrealAndroidArm32.a"));
				PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, "libBinkUnrealAndroidArm64.a"));
				string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
				AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "BinkMediaPlayer_APL.xml"));
			}
			else
			{
				Lib = "BinkUnreal" + Platform + ".a";
			}
		}

		if (Lib != null)
		{
			PublicAdditionalLibraries.Add(Path.Combine(LibDirectory, Lib));
		}
        PublicIncludePaths.Add(IncDirectory);
	}
}
