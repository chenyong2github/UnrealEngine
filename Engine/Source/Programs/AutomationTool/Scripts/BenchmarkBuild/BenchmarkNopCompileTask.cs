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
	class BenchmarkNopCompileTask : BenchmarkBuildTask
	{
		BenchmarkBuildTask PreTask;

		public BenchmarkNopCompileTask(string InProject, string InTarget, UnrealTargetPlatform InPlatform, BuildOptions InOptions)
			: base(InProject, InTarget, InPlatform, InOptions)
		{
			PreTask = new BenchmarkBuildTask(InProject, InTarget, InPlatform, InOptions);
			TaskModifiers.Add("nopcompile");
		}

		protected override bool PerformPrequisites()
		{
			if (!base.PerformPrequisites())
			{
				return false;
			}

			PreTask.Run();
						
			return !PreTask.Failed;
		}
	}
}
