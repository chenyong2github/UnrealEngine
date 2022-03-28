// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

namespace UnrealBuildBase
{
	public static class Unreal
	{
		private static DirectoryReference FindRootDirectory()
		{
			if (LocationOverride.RootDirectory != null)
			{
				return LocationOverride.RootDirectory;
			}

			// This base library may be used - and so be launched - from more than one location (at time of writing, UnrealBuildTool and AutomationTool)
			// Programs that use this assembly must be located under "Engine/Binaries/DotNET" and so we look for that sequence of directories in that path of the executing assembly
			
			// Use the EntryAssembly (the application path), rather than the ExecutingAssembly (the library path)
			string AssemblyLocation = Assembly.GetEntryAssembly()!.GetOriginalLocation();

			DirectoryReference? FoundRootDirectory = DirectoryReference.FindCorrectCase(DirectoryReference.FromString(AssemblyLocation)!);

			// Search up through the directory tree for the deepest instance of the sub-path "Engine/Binaries/DotNET"
			while(FoundRootDirectory != null)
			{
				if (String.Equals("DotNET", FoundRootDirectory.GetDirectoryName()))
				{
					FoundRootDirectory = FoundRootDirectory.ParentDirectory;
					if (FoundRootDirectory != null && String.Equals("Binaries", FoundRootDirectory.GetDirectoryName()))
					{
						FoundRootDirectory = FoundRootDirectory.ParentDirectory;
						if (FoundRootDirectory != null && String.Equals("Engine", FoundRootDirectory.GetDirectoryName()))
						{
							FoundRootDirectory = FoundRootDirectory.ParentDirectory;
							break;
						}
						continue;
					}
					continue;
				}
				FoundRootDirectory = FoundRootDirectory.ParentDirectory;
			}

			if (FoundRootDirectory == null)
			{
				throw new Exception($"This code requires that applications using it are launched from a path containing \"Engine/Binaries/DotNET\". This application was launched from {Path.GetDirectoryName(AssemblyLocation)}");
			}

			// Confirm that we've found a valid root directory, by testing for the existence of a well-known file
			FileReference ExpectedExistingFile = FileReference.Combine(FoundRootDirectory, "Engine", "Build", "Build.version");
			if (!FileReference.Exists(ExpectedExistingFile))
			{
				throw new Exception($"Expected file \"Engine/Build/Build.version\" was not found at {ExpectedExistingFile.FullName}");
			}

			return FoundRootDirectory;
		}

		private static FileReference FindUnrealBuildTool()
		{
			// todo: use UnrealBuildTool.dll (same on all platforms). Will require changes wherever UnrealBuildTool is invoked.
			string UBTName = "UnrealBuildTool" + RuntimePlatform.ExeExtension;

			// the UnrealBuildTool executable is assumed to be located under {RootDirectory}/Engine/Binaries/DotNET/UnrealBuildTool/
			FileReference UnrealBuildToolPath = FileReference.Combine(EngineDirectory, "Binaries", "DotNET", "UnrealBuildTool", UBTName);

			UnrealBuildToolPath = FileReference.FindCorrectCase(UnrealBuildToolPath);

			if (!FileReference.Exists(UnrealBuildToolPath))
			{
				// if there's no exe found, use the dll as a fallback
				UnrealBuildToolPath = FileReference.FindCorrectCase(UnrealBuildToolPath.ChangeExtension(".dll"));
			}

			if (!FileReference.Exists(UnrealBuildToolPath))
			{
				throw new Exception($"Unable to find UnrealBuildTool in the expected location at {UnrealBuildToolPath.FullName}");
			}

			return UnrealBuildToolPath;
		}

		static private DirectoryReference FindDotnetDirectory()
		{
			string HostDotNetDirectoryName;
			switch (RuntimePlatform.Current)
			{
				case RuntimePlatform.Type.Windows: HostDotNetDirectoryName = "Windows"; break;
				case RuntimePlatform.Type.Mac:     HostDotNetDirectoryName = "Mac"; break;
				case RuntimePlatform.Type.Linux:   HostDotNetDirectoryName = "Linux"; break;
				default: throw new Exception("Unknown host platform");
			}

			return DirectoryReference.Combine(EngineDirectory, "Binaries", "ThirdParty", "DotNet", HostDotNetDirectoryName);
		}

		/// <summary>
		/// The full name of the root UE directory
		/// </summary>
		public static readonly DirectoryReference RootDirectory = FindRootDirectory();

