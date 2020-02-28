// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	[Flags]
	public enum BuildOptions
	{
		None = 0,
		Clean = 1 << 0,
		NoXGE = 1 << 1,
	}

	/// <summary>
	/// Task that builds a target
	/// </summary>
	class BenchmarkBuildTask : BenchmarkTaskBase
	{
		protected string TaskName;

		private BuildTarget Command;

		public static bool SupportsXGE
		{
			get
			{
				return BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64;
			}
		}

		public BenchmarkBuildTask(string InProject, string InTarget, UnrealTargetPlatform InPlatform, BuildOptions InOptions)
		{
			bool IsVanillaUE4 = InProject == null || string.Equals(InProject, "UE4", StringComparison.OrdinalIgnoreCase);

			string ModuleName = IsVanillaUE4 ? "UE4" : InProject;

			TaskName = string.Format("{0} {1} {2}", ModuleName, InTarget, InPlatform);

			Command = new BuildTarget();
			Command.ProjectName = IsVanillaUE4 ? null : InProject;
			Command.Platforms = InPlatform.ToString();
			Command.Targets = InTarget;
			Command.NoTools = true;
			Command.Clean = InOptions.HasFlag(BuildOptions.Clean);

			List<string> Args = new List<string>();

			bool WithXGE = !InOptions.HasFlag(BuildOptions.NoXGE);

			if (!WithXGE || !SupportsXGE)
			{
				Args.Add("NoXGE");
				TaskName += " (noxge)";
			}
			else
			{
				TaskName += " (xge)";
			}

			Command.Params = Args.ToArray();
		}

		public override string GetTaskName()
		{
			return TaskName;
		}

		protected override bool PerformTask()
		{
			ExitCode Result = Command.Execute();

			return Result == ExitCode.Success;
		}
	}
}
