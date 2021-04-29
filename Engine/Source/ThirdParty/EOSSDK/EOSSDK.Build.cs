// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using EpicGames.Core;

public class EOSSDK : ModuleRules
{
	// Set this to true to build with EOS.
	protected bool bBuildWithEOS = false;

	public string SDKBaseDir
	{
		get
		{
			return Path.Combine(ModuleDirectory, "EOSSDK1.12", "SDK");
		}
	}

	public virtual string SDKIncludesDir
	{
		get
		{
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
			return Path.Combine(SDKBaseDir, "Bin");
		}
	}

	public string ProjectBinariesDir
	{
		get
		{
			return Path.Combine("$(ProjectDir)", "Binaries", Target.Platform.ToString());
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
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				return Path.Combine(SDKBinariesDir, LibraryLinkNameBase + ".framework");
			}
			else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Unix))
			{
				return Path.Combine(SDKBinariesDir, "lib" + LibraryLinkNameBase + ".so");
			}
			else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Microsoft))
			{
				return Path.Combine(SDKLibsDir, LibraryLinkNameBase + ".lib");
			}
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
			else if (Target.Platform.IsInGroup(UnrealPlatformGroup.Unix))
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

		PublicDefinitions.Add(String.Format("WITH_EOS_SDK={0}", bBuildWithEOS ? 1 : 0));

		if (bBuildWithEOS)
		{
			PublicIncludePaths.Add(SDKIncludesDir);

			PublicDefinitions.Add(String.Format("EOSSDK_RUNTIME_LOAD_REQUIRED={0}", bRequiresRuntimeLoad ? 1 : 0));
			PublicDefinitions.Add(String.Format("EOSSDK_RUNTIME_LIBRARY_NAME=\"{0}\"", RuntimeLibraryFileName));

			string RuntimeLibrarySourcePath = Path.Combine(SDKBinariesDir, RuntimeLibraryFileName);
			string RuntimeLibraryTargetPath = Path.Combine(ProjectBinariesDir, RuntimeLibraryFileName);

			if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				PublicAdditionalFrameworks.Add(new Framework("EOSSDK", SDKBinariesDir, "", true));
			}
			else
			{
				PublicSystemLibraryPaths.Add(SDKBinariesDir);
				PublicAdditionalLibraries.Add(LibraryLinkName);
				RuntimeDependencies.Add(
					RuntimeLibraryTargetPath,
					RuntimeLibrarySourcePath,
					StagedFileType.NonUFS);

				if (bRequiresRuntimeLoad)
				{
					PublicRuntimeLibraryPaths.Add(ProjectBinariesDir);
					PublicDelayLoadDLLs.Add(RuntimeLibraryFileName);
				}
			}
		}
	}
}