		/// <summary>
		/// The full name of the Engine directory
		/// </summary>
		public static readonly DirectoryReference EngineDirectory = DirectoryReference.Combine(RootDirectory, "Engine");

		/// <summary>
		/// The path to UBT
		/// </summary>
		public static readonly FileReference UnrealBuildToolPath = FindUnrealBuildTool();

		/// <summary>
		/// The path to UBT
		/// </summary>
		public static readonly FileReference UnrealBuildToolDllPath = FindUnrealBuildTool().ChangeExtension(".dll");

		/// <summary>
		/// The directory containing the bundled .NET installation
		/// </summary>
		static public readonly DirectoryReference DotnetDirectory = FindDotnetDirectory();

		/// <summary>
		/// The path of the bundled dotnet executable
		/// </summary>
		static public readonly FileReference DotnetPath = FileReference.Combine(DotnetDirectory, "dotnet" + RuntimePlatform.ExeExtension);

		/// <summary>
		/// Whether we're running with engine installed
		/// </summary>
		static private bool? bIsEngineInstalled;

		/// <summary>
		/// Returns true if UnrealBuildTool is running using installed Engine components
		/// </summary>
		/// <returns>True if running using installed Engine components</returns>
		static public bool IsEngineInstalled()
		{
			if (!bIsEngineInstalled.HasValue)
			{
				bIsEngineInstalled = FileReference.Exists(FileReference.Combine(EngineDirectory, "Build", "InstalledBuild.txt"));
			}
			return bIsEngineInstalled.Value;
		}

		public static class LocationOverride 
		{
			/// <summary>
			/// If set, this value will be used to populate Unreal.RootDirectory
			/// </summary>
			public static DirectoryReference? RootDirectory = null;
		}


		// A subset of the functionality in DataDrivenPlatformInfo.GetAllPlatformInfos() - finds the DataDrivenPlatformInfo.ini files and records their existence, but does not parse them
		// (perhaps DataDrivenPlatformInfo.GetAllPlatformInfos() could be modified to use this data to avoid an additional search through the filesystem)
		public static HashSet<string>? IniPresentForPlatform = null;
		private static bool DataDrivenPlatformInfoIniIsPresent(string PlatformName)
		{
			if (IniPresentForPlatform == null)
			{
				IniPresentForPlatform = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

				// find all platform directories (skipping NFL/NoRedist)
				foreach (DirectoryReference EngineConfigDir in GetExtensionDirs(Unreal.EngineDirectory, "Config", bIncludeRestrictedDirectories: false))
				{
					// look through all config dirs looking for the data driven ini file
					foreach (string FilePath in Directory.EnumerateFiles(EngineConfigDir.FullName, "DataDrivenPlatformInfo.ini", SearchOption.AllDirectories))
					{
						FileReference FileRef = new FileReference(FilePath);

						// get the platform name from the path
						string IniPlatformName;
						if (FileRef.IsUnderDirectory(DirectoryReference.Combine(Unreal.EngineDirectory, "Config")))
						{
							// Foo/Engine/Config/<Platform>/DataDrivenPlatformInfo.ini
							IniPlatformName = Path.GetFileName(Path.GetDirectoryName(FilePath))!;
						}
						else
						{
							// Foo/Engine/Platforms/<Platform>/Config/DataDrivenPlatformInfo.ini
							IniPlatformName = Path.GetFileName(Path.GetDirectoryName(Path.GetDirectoryName(FilePath)))!;
						}

						// DataDrivenPlatformInfo.GetAllPlatformInfos() checks that [DataDrivenPlatformInfo] section exists as part of validating that the file exists
						// This code should probably behave the same way.

						IniPresentForPlatform.Add(IniPlatformName);
					}
				}
			}
			return IniPresentForPlatform.Contains(PlatformName);
		}

		// cached dictionary of BaseDir to extension directories
		private static Dictionary<DirectoryReference, Tuple<List<DirectoryReference>, List<DirectoryReference>>> CachedExtensionDirectories = new Dictionary<DirectoryReference, Tuple<List<DirectoryReference>, List<DirectoryReference>>>();

