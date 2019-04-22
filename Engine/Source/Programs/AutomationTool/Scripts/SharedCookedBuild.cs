// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Reflection;
using System.Linq;
using System.Threading.Tasks;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;
using System.Text.RegularExpressions;

public class SharedCookedBuild
{
	private const string SyncedBuildFileName = "SyncedBuild.txt";

	/// <summary>
	/// Types of shared cook base builds
	/// </summary>
	public enum SharedCookType
	{
		/// <summary>
		/// Only allow shared cook build of version identical to local sync
		/// </summary>
		Exact,

		/// <summary>
		/// Allow any previous version that is only a content change from local sync
		/// </summary>
		Content,
		
		/// <summary>
		/// Closest previous version, regardless of code/content changes
		/// </summary>
		Any,
	}
		
	public static void CopySharedCookedBuild(ProjectParams Params)
	{
		foreach (TargetPlatformDescriptor ClientPlatform in Params.ClientTargetPlatforms)
		{
			TargetPlatformDescriptor DataPlatformDesc = Params.GetCookedDataPlatformForClientTarget(ClientPlatform);
			string PlatformToCook = Platform.Platforms[DataPlatformDesc].GetCookPlatform(false, Params.Client);
			UnrealTargetPlatform TargetPlatform = (UnrealTargetPlatform)Enum.Parse(typeof(UnrealTargetPlatform), PlatformToCook, true);
			DirectoryReference InstallPath = DirectoryReference.Combine(Params.RawProjectPath.Directory, "Saved", "SharedIterativeBuild", PlatformToCook);
			SharedCookType BuildType = (SharedCookType)Enum.Parse(typeof(SharedCookType), Params.IterateSharedCookedBuild, true);

			CopySharedCookedBuild(Params.RawProjectPath.FullName, TargetPlatform, BuildType, true);
		}
	}

	public static void CopySharedCookedBuild(string ProjectFullPath, UnrealTargetPlatform TargetPlatform, SharedCookType BuildType, bool bAllowExistingBuild = true)
	{
		DirectoryReference InstallPath = DirectoryReference.Combine(new FileReference(ProjectFullPath).Directory, "Saved", "SharedIterativeBuild", TargetPlatform.ToString());
		List<ISharedCookedBuild> SharedCookedBuilds = FindBestBuilds(ProjectFullPath, TargetPlatform, BuildType, true);
		foreach (ISharedCookedBuild Build in SharedCookedBuilds)
		{
			if (Build.CopyBuild(InstallPath))
			{
				return;
			}
		}

		throw new AutomationException("Failed to install shared cooked build");
	}

