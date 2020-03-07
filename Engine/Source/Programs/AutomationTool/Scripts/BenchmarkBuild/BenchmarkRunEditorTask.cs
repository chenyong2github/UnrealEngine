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
		FileReference ProjectFile = null;

		string Project = "";
		string EditorArgs = "";

		EditorTaskOptions Options;

		public BenchmarkRunEditorTask(string InProject, EditorTaskOptions InOptions = EditorTaskOptions.None, string InEditorArgs="")
		{
			Options = InOptions;
			EditorArgs = InEditorArgs;

			if (!InProject.Equals("UE4", StringComparison.OrdinalIgnoreCase))
			{
				Project = InProject;
				ProjectFile = ProjectUtils.FindProjectFileFromName(InProject);
			}

			if (Options.HasFlag(EditorTaskOptions.NoDDC))
			{
				TaskModifiers.Add("noddc");
			}

			if (Options.HasFlag(EditorTaskOptions.NoShaderDDC))
			{
				TaskModifiers.Add("noshaderddc");
			}

			if (Options.HasFlag(EditorTaskOptions.ColdDDC))
			{
				TaskModifiers.Add("coldddc");
			}

			if (!string.IsNullOrEmpty(EditorArgs))
			{
				TaskModifiers.Add(EditorArgs);
			}
			/*
			DirectoryReference ProjectDir = ProjectUtils.FindProjectFileFromName(InProject).Directory;
			FileReference EngineIni = FileReference.Combine(ProjectDir, "Config", "DefaultEngine.ini");

			if (!FileReference.Exists(EngineIni))
			{
				throw new AutomationException("Could not find DefaultEngine.ini for {0}", InProject);
			}

			ConfigFile Config = new ConfigFile(EngineIni);
			ConfigFileSection Section;
			if (Config.TryGetSection("/Script/Engine.AutomationTestSettings", out Section))
			{
				ConfigLine ConfigLine;
			if (!Section.TryGetLine("PIETestMapList", out ConfigLine))
			{
				throw new AutomationException("Unable to read \"Content->Label\" value from ini file:{0}", InputBuildInfoIniFile);
			}*/

			TaskName = string.Format("{0} PIE", InProject, BuildHostPlatform.Current.Platform);
		}

		protected override bool PerformPrequisites()
		{
			// build editor
			BuildTarget Command = new BuildTarget();
			Command.ProjectName = Project;
			Command.Platforms = BuildHostPlatform.Current.Platform.ToString();
			Command.Targets = "Editor";
			Command.NoTools = true;

			if (Command.Execute() != ExitCode.Success)
			{
				return false;

			}
			if (Options.HasFlag(EditorTaskOptions.ColdDDC))
			{
				DeleteLocalDDC(ProjectFile);
			}

			// if they want a hot DDC then do the test one time with no timing
			if (Options.HasFlag(EditorTaskOptions.HotDDC))
			{
				RunEditorAndWaitForMapLoad();
			}

			return true;
		}

		static IProcessResult CurrentProcess = null;
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
				if (Message.Contains("TEST COMPLETE"))
				{
					Log.TraceInformation("Automation test reported as complete.");
					//TestCompleted = true;
				}
			}
			return Message;
		}

		protected bool RunEditorAndWaitForMapLoad()
		{
			string ProjectArg = ProjectFile != null ? ProjectFile.ToString() : "";
			string EditorPath = HostPlatform.Current.GetUE4ExePath("UE4Editor.exe");
			string Arguments = string.Format("{0} {1} -execcmds=\"automation runtest System.Maps.PIE;Quit\" -stdout -AllowStdOutLogVerbosity -unattended", ProjectArg, EditorArgs);

			var RunOptions = CommandUtils.ERunOptions.AllowSpew | CommandUtils.ERunOptions.NoWaitForExit;

			var SpewDelegate = new ProcessResult.SpewFilterCallbackType(EndOnMapCheckFilter);

			//TestCompleted = false;
			CurrentProcess = CommandUtils.Run(EditorPath, Arguments, Options: RunOptions, SpewFilterCallback: SpewDelegate);

			DateTime StartTime = DateTime.Now;

			int MaxWaitMins = 30;

			while (!CurrentProcess.HasExited)
			{
				Thread.Sleep(5 * 1000);

				if ((DateTime.Now - StartTime).TotalMinutes >= MaxWaitMins)
				{
					Log.TraceError("Gave up waiting for task after {0} minutes", MaxWaitMins);
					CurrentProcess.ProcessObject.Kill();
				}
			}

			int ExitCode = CurrentProcess.ExitCode;
			CurrentProcess = null;

			/*if (!TestCompleted)
			{
				// spew filter can lag slightly...
				Thread.Sleep(1000);

				if (!TestCompleted)
				{
					Log.TraceError("Editor exited without test completing");
				}
			}*/

			return ExitCode == 0;
		}

		protected override bool PerformTask()
		{
			return RunEditorAndWaitForMapLoad();
		}
	}
}
