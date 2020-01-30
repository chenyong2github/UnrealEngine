// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class libcurl : ModuleRules
{
	public libcurl(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_LIBCURL=1");

		string LinuxLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/7_65_3/";
		string WinLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/curl-7.55.1/";
		string AndroidLibCurlPath = Target.UEThirdPartySourceDirectory + "libcurl/";

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string platform = "/Linux/" + Target.Architecture;
			string IncludePath = LinuxLibCurlPath + "include" + platform;
			string LibraryPath = LinuxLibCurlPath + "lib" + platform;

			PublicIncludePaths.Add(IncludePath);
			PublicAdditionalLibraries.Add(LibraryPath + "/libcurl.a");

			PrivateDependencyModuleNames.Add("SSL");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string[] Architectures = new string[] {
				"ARMv7",
				"ARM64",
				"x86",
				"x64",
			};
 
			foreach(var Architecture in Architectures)
			{
				PublicIncludePaths.Add(AndroidLibCurlPath + "include/Android/" + Architecture);

				PublicAdditionalLibraries.Add(AndroidLibCurlPath + "lib/Android/" + Architecture + "/libcurl.a");
//				PublicAdditionalLibraries.Add(AndroidLibCurlPath + "lib/Android/" + Architecture + "/libcrypto.a");
//				PublicAdditionalLibraries.Add(AndroidLibCurlPath + "lib/Android/" + Architecture + "/libssl.a");
//				PublicAdditionalLibraries.Add(AndroidLibCurlPath + "lib/Android/" + Architecture + "/libdl.a");
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens)
		{
			PublicIncludePaths.Add(WinLibCurlPath + "include/" + Target.Platform.ToString() +  "/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			string LibDir = WinLibCurlPath + "lib/" + Target.Platform.ToString() +  "/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName() + "/";
			PublicAdditionalLibraries.Add(LibDir + "libcurl_a.lib");
			PublicDefinitions.Add("CURL_STATICLIB=1");

			// Our build requires OpenSSL and zlib, so ensure thye're linked in
			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[]
			{
				"OpenSSL",
				"zlib"
			});
		}
	}
}
