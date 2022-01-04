// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	class ToolLink
	{
		public string Label;
		public string FileName;
		public string? Arguments;
		public string? WorkingDir;

		public ToolLink(string Label, string FileName)
		{
			this.Label = Label;
			this.FileName = FileName;
		}
	}

	class ToolDefinition
	{
		public Guid Id;
		public string Name;
		public string Description;
		public bool Enabled;
		public Func<ILogger, CancellationToken, Task>? InstallAction;
		public Func<ILogger, CancellationToken, Task>? UninstallAction;
		public List<ToolLink> StatusPanelLinks = new List<ToolLink>();
		public string ZipPath;
		public int ZipChange;
		public string ConfigPath;
		public int ConfigChange;

		public ToolDefinition(Guid Id, string Name, string Description, string ZipPath, int ZipChange, string ConfigPath, int ConfigChange)
		{
			this.Id = Id;
			this.Name = Name;
			this.Description = Description;
			this.ZipPath = ZipPath;
			this.ZipChange = ZipChange;
			this.ConfigPath = ConfigPath;
			this.ConfigChange = ConfigChange;
		}
	}

	class ToolUpdateMonitor : IDisposable
	{
		CancellationTokenSource CancellationSource;
		SynchronizationContext SynchronizationContext;
		Task? WorkerTask;
		AsyncEvent WakeEvent;
		ILogger Logger;
		public List<ToolDefinition> Tools { get; private set; } = new List<ToolDefinition>();
		int LastChange = -1;
		IAsyncDisposer AsyncDisposer;

		IPerforceSettings PerforceSettings { get; }
		DirectoryReference ToolsDir { get; }
		UserSettings Settings { get; }

		public Action? OnChange;

		public ToolUpdateMonitor(IPerforceSettings PerforceSettings, DirectoryReference DataDir, UserSettings Settings, IServiceProvider ServiceProvider)
		{
			CancellationSource = new CancellationTokenSource();
			SynchronizationContext = SynchronizationContext.Current!;
			this.ToolsDir = DirectoryReference.Combine(DataDir, "Tools");
			this.PerforceSettings = PerforceSettings;
			this.Settings = Settings;
			this.Logger = ServiceProvider.GetRequiredService<ILogger<ToolUpdateMonitor>>();
			this.AsyncDisposer = ServiceProvider.GetRequiredService<IAsyncDisposer>();

			DirectoryReference.CreateDirectory(ToolsDir);

			WakeEvent = new AsyncEvent();
		}

		public void Start()
		{
			if (DeploymentSettings.ToolsDepotPath != null)
			{
				WorkerTask = Task.Run(() => PollForUpdatesAsync(CancellationSource.Token));
			}
		}

		public void Dispose()
		{
			OnChange = null;

			if (WorkerTask != null)
			{
				CancellationSource.Cancel();
				AsyncDisposer.Add(WorkerTask.ContinueWith(_ => CancellationSource.Dispose()));
				WorkerTask = null;
			}
		}

		DirectoryReference GetToolPathInternal(string ToolName)
		{
			return DirectoryReference.Combine(ToolsDir, ToolName, "Current");
		}

		public string? GetToolName(Guid ToolId)
		{
			foreach (ToolDefinition Tool in Tools)
			{
				if(Tool.Id == ToolId)
				{
					return Tool.Name;
				}
			}
			return null;
		}

		public DirectoryReference? GetToolPath(string ToolName)
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

		async Task PollForUpdatesAsync(CancellationToken CancellationToken)
		{
			while (!CancellationToken.IsCancellationRequested)
			{
				Task WakeTask = WakeEvent.Task;

				try
				{
					await PollForUpdatesOnce(Logger, CancellationToken);
				}
				catch(Exception Ex)
				{
					Logger.LogError(Ex, "Exception while checking for tool updates");
				}

				Task DelayTask = Task.Delay(TimeSpan.FromMinutes(60.0), CancellationToken);
				await Task.WhenAny(DelayTask, WakeTask);
			}
		}

		async Task PollForUpdatesOnce(ILogger Logger, CancellationToken CancellationToken)
		{
			using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger);

			List<ChangesRecord> Changes = await Perforce.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, $"{DeploymentSettings.ToolsDepotPath}/...", CancellationToken);
			if (Changes.Count == 0 || Changes[0].Number == LastChange)
			{
				return;
			}

			List<FStatRecord> FileRecords = await Perforce.FStatAsync($"{DeploymentSettings.ToolsDepotPath}/...", CancellationToken).ToListAsync(CancellationToken);

			// Update the tools list
			List<ToolDefinition> NewTools = new List<ToolDefinition>();
			foreach (FStatRecord FileRecord in FileRecords)
			{
				if (FileRecord.DepotFile != null && FileRecord.DepotFile.EndsWith(".ini"))
				{
					ToolDefinition? Tool = Tools.FirstOrDefault(x => x.ConfigPath.Equals(FileRecord.DepotFile, StringComparison.Ordinal));
					if (Tool == null || Tool.ConfigChange != FileRecord.HeadChange)
					{
						Tool = await ReadToolDefinitionAsync(Perforce, FileRecord.DepotFile, FileRecord.HeadChange, Logger, CancellationToken);
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

				List<FStatRecord> ToolFileRecords = FileRecords.Where(x => String.Equals(x.DepotFile, Tool.ZipPath, StringComparison.OrdinalIgnoreCase)).ToList();
				if (ToolFileRecords.Count == 0)
				{
					continue;
				}

				int HeadChange = ToolFileRecords.Max(x => x.HeadChange);
				if (HeadChange == GetToolChange(Tool.Name))
				{
					continue;
				}

				List<FStatRecord> SyncFileRecords = ToolFileRecords.Where(x => x.Action != FileAction.Delete && x.Action != FileAction.MoveDelete).ToList();
				try
				{
					await UpdateToolAsync(Perforce, Tool.Name, HeadChange, SyncFileRecords, Tool.InstallAction, Logger, CancellationToken);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception while updating tool");
				}
			}

			foreach (ToolDefinition Tool in Tools)
			{
				if (!Tool.Enabled && GetToolChange(Tool.Name) != 0)
				{
					try
					{
						await RemoveToolAsync(Tool.Name, Tool.UninstallAction, Logger, CancellationToken);
					}
					catch (Exception Ex)
					{
						Logger.LogError(Ex, "Exception while removing tool");
					}
				}
			}

			SynchronizationContext.Post(_ => OnChange?.Invoke(), null);
		}

		async Task<ToolDefinition?> ReadToolDefinitionAsync(IPerforceConnection Perforce, string DepotPath, int Change, ILogger Logger, CancellationToken CancellationToken)
		{
			PerforceResponse<PrintRecord<string[]>> Response = await Perforce.TryPrintLinesAsync($"{DepotPath}@{Change}", CancellationToken);
			if (!Response.Succeeded || Response.Data.Contents == null)
			{
				return null;
			}

			int NameIdx = DepotPath.LastIndexOf('/') + 1;
			int ExtensionIdx = DepotPath.LastIndexOf('.');

			ConfigFile ConfigFile = new ConfigFile();
			ConfigFile.Parse(Response.Data.Contents);

			string? Id = ConfigFile.GetValue("Settings.Id", null);
			if (Id == null || !Guid.TryParse(Id, out Guid ToolId))
			{
				return null;
			}

			string ToolName = ConfigFile.GetValue("Settings.Name", DepotPath.Substring(NameIdx, ExtensionIdx - NameIdx));
			string ToolDescription = ConfigFile.GetValue("Settings.Description", ToolName);
			string ToolZipPath = DepotPath.Substring(0, ExtensionIdx) + ".zip";
			int ToolZipChange = GetToolChange(ToolName);
			string ToolConfigPath = DepotPath;
			int ToolConfigChange = Change;

			ToolDefinition Tool = new ToolDefinition(ToolId, ToolName, ToolDescription, ToolZipPath, ToolZipChange, ToolConfigPath, ToolConfigChange);

			string? InstallCommand = ConfigFile.GetValue("Settings.InstallCommand", null);
			if (!String.IsNullOrEmpty(InstallCommand))
			{
				Tool.InstallAction = async (Logger, CancellationToken) =>
				{
					Logger.LogInformation("Running install action: {Command}", InstallCommand);
					await RunCommandAsync(Tool.Name, InstallCommand, Logger, CancellationToken);
				};
			}

			string? UninstallCommand = ConfigFile.GetValue("Settings.UninstallCommand", null);
			if (!String.IsNullOrEmpty(UninstallCommand))
			{
				Tool.UninstallAction = async (Logger, CancellationToken) =>
				{
					Logger.LogInformation("Running unininstall action: {Command}", UninstallCommand);
					await RunCommandAsync(Tool.Name, UninstallCommand, Logger, CancellationToken);
				};
			}

			string[] StatusPanelLinks = ConfigFile.GetValues("Settings.StatusPanelLinks", new string[0]);
			foreach (string StatusPanelLink in StatusPanelLinks)
			{
				ConfigObject Object = new ConfigObject(StatusPanelLink);

				string? Label = Object.GetValue("Label", null);
				string? FileName = Object.GetValue("FileName", null);

				if (Label != null && FileName != null)
				{
					ToolLink Link = new ToolLink(Label, FileName);
					Link.Arguments = Object.GetValue("Arguments", null);
					Link.WorkingDir = Object.GetValue("WorkingDir", null);
					Tool.StatusPanelLinks.Add(Link);
				}
			}

			return Tool;
		}

		async Task RunCommandAsync(string ToolName, string Command, ILogger Logger, CancellationToken CancellationToken)
		{
			DirectoryReference ToolPath = GetToolPathInternal(ToolName);

			string CommandExe = Command;
			string CommandArgs = string.Empty;

			int SpaceIdx = Command.IndexOf(' ');
			if (SpaceIdx != -1)
			{
				CommandExe = Command.Substring(0, SpaceIdx);
				CommandArgs = Command.Substring(SpaceIdx + 1);
			}

			int ExitCode = await Utility.ExecuteProcessAsync(FileReference.Combine(ToolPath, CommandExe).FullName, ToolPath.FullName, CommandArgs, Line => Logger.LogInformation("{ToolName}> {Line}", ToolName, Line), CancellationToken);
			Logger.LogInformation("{ToolName}> Exit code {ExitCode})", ToolName, ExitCode);
		}

		async Task RemoveToolAsync(string ToolName, Func<ILogger, CancellationToken, Task>? UninstallAction, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Removing {0}", ToolName);
			DirectoryReference? ToolPath = GetToolPath(ToolName);

			if (UninstallAction != null)
			{
				Logger.LogInformation("Running uninstall...");
				await UninstallAction(Logger, CancellationToken);
			}

			SetToolChange(ToolName, null);

			if (ToolPath != null)
			{
				Logger.LogInformation("Removing {ToolPath}", ToolPath);
				TryDeleteDirectory(ToolPath, Logger);
			}

			Logger.LogInformation("{ToolName} has been removed successfully", ToolName);
		}

		static void ForceDeleteDirectory(DirectoryReference DirectoryName)
		{
			DirectoryInfo BaseDir = DirectoryName.ToDirectoryInfo();
			if (BaseDir.Exists)
			{
				foreach (FileInfo File in BaseDir.EnumerateFiles("*", SearchOption.AllDirectories))
				{
					File.Attributes = FileAttributes.Normal;
				}
				BaseDir.Delete(true);
			}
		}

		static bool TryDeleteDirectory(DirectoryReference DirectoryName, ILogger Logger)
		{
			try
			{
				ForceDeleteDirectory(DirectoryName);
				return true;
			}
			catch(Exception Ex)
			{
				Logger.LogWarning(Ex, "Unable to delete directory {DirectoryName}", DirectoryName);
				return false;
			}
		}

		async Task<bool> UpdateToolAsync(IPerforceConnection Perforce, string ToolName, int Change, List<FStatRecord> Records, Func<ILogger, CancellationToken, Task>? InstallAction, ILogger Logger, CancellationToken CancellationToken)
		{
			DirectoryReference ToolDir = DirectoryReference.Combine(ToolsDir, ToolName);
			DirectoryReference.CreateDirectory(ToolDir);

			foreach (DirectoryReference ExistingDir in DirectoryReference.EnumerateDirectories(ToolDir, "Prev-*"))
			{
				TryDeleteDirectory(ExistingDir, Logger);
			}

			DirectoryReference NextToolDir = DirectoryReference.Combine(ToolDir, "Next");
			ForceDeleteDirectory(NextToolDir);
			DirectoryReference.CreateDirectory(NextToolDir);

			DirectoryReference NextToolZipsDir = DirectoryReference.Combine(NextToolDir, ".zips");
			DirectoryReference.CreateDirectory(NextToolZipsDir);

			for (int Idx = 0; Idx < Records.Count; Idx++)
			{
				FileReference ZipFile = FileReference.Combine(NextToolZipsDir, String.Format("{0}.{1}.zip", ToolName, Idx));

				PerforceResponse<PrintRecord> Response = await Perforce.TryPrintAsync(ZipFile.FullName, $"{Records[Idx].DepotFile}#{Records[Idx].HeadRevision}", CancellationToken);
				if(!Response.Succeeded || !FileReference.Exists(ZipFile))
				{
					Logger.LogError("Unable to print {0}", Records[Idx].DepotFile);
					return false;
				}

				ArchiveUtils.ExtractFiles(ZipFile, NextToolDir, null, new ProgressValue(), Logger);
			}

			SetToolChange(ToolName, null);

			DirectoryReference CurrentToolDir = DirectoryReference.Combine(ToolDir, "Current");
			if (DirectoryReference.Exists(CurrentToolDir))
			{
				DirectoryReference PrevDirectoryName = DirectoryReference.Combine(ToolDir, String.Format("Prev-{0:X16}", Stopwatch.GetTimestamp()));
				Directory.Move(CurrentToolDir.FullName, PrevDirectoryName.FullName);
				TryDeleteDirectory(PrevDirectoryName, Logger);
			}

			Directory.Move(NextToolDir.FullName, CurrentToolDir.FullName);

			if (InstallAction != null)
			{
				Logger.LogInformation("Running installer...");
				await InstallAction.Invoke(Logger, CancellationToken);
			}

			SetToolChange(ToolName, Change);
			Logger.LogInformation("Updated {ToolName} to change {Change}", ToolName, Change);
			return true;
		}

		FileReference GetConfigFilePath(string ToolName)
		{
			return FileReference.Combine(ToolsDir, ToolName, ToolName + ".ini");
		}

		int GetToolChange(string ToolName)
		{
			try
			{
				FileReference ConfigFilePath = GetConfigFilePath(ToolName);
				if (FileReference.Exists(ConfigFilePath))
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
			FileReference ConfigFilePath = GetConfigFilePath(ToolName);
			if (Change.HasValue)
			{
				ConfigFile ConfigFile = new ConfigFile();
				ConfigFile.SetValue("Settings.Change", Change.Value);
				ConfigFile.Save(ConfigFilePath);
			}
			else
			{
				FileReference.Delete(ConfigFilePath);
			}
		}
	}
}
