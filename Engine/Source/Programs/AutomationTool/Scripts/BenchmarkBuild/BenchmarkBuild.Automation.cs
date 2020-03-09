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
	[Help("cores=X+Y+Z", "Do noxge builds with these processor counts (default is Environment.ProcessorCount)")]
	[Help("cook", "Do a cook for the specified platform")]
	[Help("cook-cold", "When cooking clear the local ddc before each run")]
	[Help("cook-noshaderddc", "Do a cook test with no ddc for shaders")]
	[Help("cook-noddc", "Do a cook test with nodcc (likely to take 10+ hours with cookfortnite)")]
	[Help("pie", "Launch the editor (only valid when -project is specified")]
	[Help("pie-cold", "Launch the editor with a cold ddc")]
	[Help("pie-hot", "Launch the editor with a hot ddc")]
	[Help("pie-hot", "Launch the editor with a hot ddc")]
	[Help("iterations=<n>", "How many times to perform each test)")]
	[Help("wait=<n>", "How many seconds to wait between each test)")]
	[Help("filename", "Name/path of file to write CSV results to. If empty the local machine name will be used")]
	[Help("noclean", "Don't build from clean. (Mostly just to speed things up when testing)")]
	[Help("ExtraCookArgs", "Extra args to use when cooking")]
	class BenchmarkBuild : BuildCommand
	{
		class BenchmarkOptions : BuildCommand
		{
			public bool Preview = false;

			public bool DoUE4Tests = false;
			public IEnumerable<string> ProjectsToTest = Enumerable.Empty<string>();
			public IEnumerable<UnrealTargetPlatform> PlatformsToTest = Enumerable.Empty<UnrealTargetPlatform>();

			// building
			public bool DoBuildEditorTests = false;
			public bool DoBuildClientTests = false;
			public bool DoNoCompileTests = false;
			public bool DoSingleCompileTests = false;
			public bool DoAcceleratedCompileTests = false;
			public bool DoNoAcceleratedCompileTests = false;

			public IEnumerable<int> CoresForLocalJobs = new[] { Environment.ProcessorCount };

			// cooking
			public bool DoCookTests = false;
			public bool DoColdCookTests = false;
			public bool DoHotCookTests = false;
			public bool DoNoshaderDDCCookTests = false;
			public bool DoNoDDCCookTests = false;

			// editor startup tests
			public bool DoPIETests = false;
			public bool DoHotPIETests = false;
			public bool DoColdPIETests = false;
			public bool DoNoDDCPIETests = false;

			// list of maps for each project
			public IEnumerable<string> StartupMapList = Enumerable.Empty<string>();

			// misc
			public int Iterations = 1;
			public bool NoClean = false;
			public int TimeBetweenTasks = 0;
			public string ExtraCookArgs = "";
			public string FileName = string.Format("{0}_Results.csv", Environment.MachineName);

			public void ParseParams(string[] InParams)
			{
				this.Params = InParams;

				bool AllThings = ParseParam("all");
				bool AllCompile = AllThings | ParseParam("allcompile");

				Preview = ParseParam("preview");
				DoUE4Tests = AllThings || ParseParam("ue4");

				// compilation
				DoBuildEditorTests = AllCompile | ParseParam("editor");
				DoBuildClientTests = AllCompile | ParseParam("client");
				DoNoCompileTests = AllCompile | ParseParam("nopcompile");
				DoSingleCompileTests = AllCompile | ParseParam("singlecompile");
				DoAcceleratedCompileTests = AllCompile | ParseParam("xge") | ParseParam("fastbuild");
				DoNoAcceleratedCompileTests = AllCompile | ParseParam("noxge") | ParseParam("nofastbuild");

				// cooking
				DoCookTests = AllThings | ParseParam("cook");
				DoColdCookTests = AllThings | ParseParam("cook-cold");
				DoHotCookTests = AllThings | ParseParam("cook-hotddc");
				DoNoshaderDDCCookTests = AllThings | ParseParam("cook-noshaderddc");
				DoNoDDCCookTests = ParseParam("cook-noddc");

				// editor startup tests
				DoPIETests = AllThings | ParseParam("pie");
				DoColdPIETests = AllThings | ParseParam("pie-cold");
				DoHotPIETests = AllThings | ParseParam("pie-hot");
				DoNoDDCPIETests = AllThings | ParseParam("pie-noddc");

				// sanity
				DoAcceleratedCompileTests = DoAcceleratedCompileTests && BenchmarkBuildTask.SupportsAcceleration;

				Preview = ParseParam("Preview");
				Iterations = ParseParamInt("Iterations", Iterations);
				TimeBetweenTasks = ParseParamInt("Wait", TimeBetweenTasks);

				FileName = ParseParamValue("filename", FileName);

				ExtraCookArgs = ParseParamValue("ExtraCookArgs", "");

				// Parse the project arg
				{
					string ProjectsArg = ParseParamValue("project", null);
					ProjectsArg = ParseParamValue("projects", ProjectsArg);

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
						}

						ProjectsToTest = ProjectList;
					}
				}

				// Parse and validate platform list from arguments
				{
					string PlatformArg = ParseParamValue("platform", "");
					PlatformArg = ParseParamValue("platforms", PlatformArg);

					if (!string.IsNullOrEmpty(PlatformArg))
					{
						List<UnrealTargetPlatform> ClientPlatforms = new List<UnrealTargetPlatform>();

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

						PlatformsToTest = ClientPlatforms;
					}
					else
					{
						PlatformsToTest = new[] { BuildHostPlatform.Current.Platform };
					}
				}

				// parse processor args
				{
					string ProcessorArg = ParseParamValue("cores", "");

					if (!string.IsNullOrEmpty(ProcessorArg))
					{
						var ProcessorList = ProcessorArg.Split(new[] { '+', ',' }, StringSplitOptions.RemoveEmptyEntries);

						CoresForLocalJobs = ProcessorList.Select(P => Convert.ToInt32(P));
					}
				}

				// parse maps for startup tests
				{
					// pull the list of maps to use for startup tests
					string MapArg = ParseParamValue("startupmap", "");
					MapArg = ParseParamValue("startupmaps", MapArg);
					StartupMapList = MapArg.Split(new[] { '+', ',' }, StringSplitOptions.RemoveEmptyEntries);
				}
			}
		}

		public BenchmarkBuild()
		{ 
		}

		public override ExitCode Execute()
		{
			BenchmarkOptions Options = new BenchmarkOptions();
			Options.ParseParams(this.Params);

			List<BenchmarkTaskBase> Tasks = new List<BenchmarkTaskBase>();

			Dictionary<BenchmarkTaskBase, List<TimeSpan>> Results = new Dictionary<BenchmarkTaskBase, List<TimeSpan>>();

			for (int ProjectIndex = 0; ProjectIndex <  Options.ProjectsToTest.Count(); ProjectIndex++)
			{
				string Project = Options.ProjectsToTest.ElementAt(ProjectIndex);

				if (Options.DoBuildEditorTests)
				{
					Tasks.AddRange(AddBuildTests(Project, BuildHostPlatform.Current.Platform, "Editor", Options));
				}

				// do startup tests
				Tasks.AddRange(AddStartupTests(Project, Options));

				foreach (var ClientPlatform in Options.PlatformsToTest)
				{
					// build a client if the project supports it
					string TargetName = ProjectSupportsClientBuild(Project) ? "Client" : "Game";

					// do build tests
					Tasks.AddRange(AddBuildTests(Project, ClientPlatform, TargetName, Options));

					// do cook tests
					Tasks.AddRange(AddCookTests(Project, ClientPlatform, Options));
				}
			}

			Log.TraceInformation("Will execute tests:");

			foreach (var Task in Tasks)
			{
				Log.TraceInformation("{0}", Task.GetFullTaskName());
			}

			if (!Options.Preview)
			{
				// create results lists
				foreach (var Task in Tasks)
				{
					Results.Add(Task, new List<TimeSpan>());
				}

				DateTime StartTime = DateTime.Now;

				for (int i = 0; i < Options.Iterations; i++)
				{
					foreach (var Task in Tasks)
					{
						Log.TraceInformation("Starting task {0} (Pass {1})", Task.GetFullTaskName(), i+1);

						Task.Run();

						Log.TraceInformation("Task {0} took {1}", Task.GetFullTaskName(), Task.TaskTime.ToString(@"hh\:mm\:ss"));

						Results[Task].Add(Task.TaskTime);

						// write results so far
						WriteCSVResults(Options.FileName, Tasks, Results);

						Log.TraceInformation("Waiting {0} secs until next task", Options.TimeBetweenTasks);
						Thread.Sleep(Options.TimeBetweenTasks * 1000);
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

				WriteCSVResults(Options.FileName, Tasks, Results);
			}

			return ExitCode.Success;
		}

		IEnumerable<BenchmarkTaskBase> AddBuildTests(string InProjectName, UnrealTargetPlatform InPlatform, string InTargetName, BenchmarkOptions InOptions)
		{
			BuildOptions CleanFlag = InOptions.NoClean ? BuildOptions.None : BuildOptions.Clean;

			BuildOptions NoAndSingleCompileOptions = BuildOptions.None;

			List<BenchmarkTaskBase> NewTasks = new List<BenchmarkTaskBase>();

			if (InOptions.DoAcceleratedCompileTests)
			{
				NewTasks.Add(new BenchmarkBuildTask(InProjectName, InTargetName, InPlatform, CleanFlag));
			}

			if (InOptions.DoNoAcceleratedCompileTests)
			{
				foreach (int ProcessorCount in InOptions.CoresForLocalJobs)
				{
					NewTasks.Add(new BenchmarkBuildTask(InProjectName, InTargetName, InPlatform, CleanFlag | BuildOptions.NoAcceleration, "", ProcessorCount));
				}
				// do single compilation with these results
				NoAndSingleCompileOptions |= BuildOptions.NoAcceleration;
			}

			if (InOptions.DoNoCompileTests)
			{
				// note, don't clean since we build normally then build a single file
				NewTasks.Add(new BenchmarkNopCompileTask(InProjectName, InTargetName, InPlatform, NoAndSingleCompileOptions));
			}

			if (InOptions.DoSingleCompileTests)
			{
				FileReference SourceFile = FindProjectSourceFile(InProjectName);

				// note, don't clean since we build normally then build again
				NewTasks.Add(new BenchmarkSingleCompileTask(InProjectName, InTargetName, InPlatform, SourceFile, NoAndSingleCompileOptions));
			}

			return NewTasks;
		}

		IEnumerable<BenchmarkTaskBase> AddCookTests(string InProjectName, UnrealTargetPlatform InPlatform, BenchmarkOptions InOptions)
		{
			if (InProjectName.Equals("UE4", StringComparison.OrdinalIgnoreCase))
			{
				return Enumerable.Empty<BenchmarkTaskBase>();
			}

			List<BenchmarkTaskBase> NewTasks = new List<BenchmarkTaskBase>();

			// Cook a client if the project supports i
			bool CookClient = ProjectSupportsClientBuild(InProjectName);
			EditorTaskOptions ClientCookOptions = CookClient ? EditorTaskOptions.CookClient : EditorTaskOptions.None;

			EditorTaskOptions DefaultCookOptions = ClientCookOptions;
			EditorTaskOptions TaskCookOptions = DefaultCookOptions;

			if (InOptions.DoHotCookTests)
			{
				// only want to set this for the first cook for a platform
				TaskCookOptions |= EditorTaskOptions.HotDDC;
			}

			if (InOptions.DoCookTests)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, TaskCookOptions, InOptions.ExtraCookArgs));
				TaskCookOptions = DefaultCookOptions;
			}

			if (InOptions.DoColdCookTests)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, TaskCookOptions | EditorTaskOptions.ColdDDC, InOptions.ExtraCookArgs));
				TaskCookOptions = DefaultCookOptions;
			}

			if (InOptions.DoNoshaderDDCCookTests)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, TaskCookOptions | EditorTaskOptions.NoShaderDDC, InOptions.ExtraCookArgs));
				TaskCookOptions = DefaultCookOptions;
			}

			if (InOptions.DoNoDDCCookTests)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, TaskCookOptions | EditorTaskOptions.NoDDC, InOptions.ExtraCookArgs));
				TaskCookOptions = DefaultCookOptions;
			}

			/*bool DoWriteHints = ParseParam("recordhints");
			bool DoUseHints = ParseParam("usehints");

			if (DoWriteHints)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, ClientCookOptions, "-recordhints"));
			}

			if (DoUseHints)
			{
				NewTasks.Add(new BenchmarkCookTask(InProjectName, InPlatform, ClientCookOptions | EditorTaskOptions.ColdDDC, "-usehints"));
			}*/

			return NewTasks;
		}

		IEnumerable<BenchmarkTaskBase> AddStartupTests(string InProjectName, BenchmarkOptions InOptions)
		{
			if (InProjectName.Equals("UE4", StringComparison.OrdinalIgnoreCase))
			{
				return Enumerable.Empty<BenchmarkTaskBase>();
			}

			List<BenchmarkTaskBase> NewTasks = new List<BenchmarkTaskBase>();

			if (InOptions.DoPIETests)
			{
				EditorTaskOptions Options = InOptions.DoHotPIETests ? EditorTaskOptions.HotDDC : EditorTaskOptions.None;
				NewTasks.Add(new BenchmarkRunEditorTask(InProjectName, Options));
			}

			if (InOptions.DoColdPIETests)
			{
				NewTasks.Add(new BenchmarkRunEditorTask(InProjectName, EditorTaskOptions.None | EditorTaskOptions.ColdDDC));
			}

			if (InOptions.DoNoDDCPIETests)
			{
				NewTasks.Add(new BenchmarkRunEditorTask(InProjectName, EditorTaskOptions.None | EditorTaskOptions.NoDDC));
			}

			return NewTasks;
		}

		/// <summary>
		/// Writes our current result to a CSV file. It's expected that this function is called multiple times so results are
		/// updated as we go
		/// </summary>
		void WriteCSVResults(string InFileName, List<BenchmarkTaskBase> InTasks, Dictionary<BenchmarkTaskBase, List<TimeSpan>> InResults)
		{

			Log.TraceInformation("Writing results to {0}", InFileName);

			try
			{
				List<string> Lines = new List<string>();

				// first line is machine name,CPU count,Iteration 1, Iteration 2 etc
				string FirstLine = string.Format("{0},{1}", Environment.MachineName, Environment.ProcessorCount);

				if (InTasks.Count() > 0)
				{
					int Iterations = InResults[InTasks.First()].Count();

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

				foreach (var Task in InTasks)
				{
					// start with Name, StartTime
					string Line = string.Format("{0},{1}", Task.GetFullTaskName(), Task.StartTime.ToString("yyyy-dd-MM HH:mm:ss"));

					// now append all iteration times
					foreach (TimeSpan TaskTime in InResults[Task])
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

				File.WriteAllLines(InFileName, Lines.ToArray());
			}
			catch (Exception Ex)
			{
				Log.TraceError("Failed to write CSV to {0}. {1}", InFileName, Ex);
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

			ProjectProperties Properties = ProjectUtils.GetProjectProperties(ProjectFile);

			return Properties.Targets.Where(T => T.TargetName.Contains("Client")).Any();
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