		/// <summary>
		/// Finds all the extension directories for the given base directory. This includes platform extensions and restricted folders.
		/// </summary>
		/// <param name="BaseDir">Location of the base directory</param>
		/// <param name="bIncludePlatformDirectories">If true, platform subdirectories are included (will return platform directories under Restricted dirs, even if bIncludeRestrictedDirectories is false)</param>
		/// <param name="bIncludeRestrictedDirectories">If true, restricted (NotForLicensees, NoRedist) subdirectories are included</param>
		/// <param name="bIncludeBaseDirectory">If true, BaseDir is included</param>
		/// <returns>List of extension directories, including the given base directory</returns>
		public static List<DirectoryReference> GetExtensionDirs(DirectoryReference BaseDir, bool bIncludePlatformDirectories=true, bool bIncludeRestrictedDirectories=true, bool bIncludeBaseDirectory=true)
		{
			Tuple<List<DirectoryReference>, List<DirectoryReference>>? CachedDirs;
			if (!CachedExtensionDirectories.TryGetValue(BaseDir, out CachedDirs))
			{
				CachedDirs = Tuple.Create(new List<DirectoryReference>(), new List<DirectoryReference>());

				CachedExtensionDirectories[BaseDir] = CachedDirs;

				DirectoryReference PlatformExtensionBaseDir = DirectoryReference.Combine(BaseDir, "Platforms");
				if (DirectoryReference.Exists(PlatformExtensionBaseDir))
				{
					CachedDirs.Item1.AddRange(DirectoryReference.EnumerateDirectories(PlatformExtensionBaseDir));
				}

				DirectoryReference RestrictedBaseDir = DirectoryReference.Combine(BaseDir, "Restricted");
				if (DirectoryReference.Exists(RestrictedBaseDir))
				{
					IEnumerable<DirectoryReference> RestrictedDirs = DirectoryReference.EnumerateDirectories(RestrictedBaseDir);
					CachedDirs.Item2.AddRange(RestrictedDirs);

					// also look for nested platforms in the restricted
					foreach (DirectoryReference RestrictedDir in RestrictedDirs)
					{
						DirectoryReference RestrictedPlatformExtensionBaseDir = DirectoryReference.Combine(RestrictedDir, "Platforms");
						if (DirectoryReference.Exists(RestrictedPlatformExtensionBaseDir))
						{
							CachedDirs.Item1.AddRange(DirectoryReference.EnumerateDirectories(RestrictedPlatformExtensionBaseDir));
						}
					}
				}

				// remove any platform directories in non-engine locations if the engine doesn't have the platform 
				if (BaseDir != Unreal.EngineDirectory && CachedDirs.Item1.Count > 0)
				{
					// if the DDPI.ini file doesn't exist, we haven't synced the platform, so just skip this directory
					CachedDirs.Item1.RemoveAll(x => !DataDrivenPlatformInfoIniIsPresent(x.GetDirectoryName()));
				}
			}

			// now return what the caller wanted (always include BaseDir)
			List<DirectoryReference> ExtensionDirs = new List<DirectoryReference>();
			if (bIncludeBaseDirectory)
			{
				ExtensionDirs.Add(BaseDir);
			}
			if (bIncludePlatformDirectories)
			{
				ExtensionDirs.AddRange(CachedDirs.Item1);
			}
			if (bIncludeRestrictedDirectories)
			{
				ExtensionDirs.AddRange(CachedDirs.Item2);
			}
			return ExtensionDirs;
		}

		/// <summary>
		/// Finds all the extension directories for the given base directory. This includes platform extensions and restricted folders.
		/// </summary>
		/// <param name="BaseDir">Location of the base directory</param>
		/// <param name="SubDir">The subdirectory to find</param>
		/// <param name="bIncludePlatformDirectories">If true, platform subdirectories are included (will return platform directories under Restricted dirs, even if bIncludeRestrictedDirectories is false)</param>
		/// <param name="bIncludeRestrictedDirectories">If true, restricted (NotForLicensees, NoRedist) subdirectories are included</param>
		/// <param name="bIncludeBaseDirectory">If true, BaseDir is included</param>
		/// <returns>List of extension directories, including the given base directory</returns>
		public static List<DirectoryReference> GetExtensionDirs(DirectoryReference BaseDir, string SubDir, bool bIncludePlatformDirectories=true, bool bIncludeRestrictedDirectories=true, bool bIncludeBaseDirectory=true)
		{
			return GetExtensionDirs(BaseDir, bIncludePlatformDirectories, bIncludeRestrictedDirectories, bIncludeBaseDirectory).Select(x => DirectoryReference.Combine(x, SubDir)).Where(x => DirectoryReference.Exists(x)).ToList();
		}
	}
}
