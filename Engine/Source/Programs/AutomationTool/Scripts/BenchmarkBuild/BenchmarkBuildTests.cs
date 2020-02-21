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
		NoAcceleration = 1 << 1,
	}

	/// <summary>
	/// Task that builds a target
	/// </summary>
	class BenchmarkBuildTask : BenchmarkTaskBase
	{
		private BuildTarget Command;

		public static bool SupportsAcceleration
		{
			get
			{
				return BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64 ||
					(BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac);
			}
		}

		public static string AccelerationName
		{
			get
			{
				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
				{
					return "XGE";
				}
				else if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Mac)
				{
					return "FASTBuild";
				}
				else
				{
					return "none";
				}
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

			Command.UBTArgs = "";

			bool WithAccel = !InOptions.HasFlag(BuildOptions.NoAcceleration);

			if (!WithAccel || !SupportsAcceleration)
			{
				string Arg = string.Format("No{0}", AccelerationName);

				Command.UBTArgs += " -" + Arg;
				TaskModifiers.Add(Arg);
				Command.Params = new[] { Arg };	// need to also pass it to this
			}
			else
			{
				TaskModifiers.Add(AccelerationName);
			}			
		}


		protected override bool PerformTask()
		{
			ExitCode Result = Command.Execute();

			return Result == ExitCode.Success;
		}
	}
}
