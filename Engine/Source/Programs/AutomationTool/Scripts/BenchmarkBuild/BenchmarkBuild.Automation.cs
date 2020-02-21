// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using UnrealBuildTool;

namespace AutomationTool.Benchmark
{

	[Help("Runs benchmarks and reports overall results")]
	[Help("Example1: RunUAT BenchmarkBuild -all -project=UE4")]
	[Help("Example2: RunUAT BenchmarkBuild -allcompile -project=UE4+EngineTest -platform=PS4")]
	[Help("Example3: RunUAT BenchmarkBuild -editor -client -cook -cooknoshaderddc -cooknoddc -xge -noxge -singlecompile -nopcompile -project=UE4+QAGame+EngineTest -platform=WIn64+PS4+XboxOne+Switch -iterations=3")]
	[Help("preview", "List everything that will run but don't do it")]
	[Help("project=<name>", "Do tests on the specified projec(s)t. E.g. -project=UE4+FortniteGame+QAGame")]
	[Help("all", "Run all the things (except noddc)")]
	[Help("allcompile", "Run all the compile things")]
	[Help("editor", "Build an editor for compile tests")]
	[Help("client", "Build a client for comple tests (see -platform)")]	
	[Help("platform=<p1+p2>", "Specify the platform(s) to use for client compilation/cooking, if empty the local platform be used if -client or -cook is specified")]
	[Help("xge", "Do a compile with XGE / FASTBuild")]
	[Help("noxge", "Do a compile without XGE / FASTBuild")]
	[Help("singlecompile", "Do a single-file compile")]
	[Help("nopcompile", "Do a nothing-needs-compiled compile")]
	[Help("cook", "Do a cook for the specified platform")]
	[Help("cooknoshaderddc", "Do a cook test with no ddc for shaders")]
	[Help("cooknoddc", "Do a cook test with nodcc (likely to take 10+ hours with cookfortnite)")]
	[Help("iterations=<n>", "How many times to perform each test)")]
	[Help("wait=<n>", "How many seconds to wait between each test)")]
	[Help("filename", "Name/path of file to write CSV results to. If empty the local machine name will be used")]
	[Help("warmcook", "Before cooking do a non-timed cook to make sure any  DDC is full")]
	[Help("noclean", "Don't build from clean. (Mostly just to speed things up when testing)")]	
	class BenchmarkBuild : BuildCommand
	{
		protected List<BenchmarkTaskBase> Tasks = new List<BenchmarkTaskBase>();

		protected Dictionary<BenchmarkTaskBase, List<TimeSpan>> Results = new Dictionary<BenchmarkTaskBase, List<TimeSpan>>();

		public BenchmarkBuild()
		{ 
		}