	public static BuildVersion LocalSync()
	{
		BuildVersion P4Version = new BuildVersion();
		if (CommandUtils.P4Enabled)
		{
			P4Version.BranchName = CommandUtils.P4Env.Branch.Replace("/", "+");
			P4Version.Changelist = CommandUtils.P4Env.Changelist;
			P4Version.CompatibleChangelist = CommandUtils.P4Env.CodeChangelist;
		}

		BuildVersion UGSVersion;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out UGSVersion))
		{
			return UGSVersion;
		}
		
		if (!CommandUtils.P4Enabled)
		{
			throw new AutomationException("Cannot determine local sync");
		}

		return P4Version;
	}

	public static List<ISharedCookedBuild> FindBestBuilds(string ProjectFullPath, UnrealTargetPlatform TargetPlatform, SharedCookType BuildType, bool bAllowExistingBuild = true)
	{
		// Attempt manifest searching first
		FileReference ProjectFileRef = new FileReference(ProjectFullPath);
		if (!FileReference.Exists(ProjectFileRef))
		{
			throw new AutomationException("Cannot locate project file: {0}", ProjectFileRef);
		}

		ConfigHierarchy Herarchy = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirectoryReference.FromFile(ProjectFileRef), TargetPlatform);
		List<string> CookedBuildManifestPaths = null;
		List<string> CookedBuildStagedPaths = null;
		Herarchy.GetArray("SharedCookedBuildSettings", "SharedCookedManifestPath", out CookedBuildManifestPaths);
		Herarchy.GetArray("SharedCookedBuildSettings", "SharedCookedBuildPath", out CookedBuildStagedPaths);
		if (CookedBuildManifestPaths == null && CookedBuildStagedPaths == null)
		{
			throw new AutomationException("Unable to locate shared cooked builds. SharedCookedManifestPath and SharedCookedBuildPath not set in Engine.ini [SharedCookedBuildSettings]");
		}

		BuildVersion Version = LocalSync();
		List<ISharedCookedBuild> CandidateBuilds = new List<ISharedCookedBuild>();

		if (bAllowExistingBuild)
		{
			// If existing sync is present, stick to it. Read version out of sync file
			FileReference SyncedBuildFile = new FileReference(CommandUtils.CombinePaths(Path.GetDirectoryName(ProjectFullPath), "Saved", "SharedIterativeBuild", TargetPlatform.ToString(), SyncedBuildFileName));
			if (FileReference.Exists(SyncedBuildFile))
			{
				string[] SyncedBuildInfo = FileReference.ReadAllLines(SyncedBuildFile);
				int SyncedCL = int.Parse(SyncedBuildInfo[0]);
				if (IsValidCL(SyncedCL, BuildType, Version))
				{
					CandidateBuilds.Add(new ExistingSharedCookedBuild { CL = SyncedCL });
				}
			}
		}

		if (CookedBuildManifestPaths.Count > 0)
		{
			foreach (string ManifestPath in CookedBuildManifestPaths)
			{
				ISharedCookedBuild Candidate = FindBestManifestBuild(ManifestPath, TargetPlatform, BuildType, Version);
				if (Candidate != null)
				{
					CandidateBuilds.Add(Candidate);
				}
			}
		}

		if (CookedBuildStagedPaths.Count > 0)
		{
			foreach (string StagedPath in CookedBuildStagedPaths)
			{
				ISharedCookedBuild Candidate = FindBestLooseBuild(StagedPath, TargetPlatform, BuildType, Version);
				if (Candidate != null)
				{
					CandidateBuilds.Add(Candidate);
				}
			}
		}

		if (CandidateBuilds.Count == 0)
		{
			CommandUtils.LogInformation("Could not locate valid shared cooked build");
		}

		return CandidateBuilds.OrderBy(x => x.CL).ToList();
	}

	public static ISharedCookedBuild FindBestManifestBuild(string Path, UnrealTargetPlatform TargetPlatform, SharedCookType BuildType, BuildVersion Version)
	{
		Tuple<string, string> SplitPath = SplitOnFixedPrefix(Path);
		Regex Pattern = RegexFromWildcards(SplitPath.Item2, Version, TargetPlatform);

		ManifestSharedCookedBuild Build = new ManifestSharedCookedBuild { CL = 0, Manifest = null };
		DirectoryReference SearchDir = new DirectoryReference(SplitPath.Item1);
		if (DirectoryReference.Exists(SearchDir))
		{
			foreach (FileReference File in DirectoryReference.EnumerateFiles(SearchDir))
			{
				Match Match = Pattern.Match(File.FullName);
				if (Match.Success)
				{
					int MatchCL = int.Parse(Match.Result("${CL}"));
					if (IsValidCL(MatchCL, BuildType, Version) && MatchCL >= Build.CL)
					{
						Build = new ManifestSharedCookedBuild { CL = MatchCL, Manifest = File };
					}
				}
			}
		}

		if (Build.CL != 0)
		{
			return Build;
		}

		return null;
	}
	public static ISharedCookedBuild FindBestLooseBuild(string Path, UnrealTargetPlatform TargetPlatform, SharedCookType BuildType, BuildVersion Version)
	{
		Tuple<string, string> SplitPath = SplitOnFixedPrefix(Path);
		Regex Pattern = RegexFromWildcards(SplitPath.Item2, Version, TargetPlatform);
		LooseSharedCookedBuild Build = new LooseSharedCookedBuild { CL = 0, Path = null };

		// Search for all available builds
		const string MetaDataFilename = "\\Metadata\\DevelopmentAssetRegistry.bin";
		string BuildRule = Path + MetaDataFilename;
		BuildRule = BuildRule.Replace("[BRANCHNAME]", Version.BranchName);
		BuildRule = BuildRule.Replace("[PLATFORM]", TargetPlatform.ToString());
		string IncludeRule = BuildRule.Replace("[CL]", "*");
		string ExcludeRule = BuildRule.Replace("[CL]", "*-PF-*"); // Exclude preflights
		FileFilter BuildSearch = new FileFilter();
		BuildSearch.AddRule(IncludeRule);
		BuildSearch.AddRule(ExcludeRule, FileFilterType.Exclude);
		
		foreach (FileReference CandidateBuild in BuildSearch.ApplyToDirectory(new DirectoryReference(SplitPath.Item1), false))
		{
			string BaseBuildPath = CandidateBuild.FullName.Replace(MetaDataFilename, "");
			Match Match = Pattern.Match(BaseBuildPath);
			if (Match.Success)
			{
				int MatchCL = int.Parse(Match.Result("${CL}"));
				if (IsValidCL(MatchCL, BuildType, Version) && MatchCL >= Build.CL)
				{
					Build = new LooseSharedCookedBuild { CL = MatchCL, Path = new DirectoryReference(BaseBuildPath) };
				}
			}
		}

		if (Build.CL != 0)
		{
			return Build;
		}

		return null;
	}


	private static bool IsValidCL(int CL, SharedCookType BuildType, BuildVersion Version)
	{
		if (BuildType == SharedCookType.Exact && CL == Version.Changelist)
		{
			return true;
		}
		else if (BuildType == SharedCookType.Content && CL >= Version.EffectiveCompatibleChangelist && CL <= Version.Changelist)
		{
			return true;
		}
		else if (BuildType == SharedCookType.Any && CL <= Version.Changelist)
		{
			return true;
		}

		return false;
	}

	private static Regex RegexFromWildcards(string Path, BuildVersion Version, UnrealTargetPlatform TargetPlatform)
	{
		string Pattern = Path.Replace(@"\", @"\\");
		Pattern = Pattern.Replace("[BRANCHNAME]", Version.BranchName.Replace(@"+", @"\+"));
		Pattern = Pattern.Replace("[PLATFORM]", TargetPlatform.ToString());
		Pattern = Pattern.Replace("[CL]", @"(?<CL>\d+)");
		return new Regex(Pattern);
	}

	private static Tuple<string, string> SplitOnFixedPrefix(string Path)
	{
		int IndexOfFirstParam = Path.IndexOf("[");
		int PrefixStart = Path.LastIndexOf(@"\", IndexOfFirstParam);
		return new Tuple<string, string>(Path.Substring(0, PrefixStart), Path.Substring(PrefixStart));
	}

	public interface ISharedCookedBuild
	{
		int CL { get; }
		bool CopyBuild(DirectoryReference InstallPath);
	}

	private class ManifestSharedCookedBuild : ISharedCookedBuild
	{
		public int CL { get; set; }
		public FileReference Manifest { get; set; }

		public bool CopyBuild(DirectoryReference InstallPath)
		{
			CommandUtils.LogInformation("Installing shared cooked build from manifest: {0} to {1}", Manifest.FullName, InstallPath.FullName);

			FileReference PreviousManifest = FileReference.Combine(InstallPath, ".build", "Current.manifest");

			FileReference BPTI = FileReference.Combine(CommandUtils.RootDirectory, "Engine", "Binaries", "Win64", "NotForLicensees", "BuildPatchToolInstaller.exe");
			if (!FileReference.Exists(BPTI))
			{
				CommandUtils.LogInformation("Could not locate BuildPatchToolInstaller.exe");
				return false;
			}

			bool PreviousManifestExists = FileReference.Exists(PreviousManifest);
			if (!PreviousManifestExists && DirectoryReference.Exists(InstallPath))
			{
				DirectoryReference.Delete(InstallPath, true);
			}

			IProcessResult Result = CommandUtils.Run(BPTI.FullName, string.Format("-Manifest={0} -OutputDir={1}", Manifest.FullName, InstallPath.FullName), null, CommandUtils.ERunOptions.AllowSpew);
			if (Result.ExitCode != 0)
			{
				CommandUtils.LogWarning("Failed to install manifest {0} to {1}", Manifest.FullName, InstallPath.FullName);
				return false;
			}

			FileReference SyncedBuildFile = new FileReference(CommandUtils.CombinePaths(InstallPath.FullName, SyncedBuildFileName));
			FileReference.WriteAllLines(SyncedBuildFile, new string[] { CL.ToString(), Manifest.FullName });

			return true;
		}
	}

	private class LooseSharedCookedBuild : ISharedCookedBuild
	{
		public int CL { get; set; }
		public DirectoryReference Path { get; set; }
		public bool CopyBuild(DirectoryReference InstallPath)
		{
			CommandUtils.LogInformation("Copying shared cooked build from stage directory: {0} to {1}", Path.FullName, InstallPath.FullName);

			// Delete existing
			if (DirectoryReference.Exists(InstallPath))
			{
				DirectoryReference.Delete(InstallPath, true);
			}
			DirectoryReference.CreateDirectory(InstallPath);

			// Copy new
			if (!CommandUtils.CopyDirectory_NoExceptions(Path.FullName, InstallPath.FullName))
			{
				CommandUtils.LogWarning("Failed to copy {0} -> {1}", Path.FullName, InstallPath.FullName);
				return false;
			}
			FileReference SyncedBuildFile = new FileReference(CommandUtils.CombinePaths(InstallPath.FullName, SyncedBuildFileName));
			FileReference.WriteAllLines(SyncedBuildFile, new string[] { CL.ToString(), Path.FullName });

			return true;
		}
	}

	private class ExistingSharedCookedBuild : ISharedCookedBuild
	{
		public int CL { get; set; }
		public bool CopyBuild(DirectoryReference InstallPath)
		{
			CommandUtils.LogInformation("Using previously synced shared cooked build");
			return true;
		}
	}
}