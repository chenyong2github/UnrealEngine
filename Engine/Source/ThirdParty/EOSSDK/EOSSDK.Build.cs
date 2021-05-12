// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnrealBuildTool;
using EpicGames.Core;

public class EOSSDK : ModuleRules
{
	public virtual string BaseSDKSearchPath
	{
		get
		{
			// Overridden by platform extensions to point at the PE module directory
			return ModuleDirectory;
		}
	}

	private string SDKBaseDirCached;
	public string SDKBaseDir
	{
		get
		{
			if(SDKBaseDirCached == null)
			{
				List<string> SDKSearchPaths = new List<string>
				{
					Path.Combine(BaseSDKSearchPath, "Restricted", "NotForLicensees", "SDK"),
					Path.Combine(BaseSDKSearchPath, "SDK")
				};

				SDKBaseDirCached = SDKSearchPaths.FirstOrDefault(SDKSearchPath => Directory.Exists(Path.Combine(SDKSearchPath, "Include")));

				if (string.IsNullOrEmpty(SDKBaseDirCached))
				{
					throw new BuildException("EOS SDK not found in any search location");
				}
			}

			return SDKBaseDirCached;
		}
	}

	public virtual string SDKIncludesDir
	{
		get
		{
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				return Path.Combine(SDKBinariesDir, "include");
			}
			else if(Target.Platform == UnrealTargetPlatform.IOS)
            {
				return Path.Combine(SDKBinariesDir, "EOSSDK.framework", "Headers");
			}
			return Path.Combine(SDKBaseDir, "Include");
		}
	}

	public virtual string SDKLibsDir
	{
		get
		{
			return Path.Combine(SDKBaseDir, "Lib");
		}
	}

	public virtual string SDKBinariesDir
	{
		get
		{
			if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				return Path.Combine(SDKBaseDir, "Bin", "IOS");
			}
			else if (Target.Platform == UnrealTargetPlatform.Android)
            {
				return Path.Combine(SDKBaseDir, "Bin", "Android");
            }
			return Path.Combine(SDKBaseDir, "Bin");
		}
	}

	public string EngineBinariesDir
	{
		get
		{
			return Path.Combine(EngineDirectory, "Binaries", Target.Platform.ToString());
		}
	}

	public string LibraryLinkNameBase
	{
		get
		{
			if(Target.Platform == UnrealTargetPlatform.Android)
            {
				return "EOSSDK";
            }

			return String.Format("EOSSDK-{0}-Shipping", Target.Platform.ToString());
		}
	}

	public virtual string LibraryLinkName
	{
		get
		{
			if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				return Path.Combine(SDKBinariesDir, "lib" + LibraryLinkNameBase + ".dylib");
			}
			else if(Target.Platform.IsInGroup(UnrealPlatformGroup.Unix))
			{
				return Path.Combine(SDKBinariesDir, "lib" + LibraryLinkNameBase + ".so");
			}
			else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
			{
				return Path.Combine(SDKLibsDir, LibraryLinkNameBase + ".lib");
			}
			// Android has one .so per architecture, so just deal with that below.
			// Other platforms will override this property.

			throw new BuildException("Unsupported platform");
		}
	}

	public virtual string RuntimeLibraryFileName
	{
		get
		{
			if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				return "lib" + LibraryLinkNameBase + ".dylib";
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				return LibraryLinkNameBase + ".framework";
			}
			else if (Target.Platform == UnrealTargetPlatform.Android ||
				Target.Platform.IsInGroup(UnrealPlatformGroup.Unix))
			{
				return "lib" + LibraryLinkNameBase + ".so";
			}
			else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
			{
				return LibraryLinkNameBase + ".dll";
			}
			// Other platforms will override this property.

			throw new BuildException("Unsupported platform");
		}
	}

	public virtual bool bRequiresRuntimeLoad
	{
		get
		{
			return Target.Platform.IsInGroup(UnrealPlatformGroup.Windows);
			// Other platforms may override this property.
		}
	}

	public EOSSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_EOS_SDK=1");
		PublicIncludePaths.Add(SDKIncludesDir);

		PublicDefinitions.Add(String.Format("EOSSDK_RUNTIME_LOAD_REQUIRED={0}", bRequiresRuntimeLoad ? 1 : 0));
		PublicDefinitions.Add(String.Format("EOSSDK_RUNTIME_LIBRARY_NAME=\"{0}\"", RuntimeLibraryFileName));

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicIncludePaths.Add(Path.Combine(SDKIncludesDir, "Android"));
			PublicAdditionalLibraries.Add(Path.Combine(SDKBinariesDir, "libs", "armeabi-v7a", RuntimeLibraryFileName));
			PublicAdditionalLibraries.Add(Path.Combine(SDKBinariesDir, "libs", "arm64-v8a", RuntimeLibraryFileName));

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "EOSSDK_UPL.xml"));
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicAdditionalFrameworks.Add(new Framework("EOSSDK", SDKBinariesDir, "", true));
		}
		else
		{
			PublicAdditionalLibraries.Add(Path.Combine(SDKBinariesDir, LibraryLinkName));
			
			RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", RuntimeLibraryFileName), Path.Combine(SDKBinariesDir, RuntimeLibraryFileName));

			if (bRequiresRuntimeLoad)
			{
				PublicDelayLoadDLLs.Add(RuntimeLibraryFileName);
			}
		}
	}
}
