// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	[Flags]
	public enum CookOptions
	{
		None = 0,
		Clean			= 1 << 0,
		NoDDC			= 1 << 1,
		NoShaderDDC		= 1 << 2,
		Client			= 1 << 3,
		WarmCook		= 1 << 4,
	}

	class BenchmarkCookTask : BenchmarkTaskBase
	{
		string ProjectName;

		FileReference ProjectFile;

		string CookPlatformName;

		CookOptions Options;

		public BenchmarkCookTask(string InProject, UnrealTargetPlatform InPlatform, CookOptions InOptions)
		{
			ProjectName = InProject;
			Options = InOptions;

			ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

			var PlatformToCookPlatform = new Dictionary<UnrealTargetPlatform, string> {
				{ UnrealTargetPlatform.Win64, "WindowsClient" },
				{ UnrealTargetPlatform.Mac, "MacClient" },
				{ UnrealTargetPlatform.Linux, "LinuxClient" },
				{ UnrealTargetPlatform.Android, "Android_ASTCClient" }
			};

			CookPlatformName = InPlatform.ToString();

			if (PlatformToCookPlatform.ContainsKey(InPlatform))
			{
				CookPlatformName = PlatformToCookPlatform[InPlatform];
			}

			TaskName = string.Format("Cook {0} {1}", InProject, CookPlatformName);

			if (Options.HasFlag(CookOptions.NoDDC))
			{
				TaskModifiers.Add("noddc");
			}

			if (Options.HasFlag(CookOptions.NoShaderDDC))
			{
				TaskModifiers.Add("noshaderddc");
			}
		}

		protected override bool PerformPrequisites()
		{
			// build editor
			BuildTarget Command = new BuildTarget();
			Command.ProjectName = ProjectName;
			Command.Platforms = BuildHostPlatform.Current.Platform.ToString();
			Command.Targets = "Editor";
			
			if (Command.Execute() != ExitCode.Success)
			{
				return false;
			}

			// Do a cook to make sure the remote ddc is warm?
			if (Options.HasFlag(CookOptions.WarmCook))
			{
				// will throw an exception if it fails
				CommandUtils.RunCommandlet(ProjectFile, "UE4Editor-Cmd.exe", "Cook", String.Format("-TargetPlatform={0} ", CookPlatformName));
			}

			if (Options.HasFlag(CookOptions.Clean))
			{
				FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

				List<DirectoryReference> DirsToClear = new List<DirectoryReference>();

				DirectoryReference ProjectDir = ProjectFile.Directory;
				
				DirsToClear.Add(DirectoryReference.Combine(ProjectDir, "Saved"));
				DirsToClear.Add(DirectoryReference.Combine(CommandUtils.EngineDirectory, "DerivedDataCache"));
				DirsToClear.Add(DirectoryReference.Combine(ProjectDir, "DerivedDataCache"));

				string LocalDDC = Environment.GetEnvironmentVariable("UE-LocalDataCachePath");

				if (!string.IsNullOrEmpty(LocalDDC) && Directory.Exists(LocalDDC))
				{
					DirsToClear.Add(new DirectoryReference(LocalDDC));
				}

				foreach (var Dir in DirsToClear)
				{
					try
					{
						if (DirectoryReference.Exists(Dir))
						{
							Log.TraceInformation("Removing {0}", Dir);
							DirectoryReference.Delete(Dir, true);
						}
					}
					catch (Exception Ex)
					{
						Log.TraceWarning("Failed to remove path {0}. {1}", Dir.FullName, Ex.Message);
					}
				}
			}

			return true;
		}

		protected override bool PerformTask()
		{
			List<string> ExtraArgs = new List<string>();
			
			if (Options.HasFlag(CookOptions.Client))
			{
				ExtraArgs.Add("client");
			}

			if (Options.HasFlag(CookOptions.NoShaderDDC))
			{
				ExtraArgs.Add("noshaderddc");
				//ExtraArgs.Add("noxgeshadercompile");
			}

			if (Options.HasFlag(CookOptions.NoDDC))
			{
				ExtraArgs.Add("ddc=noshared");
			}			

			// will throw an exception if it fails
			CommandUtils.RunCommandlet(ProjectFile, "UE4Editor-Cmd.exe", "Cook", String.Format("-TargetPlatform={0} -{1}", CookPlatformName, string.Join(" -", ExtraArgs)));

			return true;
		}
	}
}