		public override ExitCode Execute()
		{
			bool Preview = ParseParam("preview");

			bool AllThings = ParseParam("all");

			bool AllCompile =  AllThings | ParseParam("allcompile");

			bool DoUE4 = AllCompile | ParseParam("ue4");
			bool DoBuildEditorTests = AllCompile | ParseParam("editor");
			bool DoBuildClientTests = AllCompile | ParseParam("client");
			bool DoNoCompile = AllCompile | ParseParam("nopcompile");
			bool DoSingleCompile = AllCompile | ParseParam("singlecompile");
			bool DoAcceleratedCompile = AllCompile | ParseParam("xge") | ParseParam("fastbuild");
			bool DoNoAcceleratedCompile = AllCompile | ParseParam("noxge") | ParseParam("nofastbuild");

			bool DoCookTests = AllThings | ParseParam("cook");
			bool DoWarmCook = AllThings | ParseParam("warmcook");
			bool DoNoShaderDDC = AllThings | ParseParam("cooknoshaderddc");
			bool DoNoDDC = ParseParam("cooknoddc");

			bool NoClean = ParseParam("noclean");
			int TimeBetweenTasks = ParseParamInt("wait", 10);
			int NumLoops = ParseParamInt("iterations", 1);

			string FileName = ParseParamValue("filename", string.Format("{0}_Results.csv", Environment.MachineName));

			// We always build the editor for the platform we're running on
			UnrealTargetPlatform EditorPlatform = BuildHostPlatform.Current.Platform;

			List<UnrealTargetPlatform> ClientPlatforms = new List<UnrealTargetPlatform>();

			string PlatformArg = ParseParamValue("platform", "");

			if (!string.IsNullOrEmpty(PlatformArg))
			{
				var PlatformList = PlatformArg.Split(new[] { '+', ',' }, StringSplitOptions.RemoveEmptyEntries);

				foreach (var Platform in PlatformList)
				{
					UnrealTargetPlatform PlatformEnum;
					if (!UnrealTargetPlatform.TryParse(Platform, out PlatformEnum))
					{
						throw new AutomationException("{0} is not a valid Unreal Platform", Platform);
					}

					ClientPlatforms.Add(PlatformEnum);
				}
			}
			else
			{
				ClientPlatforms.Add(EditorPlatform);
			}

			DoAcceleratedCompile = DoAcceleratedCompile && BenchmarkBuildTask.SupportsAcceleration;

			// Set this based on whether the user specified -noclean
			BuildOptions CleanFlag = NoClean ? BuildOptions.None : BuildOptions.Clean;

			List<string> ProjectsToBenchmark = new List<string>();

			string ProjectsArg = ParseParamValue("project", null);

			// Look at the project argument and verify it's a valid uproject
			if (!string.IsNullOrEmpty(ProjectsArg))
			{
				var ProjectList = ProjectsArg.Split(new[] { '+', ',' }, StringSplitOptions.RemoveEmptyEntries);

				foreach (var Project in ProjectList)
				{
					if (!string.Equals(Project, "UE4", StringComparison.OrdinalIgnoreCase))
					{
						FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(Project);

						if (ProjectFile == null)
						{
							throw new AutomationException("Could not find project file for {0}", Project);
						}
					}

					ProjectsToBenchmark.Add(Project);
				}
			}

			foreach (var Project in ProjectsToBenchmark)
			{
				bool IsVanillaUE4 = string.Equals(Project, "UE4", StringComparison.OrdinalIgnoreCase);

				if (DoBuildEditorTests)
				{
					BuildOptions NoAndSingleCompileOptions = BuildOptions.None;

					if (DoAcceleratedCompile)
					{
						Tasks.Add(new BenchmarkBuildTask(Project, "Editor", EditorPlatform, CleanFlag));
					}

					if (DoNoAcceleratedCompile)
					{
						Tasks.Add(new BenchmarkBuildTask(Project, "Editor", EditorPlatform, CleanFlag | BuildOptions.NoAcceleration));
						NoAndSingleCompileOptions |= BuildOptions.NoAcceleration;
					}

					if (DoNoCompile)
					{
						
						// note, don't clean since we build normally then build a single file
						Tasks.Add(new BenchmarkNopCompileTask(Project, "Editor", EditorPlatform, NoAndSingleCompileOptions));
					}

					if (DoSingleCompile)
					{
						FileReference SourceFile = FindProjectSourceFile(Project);

						// note, don't clean since we build normally then build again
						Tasks.Add(new BenchmarkSingleCompileTask(Project, "Editor", EditorPlatform, SourceFile, NoAndSingleCompileOptions));
					}
				}

				if (DoBuildClientTests)
				{
					// build a client if the project supports it
					string TargetName = ProjectSupportsClientBuild(Project) ? "Client" : "Game";

					foreach (var ClientPlatform in ClientPlatforms)
					{
						BuildOptions NoAndSingleCompileOptions = BuildOptions.None;

						if (DoAcceleratedCompile)
						{
							Tasks.Add(new BenchmarkBuildTask(Project, TargetName, ClientPlatform, CleanFlag));
						}

						if (DoNoAcceleratedCompile)
						{
							Tasks.Add(new BenchmarkBuildTask(Project, TargetName, ClientPlatform, CleanFlag | BuildOptions.NoAcceleration));
							NoAndSingleCompileOptions |= BuildOptions.NoAcceleration;
						}

						if (DoNoCompile)
						{
							// note, don't clean since we build normally then build again
							Tasks.Add(new BenchmarkNopCompileTask(Project, TargetName, ClientPlatform, NoAndSingleCompileOptions));
						}

						if (DoSingleCompile)
						{
							FileReference SourceFile = FindProjectSourceFile(Project);

							// note, don't clean since we build normally then build a single file
							Tasks.Add(new BenchmarkSingleCompileTask(Project, TargetName, ClientPlatform, SourceFile, NoAndSingleCompileOptions));
						}
					}
				}

				// Do cook tests if this is a project and not the engine
				if (DoCookTests && !IsVanillaUE4)
				{
					// Cook a client if the project supports it
					CookOptions ClientCookOptions = ProjectSupportsClientBuild(Project) ? CookOptions.Client : CookOptions.None;

					foreach (var ClientPlatform in ClientPlatforms)
					{
						CookOptions TaskCookOptions = ClientCookOptions | CookOptions.Clean;

						if (DoWarmCook)
						{
							TaskCookOptions |= CookOptions.WarmCook;
						}

						if (DoCookTests)
						{
							Tasks.Add(new BenchmarkCookTask(Project, ClientPlatform, TaskCookOptions));
							TaskCookOptions = ClientCookOptions | CookOptions.Clean;
						}

						if (DoNoShaderDDC)
						{
							Tasks.Add(new BenchmarkCookTask(Project, ClientPlatform, TaskCookOptions | CookOptions.NoShaderDDC));
							TaskCookOptions = ClientCookOptions | CookOptions.Clean;
						}

						if (DoNoDDC)
						{
							Tasks.Add(new BenchmarkCookTask(Project, ClientPlatform, TaskCookOptions | CookOptions.NoDDC));
						}
					}
				}
			}

			Log.TraceInformation("Will execute tests:");

			foreach (var Task in Tasks)
			{
				Log.TraceInformation("{0}", Task.GetFullTaskName());
			}

			if (!Preview)
			{
				// create results lists
				foreach (var Task in Tasks)
				{
					Results.Add(Task, new List<TimeSpan>());
				}

				DateTime StartTime = DateTime.Now;

				for (int i = 0; i < NumLoops; i++)
				{
					foreach (var Task in Tasks)
					{
						Log.TraceInformation("Starting task {0} (Pass {1})", Task.GetFullTaskName(), i+1);

						Task.Run();

						Log.TraceInformation("Task {0} took {1}", Task.GetFullTaskName(), Task.TaskTime.ToString(@"hh\:mm\:ss"));

						Results[Task].Add(Task.TaskTime);

						WriteCSVResults(FileName);

						Log.TraceInformation("Waiting {0} secs until next task", TimeBetweenTasks);
						Thread.Sleep(TimeBetweenTasks * 1000);
					}
				}

				Log.TraceInformation("**********************************************************************");
				Log.TraceInformation("Test Results:");
				foreach (var Task in Tasks)
				{
					string TimeString = "";

					IEnumerable<TimeSpan> TaskTimes = Results[Task];

					foreach (var TaskTime in TaskTimes)
					{
						if (TimeString.Length > 0)
						{
							TimeString += ", ";
						}

						if (TaskTime == TimeSpan.Zero)
						{
							TimeString += "Failed";
						}
						else
						{
							TimeString += TaskTime.ToString(@"hh\:mm\:ss");
						}
					}

					var AvgTimeString = "";

					if (TaskTimes.Count() > 1)
					{
						var AvgTime = new TimeSpan(TaskTimes.Sum(T => T.Ticks) / TaskTimes.Count());

						AvgTimeString = string.Format(" (Avg: {0})", AvgTime.ToString(@"hh\:mm\:ss"));
					}					

					Log.TraceInformation("Task {0}:\t{1}{2}", Task.GetFullTaskName(), TimeString, AvgTimeString);
				}
				Log.TraceInformation("**********************************************************************");

				TimeSpan Elapsed = DateTime.Now - StartTime;

				Log.TraceInformation("Total benchmark time: {0}",  Elapsed.ToString(@"hh\:mm\:ss"));

				WriteCSVResults(FileName);
			}

			return ExitCode.Success;
		}

