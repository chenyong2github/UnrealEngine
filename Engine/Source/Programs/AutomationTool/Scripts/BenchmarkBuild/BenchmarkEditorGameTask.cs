using System;
using System.Collections.Generic;
using System.Text;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	class BenchmarkEditorGameTask : BenchmarkEditorStartupTask
	{
		public BenchmarkEditorGameTask(ProjectTargetInfo InTargetInfo, ProjectTaskOptions InOptions, bool InSkipBuild)
			: base(InTargetInfo, InOptions, InSkipBuild)
		{
			TaskName = string.Format("{0} EditorGame", ProjectName, BuildHostPlatform.Current.Platform);
		}

		protected override string GetEditorTaskArgs()
		{
			return "-windowed -game -execcmds=\"automation Quit\"";
		}
	}
}
