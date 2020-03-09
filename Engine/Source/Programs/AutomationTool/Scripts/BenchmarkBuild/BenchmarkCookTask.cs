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

		public BenchmarkCookTask(string InProject, UnrealTargetPlatform InPlatform, EditorTaskOptions InOptions, string InCookArgs)
			: base(InProject, InOptions)
		{
			CookArgs = InCookArgs;

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

			if (TaskOptions.HasFlag(EditorTaskOptions.NoDDC))
			{
				TaskModifiers.Add("noddc");
			}

			if (TaskOptions.HasFlag(EditorTaskOptions.NoShaderDDC))
			{
				TaskModifiers.Add("noshaderddc");
			}

			if (TaskOptions.HasFlag(EditorTaskOptions.ColdDDC))
			{
				TaskModifiers.Add("coldddc");
			}

			if (!string.IsNullOrEmpty(CookArgs))
			{
				TaskModifiers.Add(CookArgs);
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
			if (TaskOptions.HasFlag(EditorTaskOptions.HotDDC))
			{
				// will throw an exception if it fails
				CommandUtils.RunCommandlet(ProjectFile, "UE4Editor-Cmd.exe", "Cook", String.Format("-TargetPlatform={0} ", CookPlatformName));
			}

			if (TaskOptions.HasFlag(EditorTaskOptions.ColdDDC))
			{
				//DeleteLocalDDC(ProjectFile);
			}

			base.PerformPrequisites();

			return true;
		}

		protected override bool PerformTask()
		{
			List<string> ExtraArgsList = new List<string>();
			
			if (TaskOptions.HasFlag(EditorTaskOptions.CookClient))
			{
				ExtraArgsList.Add("client");
			}

			if (TaskOptions.HasFlag(EditorTaskOptions.NoShaderDDC))
			{
				ExtraArgsList.Add("noshaderddc");
				//ExtraArgs.Add("noxgeshadercompile");
			}

			if (TaskOptions.HasFlag(EditorTaskOptions.NoDDC))
			{
				ExtraArgsList.Add("ddc=noshared");
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
