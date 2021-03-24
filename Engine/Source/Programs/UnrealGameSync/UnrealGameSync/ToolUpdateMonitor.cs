// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	class ToolLink
	{
		public string Label;
		public string FileName;
		public string Arguments;
		public string WorkingDir;
	}

	class ToolDefinition
	{
		public Guid Id;
		public string Name;
		public string Description;
		public bool Enabled;
		public Action<TextWriter> InstallAction;
		public Action<TextWriter> UninstallAction;
		public List<ToolLink> StatusPanelLinks = new List<ToolLink>();
		public string ZipPath;
		public int ZipChange;
		public string ConfigPath;
		public int ConfigChange;
	}

	class ToolUpdateMonitor : IDisposable
	{
		Thread WorkerThread;
		AutoResetEvent WakeEvent;
		bool bQuit;
		string LogFile;
		public List<ToolDefinition> Tools { get; private set; } = new List<ToolDefinition>();
		int LastChange = -1;

		public PerforceConnection Perforce { get; }
		string ToolsDir { get; }
		UserSettings Settings { get; }

		public event Action OnChange;

		public ToolUpdateMonitor(PerforceConnection InPerforce, string DataDir, UserSettings Settings)
		{
			this.Perforce = InPerforce;
			this.ToolsDir = Path.Combine(DataDir, "Tools");
			this.Settings = Settings;

			LogFile = Path.Combine(DataDir, "Tools.log");

			Directory.CreateDirectory(ToolsDir);

			WakeEvent = new AutoResetEvent(false);
		}

		public void Start()
		{
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
			List<PerforceChangeSummary> Changes;
			if (!Perforce.FindChanges(DeploymentSettings.ToolsDepotPath + "/...", 1, out Changes, Log) || Changes.Count == 0 || Changes[0].Number == LastChange)
			{
				return;
			}

			List<PerforceFileRecord> FileRecords;
			if (!Perforce.Stat(DeploymentSettings.ToolsDepotPath + "/...", out FileRecords, Log))
			{
				return;
			}

			// Update the tools list
			List<ToolDefinition> NewTools = new List<ToolDefinition>();
			foreach (PerforceFileRecord FileRecord in FileRecords)
			{
				if (FileRecord.DepotPath.EndsWith(".ini"))
				{
					ToolDefinition Tool = Tools.FirstOrDefault(x => x.ConfigPath.Equals(FileRecord.DepotPath, StringComparison.Ordinal));
					if (Tool == null || Tool.ConfigChange != FileRecord.HeadChange)
					{
						Tool = ReadToolDefinition(FileRecord.DepotPath, FileRecord.HeadChange, Log);
					}
					if (Tool != null)
					{
						NewTools.Add(Tool);
					}
				}
			}
			Tools = NewTools;

			foreach (ToolDefinition Tool in Tools)
			{
				Tool.Enabled = Settings.EnabledTools.Contains(Tool.Id);

				if(!Tool.Enabled)
				{
					continue;
				}

				List<PerforceFileRecord> ToolFileRecords = FileRecords.Where(x => x.DepotPath.Equals(Tool.ZipPath, StringComparison.OrdinalIgnoreCase)).ToList();
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

			foreach (ToolDefinition Tool in Tools)
			{
				if (!Tool.Enabled && GetToolChange(Tool.Name) != 0)
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

			OnChange();
		}

		ToolDefinition ReadToolDefinition(string DepotPath, int Change, TextWriter Log)
		{
			List<string> Lines;
			if (!Perforce.Print(String.Format("{0}@{1}", DepotPath, Change), out Lines, Log))
			{
				return null;
			}

			int NameIdx = DepotPath.LastIndexOf('/') + 1;
			int ExtensionIdx = DepotPath.LastIndexOf('.');

			ConfigFile ConfigFile = new ConfigFile();
			ConfigFile.Parse(Lines.ToArray());

			ToolDefinition Tool = new ToolDefinition();

			string Id = ConfigFile.GetValue("Settings.Id", null);
			if (Id == null || !Guid.TryParse(Id, out Tool.Id))
			{
				return null;
			}

			Tool.Name = ConfigFile.GetValue("Settings.Name", DepotPath.Substring(NameIdx, ExtensionIdx - NameIdx));
			Tool.Description = ConfigFile.GetValue("Settings.Description", Tool.Name);
			Tool.ZipPath = DepotPath.Substring(0, ExtensionIdx) + ".zip";
			Tool.ZipChange = GetToolChange(Tool.Name);
			Tool.ConfigPath = DepotPath;
			Tool.ConfigChange = Change;

			string InstallCommand = ConfigFile.GetValue("Settings.InstallCommand", null);
			if (!String.IsNullOrEmpty(InstallCommand))
			{
				Tool.InstallAction = NewLog => RunCommand(Tool.Name, InstallCommand, NewLog);
			}

			string UninstallCommand = ConfigFile.GetValue("Settings.UninstallCommand", null);
			if (!String.IsNullOrEmpty(UninstallCommand))
			{
				Tool.UninstallAction = NewLog => RunCommand(Tool.Name, UninstallCommand, NewLog);
			}

			string[] StatusPanelLinks = ConfigFile.GetValues("Settings.StatusPanelLinks", new string[0]);
			foreach (string StatusPanelLink in StatusPanelLinks)
			{
				ConfigObject Object = new ConfigObject(StatusPanelLink);

				string Label = Object.GetValue("Label", null);
				string FileName = Object.GetValue("FileName", null);

				if (Label != null && FileName != null)
				{
					ToolLink Link = new ToolLink();
					Link.Label = Label;
					Link.FileName = FileName;
					Link.Arguments = Object.GetValue("Arguments", null);
					Link.WorkingDir = Object.GetValue("WorkingDir", null);
					Tool.StatusPanelLinks.Add(Link);
				}
			}

			return Tool;
		}

		void RunCommand(string ToolName, string Command, TextWriter Log)
		{
			string ToolPath = GetToolPathInternal(ToolName);

			string CommandExe = Command;
			string CommandArgs = string.Empty;

			int SpaceIdx = Command.IndexOf(' ');
			if (SpaceIdx != -1)
			{
				CommandExe = Command.Substring(0, SpaceIdx);
				CommandArgs = Command.Substring(SpaceIdx + 1);
			}

			int ExitCode = Utility.ExecuteProcess(Path.Combine(ToolPath, CommandExe), ToolPath, CommandArgs, null, Log);
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
			if (ToolPath != null)
			{
				Log.WriteLine("Removing {0}", ToolPath);
				TryDeleteDirectory(ToolPath);
			}
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
