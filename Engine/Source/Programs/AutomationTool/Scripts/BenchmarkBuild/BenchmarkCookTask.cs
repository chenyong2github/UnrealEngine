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


	class BenchmarkCookTask : BenchmarkEditorTaskBase
	{
		string			CookPlatformName;

		string			CookArgs;

		bool			CookAsClient;

		public BenchmarkCookTask(FileReference InProjectFile, UnrealTargetPlatform InPlatform, bool bCookAsClient, DDCTaskOptions InOptions, string InCookArgs)
			: base(InProjectFile, InOptions, InCookArgs)
		{
			CookArgs = InCookArgs;
			CookAsClient = bCookAsClient;

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

			TaskName = string.Format("Cook {0} {1}", ProjectName, CookPlatformName);			
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
			if (TaskOptions.HasFlag(DDCTaskOptions.HotDDC))
			{
				// will throw an exception if it fails
				CommandUtils.RunCommandlet(ProjectFile, "UE4Editor-Cmd.exe", "Cook", String.Format("-TargetPlatform={0} ", CookPlatformName));
			}

			base.PerformPrequisites();
			return true;
		}

		protected override bool PerformTask()
		{
			List<string> ExtraArgsList = new List<string>();
			
			if (CookAsClient)
			{
				ExtraArgsList.Add("client");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.NoShaderDDC))
			{
				ExtraArgsList.Add("noshaderddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.NoXGE))
			{
				ExtraArgsList.Add("noxgeshadercompile");
			}

			string ExtraArgs = "";

			if (ExtraArgsList.Any())
			{
				ExtraArgs = "-" + string.Join(" -", ExtraArgsList);
			}

			if (CookArgs.Length > 0)
			{
				ExtraArgs += " " + CookArgs;
			}

			// will throw an exception if it fails
			CommandUtils.RunCommandlet(ProjectFile, "UE4Editor-Cmd.exe", "Cook", String.Format("-TargetPlatform={0} {1}", CookPlatformName, ExtraArgs));

			return true;
		}
	}
}
