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
	}

	[Flags]
	public enum EditorTaskOptions
	{
		None = 0,
		ColdDDC = 1 << 0,
		NoDDC = 1 << 1,
		NoShaderDDC = 1 << 2,
		CookClient = 1 << 3,
		HotDDC = 1 << 4,
	}

	abstract class BenchmarkEditorTaskBase : BenchmarkTaskBase
	{
		protected EditorTaskOptions TaskOptions;

		protected FileReference ProjectFile = null;

		protected string ProjectName
		{
			get
			{
				return ProjectFile == null ? null : ProjectFile.GetFileNameWithoutAnyExtensions();
			}
		}

		protected BenchmarkEditorTaskBase(string InProject, EditorTaskOptions InTaskOptions)
		{
			TaskOptions = InTaskOptions;

			if (!InProject.Equals("UE4", StringComparison.OrdinalIgnoreCase))
			{
				ProjectFile = ProjectUtils.FindProjectFileFromName(InProject);
			}
		}

		private void DeleteLocalDDC(FileReference InProjectFile)
		{
			List<DirectoryReference> DirsToClear = new List<DirectoryReference>();

			DirectoryReference ProjectDir = InProjectFile.Directory;

			DirsToClear.Add(DirectoryReference.Combine(ProjectDir, "Saved"));
			DirsToClear.Add(DirectoryReference.Combine(CommandUtils.EngineDirectory, "DerivedDataCache"));
			DirsToClear.Add(DirectoryReference.Combine(ProjectDir, "DerivedDataCache"));

			string LocalDDC = Environment.GetEnvironmentVariable("UE-LocalDataCachePath");

			if (!string.IsNullOrEmpty(LocalDDC) && Directory.Exists(LocalDDC))
			{
				DirsToClear.Add(new DirectoryReference(LocalDDC));
			}

			foreach (var Dir in DirsToClear)
			{
				try
				{
					if (DirectoryReference.Exists(Dir))
					{
						Log.TraceInformation("Removing {0}", Dir);
						DirectoryReference.Delete(Dir, true);
					}
				}
				catch (Exception Ex)
				{
					Log.TraceWarning("Failed to remove path {0}. {1}", Dir.FullName, Ex.Message);
				}
			}
		}

		private DirectoryReference PrivateDDCPath = null;
		private string OldLocalDCCPath = null;

		private string GetLocalDDCPathEnvironmentKey()
		{
			string EnvVar = "UE-LocalDataCachePath";

			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Win64)
			{
				EnvVar = EnvVar.Replace("-", "_");
			}

			return EnvVar;
		}

		protected override bool PerformPrequisites()
		{
			if (TaskOptions.HasFlag(EditorTaskOptions.ColdDDC))
			{
				PrivateDDCPath = DirectoryReference.Combine(CommandUtils.EngineDirectory, "BenchmarkDerivedDataCache");

				if (DirectoryReference.Exists(PrivateDDCPath))
				{
					DirectoryReference.Delete(PrivateDDCPath, true);
				}

				string Key = GetLocalDDCPathEnvironmentKey();

				OldLocalDCCPath = Environment.GetEnvironmentVariable(Key);
				Environment.SetEnvironmentVariable(Key, PrivateDDCPath.FullName);

				DirectoryReference BootDDC = DirectoryReference.Combine(ProjectFile.Directory, "DerivedDataCache");

				try
				{
					DirectoryReference.Delete(BootDDC, true);
				}
				catch
				{

				}
			}

			return base.PerformPrequisites();
		}

		protected override void PerformCleanup()
		{
			if (PrivateDDCPath !=  null)
			{
				if (DirectoryReference.Exists(PrivateDDCPath))
				{
					DirectoryReference.Delete(PrivateDDCPath, true);
				}

				Environment.SetEnvironmentVariable(GetLocalDDCPathEnvironmentKey(), OldLocalDCCPath);
			}
		}
	}
}
