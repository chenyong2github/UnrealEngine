// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{
	class BenchmarkRunEditorTask : BenchmarkEditorTaskBase
	{
		public BenchmarkRunEditorTask(FileReference InProjectFile, DDCTaskOptions InOptions, string InEditorArgs="")
			: base(InProjectFile, InOptions, InEditorArgs)
		{
			TaskName = string.Format("{0} PIE", ProjectName, BuildHostPlatform.Current.Platform);
		}

		protected override bool PerformPrequisites()
		{
			// build editor
			BuildTarget Command = new BuildTarget();
			Command.ProjectName = ProjectFile != null ? ProjectFile.GetFileNameWithoutAnyExtensions() : null;
			Command.Platforms = BuildHostPlatform.Current.Platform.ToString();
			Command.Targets = "Editor";
			Command.NoTools = false;

			if (Command.Execute() != ExitCode.Success)
			{
				return false;

			}

			// if they want a hot DDC then do the test one time with no timing
			if (TaskOptions.HasFlag(DDCTaskOptions.HotDDC))
			{
				RunEditorAndWaitForMapLoad(true);
			}

			base.PerformPrequisites();

			return true;
		}

		static IProcessResult CurrentProcess = null;
		static DateTime LastStdOutTime = DateTime.Now;
		//static bool TestCompleted = false;

		/// <summary>
		/// A filter that suppresses all output od stdout/stderr
		/// </summary>
		/// <param name="Message"></param>
		/// <returns></returns>
		static string EndOnMapCheckFilter(string Message)
		{
			if (CurrentProcess != null)
			{
				lock (CurrentProcess)
				{
					if (Message.Contains("TEST COMPLETE"))
					{
						Log.TraceInformation("Automation test reported as complete.");
						//TestCompleted = true;
					}

					LastStdOutTime = DateTime.Now;
				}
			}
			return Message;
		}

		private static string MakeValidFileName(string name)
		{
			string invalidChars = System.Text.RegularExpressions.Regex.Escape(new string(System.IO.Path.GetInvalidFileNameChars()));
			string invalidRegStr = string.Format(@"([{0}]*\.+$)|([{0}]+)", invalidChars);

			return System.Text.RegularExpressions.Regex.Replace(name, invalidRegStr, "_");
		}

		protected bool RunEditorAndWaitForMapLoad(bool bIsWarming)
		{
			string ProjectArg = ProjectFile != null ? ProjectFile.ToString() : "";
			string EditorPath = HostPlatform.Current.GetUE4ExePath("UE4Editor.exe");
			string LogArg = string.Format("-log={0}.log", MakeValidFileName(GetFullTaskName()).Replace(" ", "_"));
			string Arguments = string.Format("{0} {1} -execcmds=\"automation runtest System.Maps.PIE;Quit\" -stdout -FullStdOutLogOutput -unattended {2}", ProjectArg, EditorArgs, LogArg);

			if (!bIsWarming && TaskOptions.HasFlag(DDCTaskOptions.NoShaderDDC))
			{
				Arguments += (" -noshaderddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.NoXGE))
			{
				Arguments += (" -noxgeshadercompile");
			}

			var RunOptions = CommandUtils.ERunOptions.AllowSpew | CommandUtils.ERunOptions.NoWaitForExit;

			var SpewDelegate = new ProcessResult.SpewFilterCallbackType(EndOnMapCheckFilter);

			//TestCompleted = false;
			LastStdOutTime = DateTime.Now;
			CurrentProcess = CommandUtils.Run(EditorPath, Arguments, Options: RunOptions, SpewFilterCallback: SpewDelegate);

			DateTime StartTime = DateTime.Now;

			int MaxWaitMins = 90;

			while (!CurrentProcess.HasExited)
			{
				Thread.Sleep(5 * 1000);

				lock (CurrentProcess)
				{
					if ((LastStdOutTime - StartTime).TotalMinutes >= MaxWaitMins)
					{
						Log.TraceError("Gave up waiting for task after {0} minutes of no output", MaxWaitMins);
						CurrentProcess.ProcessObject.Kill();
					}
				}
			}

			int ExitCode = CurrentProcess.ExitCode;
			CurrentProcess = null;

			return ExitCode == 0;
		}

		protected override bool PerformTask()
		{
			return RunEditorAndWaitForMapLoad(false);
		}
	}
}
