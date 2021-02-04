// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	class ToolUpdateMonitor : IDisposable
	{
		Thread WorkerThread;
		AutoResetEvent WakeEvent;
		bool bQuit;
		string LogFile;

		public PerforceConnection Perforce { get; }
		string ToolsDir { get; }
		UserSettings Settings { get; }

		class ToolDefinition
		{
			public string Name;
			public Action<TextWriter> InstallAction;
			public Action<TextWriter> UninstallAction;
			public bool bEnabled;

			public ToolDefinition(string Name, Action<TextWriter> InstallAction, Action<TextWriter> UninstallAction, bool bEnabled)
			{
				this.Name = Name;
				this.InstallAction = InstallAction;
				this.UninstallAction = UninstallAction;
				this.bEnabled = bEnabled;
			}
		}


		public ToolUpdateMonitor(PerforceConnection InPerforce, string DataDir, UserSettings Settings)
		{
			this.Perforce = InPerforce;
			this.ToolsDir = Path.Combine(DataDir, "Tools");
			this.Settings = Settings;

			LogFile = Path.Combine(DataDir, "Tools.log");

			Directory.CreateDirectory(ToolsDir);

			WakeEvent = new AutoResetEvent(false);

			if (DeploymentSettings.ToolsDepotPath != null)
			{
				WorkerThread = new Thread(() => PollForUpdates());
				WorkerThread.Start();
			}
		}

		public void Close()
		{
			bQuit = true;
			WakeEvent.Set();

			if (WorkerThread != null)
			{
				if (!WorkerThread.Join(30))
				{
					WorkerThread.Abort();
					WorkerThread.Join();
				}
				WorkerThread = null;
			}
		}

		public void Dispose()
		{
			Close();
		}

		string GetToolPathInternal(string ToolName)
		{
			return Path.Combine(ToolsDir, ToolName, "Current");
		}

		public string GetToolPath(string ToolName)
		{
			if (GetToolChange(ToolName) != 0)
			{
				return GetToolPathInternal(ToolName);
			}
			else
			{
				return null;
			}
		}

		public void UpdateNow()
		{
			WakeEvent.Set();
		}

		void PollForUpdates()
		{
			using (BoundedLogWriter Log = new BoundedLogWriter(LogFile))
			{
				while (!bQuit)
				{
					try
					{
						PollForUpdatesOnce(Log);
					}
					catch
					{
					}

					WakeEvent.WaitOne(TimeSpan.FromMinutes(60.0));
				}
			}
		}

		void PollForUpdatesOnce(TextWriter Log)
		{
			List<ToolDefinition> Tools = new List<ToolDefinition>();
			Tools.Add(new ToolDefinition("P4VUtils", x => RunP4VUtils("install", x), x => RunP4VUtils("uninstall", x), Settings.bEnableP4VExtensions));
			Tools.Add(new ToolDefinition("Ushell", null, null, Settings.bEnableUshell));

			if (Tools.Any(x => x.bEnabled))
			{
				List<PerforceFileRecord> FileRecords;
				if (Perforce.Stat(DeploymentSettings.ToolsDepotPath + "/...", out FileRecords, Log))
				{
					foreach (ToolDefinition Tool in Tools.Where(x => x.bEnabled))
					{
						string Prefix = String.Format("{0}/{1}/", DeploymentSettings.ToolsDepotPath, Tool.Name);

						List<PerforceFileRecord> ToolFileRecords = FileRecords.Where(x => x.DepotPath.StartsWith(Prefix, StringComparison.OrdinalIgnoreCase)).ToList();
						if (ToolFileRecords.Count == 0)
						{
							continue;
						}

						int HeadChange = ToolFileRecords.Max(x => x.HeadChange);
						if (HeadChange == GetToolChange(Tool.Name))
						{
							continue;
						}

						List<PerforceFileRecord> SyncFileRecords = ToolFileRecords.Where(x => x.Action != "delete").ToList();
						try
						{
							UpdateTool(Tool.Name, HeadChange, SyncFileRecords, Tool.InstallAction, Log);
						}
						catch (Exception Ex)
						{
							Log.WriteLine("Exception while updating tool: {0}", Ex.ToString());
						}
					}
				}
			}

			foreach (ToolDefinition Tool in Tools.Where(x => !x.bEnabled))
			{
				if (GetToolChange(Tool.Name) != 0)
				{
					try
					{
						RemoveTool(Tool.Name, Tool.UninstallAction, Log);
					}
					catch (Exception Ex)
					{
						Log.WriteLine("Exception while removing tool: {0}", Ex.ToString());
					}
				}
			}
		}

		void RunP4VUtils(string Command, TextWriter Log)
		{
			string ToolPath = GetToolPathInternal("P4VUtils");

			int ExitCode = Utility.ExecuteProcess(Path.Combine(ToolPath, "P4VUtils.exe"), ToolPath, Command, null, Log);
			Log.WriteLine("(Exit: {0})", ExitCode);
		}

		void RemoveTool(string ToolName, Action<TextWriter> UninstallAction, TextWriter Log)
		{
			Log.WriteLine("Removing {0}", ToolName);

			if (UninstallAction != null)
			{
				Log.WriteLine("Running uninstall...");
				UninstallAction?.Invoke(Log);
			}

			SetToolChange(ToolName, null);

			string ToolPath = GetToolPath(ToolName);
			Log.WriteLine("Removing {0}", ToolPath);

			TryDeleteDirectory(ToolPath);
		}

		static void ForceDeleteDirectory(string DirectoryName)
		{
			DirectoryInfo BaseDir = new DirectoryInfo(DirectoryName);
			if (BaseDir.Exists)
			{
				foreach (FileInfo File in BaseDir.EnumerateFiles("*", SearchOption.AllDirectories))
				{
					File.Attributes = FileAttributes.Normal;
				}
				BaseDir.Delete(true);
			}
		}

		static bool TryDeleteDirectory(string DirectoryName)
		{
			try
			{
				ForceDeleteDirectory(DirectoryName);
				return true;
			}
			catch
			{
				return false;
			}
		}

		bool UpdateTool(string ToolName, int Change, List<PerforceFileRecord> Records, Action<TextWriter> InstallAction, TextWriter Log)
		{
			string ToolDir = Path.Combine(ToolsDir, ToolName);
			Directory.CreateDirectory(ToolDir);

			foreach (DirectoryInfo ExistingDir in new DirectoryInfo(ToolDir).EnumerateDirectories("Prev-*"))
			{
				TryDeleteDirectory(ExistingDir.FullName);
			}

			string NextToolDir = Path.Combine(ToolDir, "Next");
			ForceDeleteDirectory(NextToolDir);
			Directory.CreateDirectory(NextToolDir);

			string NextToolZipsDir = Path.Combine(NextToolDir, ".zips");
			Directory.CreateDirectory(NextToolZipsDir);

			for (int Idx = 0; Idx < Records.Count; Idx++)
			{
				string ZipFile = Path.Combine(NextToolZipsDir, String.Format("{0}.{1}.zip", ToolName, Idx));
				if (!Perforce.PrintToFile(String.Format("{0}#{1}", Records[Idx].DepotPath, Records[Idx].HeadRevision), ZipFile, Log) || !File.Exists(ZipFile))
				{
					Log.WriteLine("Unable to print {0}", Records[Idx].DepotPath);
					return false;
				}
				ArchiveUtils.ExtractFiles(ZipFile, NextToolDir, null, new ProgressValue(), Log);
			}

			SetToolChange(ToolName, null);

			string CurrentToolDir = Path.Combine(ToolDir, "Current");
			if (Directory.Exists(CurrentToolDir))
			{
				string PrevDirectoryName = Path.Combine(ToolDir, String.Format("Prev-{0:X16}", Stopwatch.GetTimestamp()));
				Directory.Move(CurrentToolDir, PrevDirectoryName);
				TryDeleteDirectory(PrevDirectoryName);
			}

			Directory.Move(NextToolDir, CurrentToolDir);

			if (InstallAction != null)
			{
				Log.WriteLine("Running installer...");
				InstallAction.Invoke(Log);
			}

			SetToolChange(ToolName, Change);
			return true;
		}

		string GetConfigFilePath(string ToolName)
		{
			return Path.Combine(ToolsDir, ToolName, ToolName + ".ini");
		}

		int GetToolChange(string ToolName)
		{
			try
			{
				string ConfigFilePath = GetConfigFilePath(ToolName);
				if (File.Exists(ConfigFilePath))
				{
					ConfigFile ConfigFile = new ConfigFile();
					ConfigFile.Load(ConfigFilePath);
					return ConfigFile.GetValue("Settings.Change", 0);
				}
			}
			catch
			{
			}
			return 0;
		}

		void SetToolChange(string ToolName, int? Change)
		{
			string ConfigFilePath = GetConfigFilePath(ToolName);
			if (Change.HasValue)
			{
				ConfigFile ConfigFile = new ConfigFile();
				ConfigFile.SetValue("Settings.Change", Change.Value);
				ConfigFile.Save(ConfigFilePath);
			}
			else
			{
				File.Delete(ConfigFilePath);
			}
		}
	}
}
