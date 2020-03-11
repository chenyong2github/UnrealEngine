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
	class BenchmarkSingleCompileTask : BenchmarkBuildTask
	{
		BenchmarkBuildTask PreTask;

		FileReference SourceFile;

		public BenchmarkSingleCompileTask(FileReference InProjectFile, string InTarget, UnrealTargetPlatform InPlatform, FileReference InSourceFile, BuildOptions InOptions)
			: base(InProjectFile, InTarget, InPlatform, InOptions)
		{
			PreTask = new BenchmarkBuildTask(InProjectFile, InTarget, InPlatform, InOptions);
			SourceFile = InSourceFile;
			TaskModifiers.Add("singlecompile");
		}

		protected override bool PerformPrequisites()
		{
			if (!base.PerformPrequisites())
			{
				return false;
			}

			PreTask.Run();

			FileInfo Fi = SourceFile.ToFileInfo();

			bool ReadOnly = Fi.IsReadOnly;

			if (ReadOnly)
			{
				Fi.IsReadOnly = false;
			}

			Fi.LastWriteTime = DateTime.Now;

			if (ReadOnly)
			{
				Fi.IsReadOnly = true;
			}

			return !PreTask.Failed;
		}
	}
}