		/// <summary>
		/// Writes our current result to a CSV file. It's expected that this function is called multiple times so results are
		/// updated as we go
		/// </summary>
		void WriteCSVResults(string FileName)
		{

			Log.TraceInformation("Writing results to {0}", FileName);

			try
			{
				List<string> Lines = new List<string>();

				// first line is machine name,CPU count,Iteration 1, Iteration 2 etc
				string FirstLine = string.Format("{0},{1}", Environment.MachineName, Environment.ProcessorCount);

				if (Tasks.Count() > 0)
				{
					int Iterations = Results[Tasks.First()].Count();

					if (Iterations > 0)
					{
						for (int i = 0; i < Iterations; i++)
						{
							FirstLine += ",";
							FirstLine += string.Format("Iteration {0}", i + 1);
						}
					}
				}

				Lines.Add(FirstLine);

				foreach (var Task in Tasks)
				{
					// start with Name, StartTime
					string Line = string.Format("{0},{1}", Task.GetFullTaskName(), Task.StartTime.ToString("yyyy-dd-MM HH:mm:ss"));

					// now append all iteration times
					foreach (TimeSpan TaskTime in Results[Task])
					{
						Line += ",";
						if (TaskTime == TimeSpan.Zero)
						{
							Line += "FAILED";
						}
						else
						{
							Line += TaskTime.ToString(@"hh\:mm\:ss");
						}
					}

					Lines.Add(Line);
				}

				File.WriteAllLines(FileName, Lines.ToArray());
			}
			catch (Exception Ex)
			{
				Log.TraceError("Failed to write CSV to {0}. {1}", FileName, Ex);
			}
		}

