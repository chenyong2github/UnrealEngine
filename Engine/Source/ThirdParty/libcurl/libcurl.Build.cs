// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class libcurl : ModuleRules
{
	public libcurl(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_LIBCURL=1");

		string LinuxLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/7_65_3/";
		string WinLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/7.83.1/";
		string AndroidLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/7_75_0/";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string platform = "/Unix/" + Target.Architecture;
			string IncludePath = LinuxLibCurlPath + "include" + platform;
			string LibraryPath = LinuxLibCurlPath + "lib" + platform;

			PublicIncludePaths.Add(IncludePath);
			PublicAdditionalLibraries.Add(LibraryPath + "/libcurl.a");

			PrivateDependencyModuleNames.Add("SSL");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string[] Architectures = new string[] {
				"ARM64",
				"x64",
			};
 
			PublicIncludePaths.Add(AndroidLibCurlPath + "include/Android/");
			foreach(var Architecture in Architectures)
			{
				PublicAdditionalLibraries.Add(AndroidLibCurlPath + "lib/Android/" + Architecture + "/libcurl.a");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/";
			PublicIncludePaths.Add(LibCurlPath + "include/Mac");
			PublicAdditionalLibraries.Add(LibCurlPath + "lib/Mac/libcurl.a");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemIncludePaths.Add(Path.Combine(WinLibCurlPath, "include"));
			PublicAdditionalLibraries.Add(Path.Combine(WinLibCurlPath, "lib", Target.Platform.ToString(), "Release", "libcurl.lib"));
			PublicDefinitions.Add("CURL_STATICLIB=1");

			// Our build requires nghttp2, OpenSSL and zlib, so ensure they're linked in
			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[]
			{
				"nghttp2",
				"OpenSSL",
				"zlib"
			});
		}
	}
}
