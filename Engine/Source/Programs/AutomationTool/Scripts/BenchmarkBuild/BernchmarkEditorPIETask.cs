using System;
using System.Collections.Generic;
using System.Text;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	class BenchmarPIEEditorTask : BenchmarkEditorStartupTask
	{
		public BenchmarPIEEditorTask(ProjectTargetInfo InTargetInfo, ProjectTaskOptions InOptions, bool InSkipBuild)
			: base(InTargetInfo, InOptions, InSkipBuild)
		{
			TaskName = string.Format("{0} Start PIE", ProjectName, BuildHostPlatform.Current.Platform);
		}

		protected override string GetEditorTaskArgs()
		{
			return "-execcmds=\"automation IgnoreLogEvents;runtest Project.Maps.PIE;Quit\"";
		}
	}
}