		/// <summary>
		/// Returns true/false based on whether the project supports a client configuration
		/// </summary>
		/// <param name="ProjectName"></param>
		/// <returns></returns>
		bool ProjectSupportsClientBuild(string ProjectName)
		{
			if (ProjectName.Equals("UE4", StringComparison.OrdinalIgnoreCase))
			{
				// UE4
				return true;
			}

			FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

			DirectoryReference SourceDir = DirectoryReference.Combine(ProjectFile.Directory, "Source");

			var Files = DirectoryReference.EnumerateFiles(SourceDir, "*Client.Target.cs");

			return Files.Any();
		}


		/// <summary>
		/// Returns true/false based on whether the project supports a client configuration
		/// </summary>
		/// <param name="ProjectName"></param>
		/// <returns></returns>
		FileReference FindProjectSourceFile(string ProjectName)
		{
			FileReference SourceFile = null;

			FileReference ProjectFile = ProjectUtils.FindProjectFileFromName(ProjectName);

			if (ProjectFile != null)
			{
				DirectoryReference SourceDir = DirectoryReference.Combine(ProjectFile.Directory, "Source", ProjectName);

				var Files = DirectoryReference.EnumerateFiles(SourceDir, "*.cpp", System.IO.SearchOption.AllDirectories);

				SourceFile = Files.FirstOrDefault();
			}

			if (SourceFile == null)
			{
				// touch the write time on a file, first making it writable since it may be under P4
				SourceFile = FileReference.Combine(CommandUtils.EngineDirectory, "Source/Runtime/Engine/Private/UnrealEngine.cpp");
			}

			Log.TraceVerbose("Will compile {0} for single-file compilation test for {1}", SourceFile, ProjectName);

			return SourceFile;
		}
	}
}
