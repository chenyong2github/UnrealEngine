// Copyright Epic Games, Inc. All Rights Reserved.

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
	/// <summary>
	/// Base class for running tasks
	/// </summary>
	abstract class BenchmarkTaskBase
	{
		/// <summary>
		/// True or false based on whether the task failed
		/// </summary>
		public bool Failed { get; protected set; }

		/// <summary>
		/// Failure message
		/// </summary>
		public string FailureString { get; protected set; }

		/// <summary>
		/// Don't report this test
		/// </summary>
		public bool SkipReport { get; protected set; }

		/// <summary>
		/// Time the task took (does not include prequisites)
		/// </summary>
		public TimeSpan TaskTime { get; protected set; }

		/// <summary>
		/// Time the task started
		/// </summary>
		public DateTime StartTime { get; protected set; }

		/// <summary>
		/// Perform any prerequisites the task requires
		/// </summary>
		virtual protected bool PerformPrequisites() { return true;  }

		/// <summary>
		/// Perform post-task cleanup
		/// </summary>
		virtual protected void PerformCleanup() { }

		/// <summary>
		/// Perform the actual task that is measured
		/// </summary>
		protected abstract bool PerformTask();

		/// <summary>
		/// Return a name for this task for reporting
		/// </summary>
		/// <returns></returns>
		public string TaskName { get; set; }

		/// <summary>
		/// A list of modifiers that can be considered when
		/// </summary>
		protected List<string> TaskModifiers { get { return InternalModifiers; }  }

		private readonly List<string> InternalModifiers = new List<string>();

		/// <summary>
		/// Run the task. Performs any prerequisites, then the actual task itself
		/// </summary>
		public void Run()
		{
			try
			{
				if (PerformPrequisites())
				{
					StartTime = DateTime.Now;

					if (PerformTask())
					{
						TaskTime = DateTime.Now - StartTime;
					}
					else
					{
						FailureString = "Task Failed";
						Failed = true;
					}
				}
				else
				{
					FailureString = "Prequisites Failed";
					Failed = true;
				}
				
			}
			catch (Exception Ex)
			{
				FailureString = string.Format("Exception: {0}", Ex.ToString());
				Failed = true;
			}		
			
			if (Failed)
			{
				Log.TraceError("{0} failed. {1}", GetFullTaskName(), FailureString);
			}

			try
			{
				PerformCleanup();
			}
			catch (Exception Ex)
			{
				Log.TraceError("Cleanup of {0} failed. {1}", GetFullTaskName(), Ex);
			}
		}

		/// <summary>
		/// Report how long the task took
		/// </summary>
		public void Report()
		{
			if (!Failed)
			{
				Log.TraceInformation("Task {0}:\t\t\t\t{1}", GetFullTaskName(), TaskTime.ToString(@"hh\:mm\:ss"));
			}
			else
			{
				Log.TraceInformation("Task {0}::\t\t\t\tFailed. {1}", GetFullTaskName(), FailureString);
			}
		}

		/// <summary>
		/// Returns a full name to use in reporting and logging
		/// </summary>
		/// <returns></returns>
		public string GetFullTaskName()
		{
			string Name = TaskName;

			if (TaskModifiers.Count > 0)
			{
				Name = string.Format("{0} ({1})", Name, string.Join(" ", TaskModifiers));
			}

			return Name;
		}

		public override string ToString()
		{
			return GetFullTaskName();
		}
	}

	[Flags]
	public enum DDCTaskOptions
	{
		None = 0,
		WarmDDC = 1 << 0,
		ColdDDC = 1 << 1,
		//NoDDC = 1 << 2,
		NoShaderDDC = 1 << 3,
		HotDDC = 1 << 4,
		NoXGE = 1 << 5,			// don't use XGE for shader compilation

		KeepMemoryDDC = 1 << 6,
	}

	abstract class BenchmarkEditorTaskBase : BenchmarkTaskBase
	{
		protected DDCTaskOptions TaskOptions;

		protected FileReference ProjectFile = null;

		protected string EditorArgs = "";

		protected string ProjectName
		{
			get
			{
				return ProjectFile == null ? "UE4" : ProjectFile.GetFileNameWithoutAnyExtensions();
			}
		}

		protected BenchmarkEditorTaskBase(FileReference InProjectFile, DDCTaskOptions InTaskOptions, string InEditorArgs)
		{
			TaskOptions = InTaskOptions;
			EditorArgs = InEditorArgs.Trim().Replace("  ", " ");
			ProjectFile = InProjectFile;

			if (TaskOptions == DDCTaskOptions.None || TaskOptions.HasFlag(DDCTaskOptions.WarmDDC))
			{
				TaskModifiers.Add("warmddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.ColdDDC))
			{
				TaskModifiers.Add("coldddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.HotDDC))
			{
				TaskModifiers.Add("hotddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.NoShaderDDC))
			{
				TaskModifiers.Add("noshaderddc");
			}

			if (TaskOptions.HasFlag(DDCTaskOptions.KeepMemoryDDC))
			{
				TaskModifiers.Add("withbootddc");
			}

			if (!string.IsNullOrEmpty(EditorArgs))
			{
				TaskModifiers.Add(EditorArgs);
			}
		}

		private Dictionary<string, string> StoredEnvVars = new Dictionary<string, string>();
		private List<DirectoryReference> CachePaths = new List<DirectoryReference>();

		private string GetXPlatformEnvironmentKey(string InKey)
		{
			// Mac uses _ in place of -
			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Win64)
			{
				InKey = InKey.Replace("-", "_");
			}

			return InKey;
		}

		protected override bool PerformPrequisites()
		{
			if (TaskOptions.HasFlag(DDCTaskOptions.ColdDDC))
			{
				StoredEnvVars.Clear();
				CachePaths.Clear();

				// We put our temp DDC paths in here
				DirectoryReference BasePath = DirectoryReference.Combine(CommandUtils.EngineDirectory, "BenchmarkDDC");

				IEnumerable<string> DDCEnvVars = new string[] { GetXPlatformEnvironmentKey("UE-BootDataCachePath"), GetXPlatformEnvironmentKey("UE-LocalDataCachePath") };
				
				if (TaskOptions.HasFlag(DDCTaskOptions.KeepMemoryDDC))
				{
					DDCEnvVars = DDCEnvVars.Where(E => !E.Contains("UE-Boot"));
				}

				// get all current environment vars and set them to our temp dir
				foreach (var Key in DDCEnvVars)
				{
					// save current key
					StoredEnvVars.Add(Key, Environment.GetEnvironmentVariable(Key));

					// create a new dir for this key
					DirectoryReference Dir = DirectoryReference.Combine(BasePath, Key);

					if (DirectoryReference.Exists(Dir))
					{
						DirectoryReference.Delete(Dir, true);
					}

					DirectoryReference.CreateDirectory(Dir);

					// save this dir and set it as the env var
					CachePaths.Add(Dir);
					Environment.SetEnvironmentVariable(Key, Dir.FullName);
				}

				// remove project files
				DirectoryReference ProjectDDC = DirectoryReference.Combine(ProjectFile.Directory, "DerivedDataCache");
				CommandUtils.DeleteDirectory_NoExceptions(ProjectDDC.FullName);

				// remove S3 files
				DirectoryReference S3DDC = DirectoryReference.Combine(ProjectFile.Directory, "Saved", "S3DDC");
				CommandUtils.DeleteDirectory_NoExceptions(S3DDC.FullName);
			}

			return base.PerformPrequisites();
		}

		protected override void PerformCleanup()
		{
			// restore keys
			foreach (var KV in StoredEnvVars)
			{
				Environment.SetEnvironmentVariable(KV.Key, KV.Value);
			}

			foreach (var Dir in CachePaths)
			{
				CommandUtils.DeleteDirectory_NoExceptions(Dir.FullName);
			}

			CachePaths.Clear();
			StoredEnvVars.Clear();
		}
	}
}
