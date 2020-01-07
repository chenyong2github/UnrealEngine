// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using System.Threading;
using System.Text.RegularExpressions;
using System.Linq;

namespace Gauntlet
{
	public class LuminBuild : IBuild
	{
		public UnrealTargetConfiguration Configuration { get; protected set; }

		public string SourceMpkPath;

		public string LuminPackageName;
		public BuildFlags Flags { get; protected set; }

		public UnrealTargetPlatform Platform { get { return UnrealTargetPlatform.Lumin; } }

		public LuminBuild(UnrealTargetConfiguration InConfig, string InLuminPackageName, string InMpkPath, BuildFlags InFlags)
		{
			Configuration = InConfig;
			LuminPackageName = InLuminPackageName;
			SourceMpkPath = InMpkPath;
			Flags = InFlags;
		}

		public bool CanSupportRole(UnrealTargetRole RoleType)
		{
			if (RoleType.IsClient())
			{
				return true;
			}

			return false;
		}

		public static IEnumerable<LuminBuild> CreateFromPath(string InProjectName, string InPath)
		{
			string BuildPath = InPath;

			List<LuminBuild> DiscoveredBuilds = new List<LuminBuild>();

			DirectoryInfo Di = new DirectoryInfo(BuildPath);

			if (Di.Exists)
			{
				// find all install batchfiles
				FileInfo[] InstallFiles = Di.GetFiles("Install_*");

				foreach (FileInfo Fi in InstallFiles)
				{
					Log.Verbose("Pulling install data from {0}", Fi.FullName);

					string AbsPath = Fi.Directory.FullName;

					// read contents and replace linefeeds (regex doesn't stop on them :((
					string BatContents = File.ReadAllText(Fi.FullName).Replace(Environment.NewLine, "\n");

					// Replace .bat with .mpk and strip up to and including the first _, that is then our MPK name
					string SourceMpkPath = Regex.Replace(Fi.Name, ".bat", ".mpk", RegexOptions.IgnoreCase);
					SourceMpkPath = SourceMpkPath.Substring(SourceMpkPath.IndexOf("_") + 1);
					SourceMpkPath = Path.Combine(AbsPath, SourceMpkPath);

					Match Info = Regex.Match(BatContents, @"install\s+(-u+)\s""%~dp0\\(.+)""");
					string LuminPackageName = Info.Groups[2].ToString();

					if (string.IsNullOrEmpty(SourceMpkPath))
					{
						Log.Warning("No MPK found for build at {0}", Fi.FullName);
						continue;
					}

					if (string.IsNullOrEmpty(LuminPackageName))
					{
						Log.Warning("No product name found for build at {0}", Fi.FullName);
						continue;
					}

					UnrealTargetConfiguration UnrealConfig = UnrealHelpers.GetConfigurationFromExecutableName(InProjectName, Fi.Name);

					// Lumin builds are always packaged, and we can always replace the command line
					BuildFlags Flags = BuildFlags.Packaged | BuildFlags.CanReplaceCommandLine;

					if (AbsPath.Contains("Bulk"))
					{
						Flags |= BuildFlags.Bulk;
					}
					else
					{
						Flags |= BuildFlags.NotBulk;
					}

					LuminBuild NewBuild = new LuminBuild(UnrealConfig, LuminPackageName, SourceMpkPath, Flags);

					DiscoveredBuilds.Add(NewBuild);

					Log.Verbose("Found {0} {1} build at {2}", UnrealConfig, ((Flags & BuildFlags.Bulk) == BuildFlags.Bulk) ? "(bulk)" : "(not bulk)", AbsPath);

				}
			}

			return DiscoveredBuilds;
		}
	}
	
	public class LuminBuildSource : IFolderBuildSource
	{	
		public string BuildName { get { return "LuminBuildSource";  } }

		public bool CanSupportPlatform(UnrealTargetPlatform InPlatform)
		{
			return InPlatform == UnrealTargetPlatform.Lumin;
		}

		public string ProjectName { get; protected set; }

		public LuminBuildSource()
		{
		}

		public List<IBuild> GetBuildsAtPath(string InProjectName, string InPath, int MaxRecursion = 3)
		{		
			List<IBuild> Builds = new List<IBuild>();

			if (Directory.Exists(InPath))
			{
				// First try creating a build just from this path
				IEnumerable<LuminBuild> FoundBuilds = LuminBuild.CreateFromPath(InProjectName, InPath);

				// If we found one we were pointed at a specific case so we're done
				if (FoundBuilds.Any())
				{
					Builds.AddRange(FoundBuilds);
				}
				else
				{
					// Ok, find all directories that match our platform and add those
					DirectoryInfo TopDir = new DirectoryInfo(InPath);

					// find all directories that begin with Lumin
					IEnumerable<string> LuminDirs = Directory.EnumerateDirectories(InPath, "Lumin*", SearchOption.AllDirectories);

					foreach (string DirPath in LuminDirs)
					{
						FoundBuilds = LuminBuild.CreateFromPath(InProjectName, DirPath);
						Builds.AddRange(FoundBuilds);
					}
				}
			}

			return Builds;
		}
	}
}