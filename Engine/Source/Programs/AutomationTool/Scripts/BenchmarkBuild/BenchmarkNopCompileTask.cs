// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	class BenchmarkNopCompileTask : BenchmarkBuildTask
	{
		public BenchmarkNopCompileTask(FileReference InProjectFile, string InTarget, UnrealTargetPlatform InPlatform, XGETaskOptions InXGEOptions)
			: base(InProjectFile, InTarget, InPlatform, InXGEOptions, "", 0)
		{
			TaskModifiers.Add("nopcompile");
		}
	}
}
