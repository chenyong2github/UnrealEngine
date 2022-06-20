// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using UnrealGameSync;

namespace UnrealGameSyncLauncher
{
	static class Program
	{
		[STAThread]
		static int Main(string[] Args)
		{
			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);

			bool bFirstInstance;
			using(Mutex InstanceMutex = new Mutex(true, "UnrealGameSyncRunning", out bFirstInstance))
			{
				if(!bFirstInstance)
				{
					using(EventWaitHandle ActivateEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "ActivateUnrealGameSync"))
					{
						ActivateEvent.Set();
					}
					return 0;
				}

				// Figure out if we should sync the unstable build by default
				bool bPreview = Args.Contains("-unstable", StringComparer.InvariantCultureIgnoreCase) || Args.Contains("-preview", StringComparer.InvariantCultureIgnoreCase);

				// Read the settings
				string? ServerAndPort = null;
				string? UserName = null;
				string? DepotPath = DeploymentSettings.DefaultDepotPath;
				GlobalPerforceSettings.ReadGlobalPerforceSettings(ref ServerAndPort, ref UserName, ref DepotPath, ref bPreview);

				// If the shift key is held down, immediately show the settings window
				SettingsWindow.SyncAndRunDelegate SyncAndRunWrapper = (Perforce, DepotParam, bPreviewParam, LogWriter, CancellationToken) => SyncAndRun(Perforce, DepotParam, bPreviewParam, Args, InstanceMutex, LogWriter, CancellationToken);
				if ((Control.ModifierKeys & Keys.Shift) != 0)
				{
					// Show the settings window immediately
					SettingsWindow UpdateError = new SettingsWindow(null, null, ServerAndPort, UserName, DepotPath, bPreview, SyncAndRunWrapper);
					if(UpdateError.ShowDialog() == DialogResult.OK)
					{
						return 0;
					}
				}
				else
				{
					// Try to do a sync with the current settings first
					CaptureLogger Logger = new CaptureLogger();

					IPerforceSettings Settings = new PerforceSettings(PerforceSettings.Default) { PreferNativeClient = true }.MergeWith(newServerAndPort: ServerAndPort, newUserName: UserName);

					ModalTask? Task = PerforceModalTask.Execute(null, "Updating", "Checking for updates, please wait...", Settings, (p, c) => SyncAndRun(p, DepotPath, bPreview, Args, InstanceMutex, Logger, c), Logger);
					if (Task == null)
					{
						Logger.LogInformation("Canceled by user");
					}
					else if (Task.Succeeded)
					{
						return 0;
					}

					SettingsWindow UpdateError = new SettingsWindow("Unable to update UnrealGameSync from Perforce. Verify that your connection settings are correct.", Logger.Render(Environment.NewLine), ServerAndPort, UserName, DepotPath, bPreview, SyncAndRunWrapper);
					if(UpdateError.ShowDialog() == DialogResult.OK)
					{
						return 0;
					}
				}
			}
			return 1;
		}

		public static async Task SyncAndRun(IPerforceConnection Perforce, string? BaseDepotPath, bool bPreview, string[] Args, Mutex InstanceMutex, ILogger Logger, CancellationToken CancellationToken)
		{
			try
			{
				if (String.IsNullOrEmpty(BaseDepotPath))
				{
					throw new UserErrorException($"Invalid setting for sync path");
				}

				string BaseDepotPathPrefix = BaseDepotPath.TrimEnd('/');

				// Find the most recent changelist
				string SyncPath = BaseDepotPathPrefix + (bPreview ? "/UnstableRelease.zip" : "/Release.zip");
				List<ChangesRecord> Changes = await Perforce.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, SyncPath, CancellationToken);
				if (Changes.Count == 0)
				{
					SyncPath = BaseDepotPathPrefix + (bPreview ? "/UnstableRelease/..." : "/Release/...");
					Changes = await Perforce.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, SyncPath, CancellationToken);
					if (Changes.Count == 0)
					{
						throw new UserErrorException($"Unable to find any UGS binaries under {SyncPath}");
					}
				}

				int RequiredChangeNumber = Changes[0].Number;
				Logger.LogInformation("Syncing from {SyncPath}", SyncPath);

				// Create the target folder
				string ApplicationFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "UnrealGameSync", "Latest");
				if (!SafeCreateDirectory(ApplicationFolder))
				{
					throw new UserErrorException($"Couldn't create directory: {ApplicationFolder}");
				}

				// Read the current version
				string SyncVersionFile = Path.Combine(ApplicationFolder, "SyncVersion.txt");
				string RequiredSyncText = String.Format("{0}\n{1}@{2}", Perforce.Settings.ServerAndPort ?? "", SyncPath, RequiredChangeNumber);

				// Check the application exists
				string ApplicationExe = Path.Combine(ApplicationFolder, "UnrealGameSync.exe");

				// Check if the version has changed
				string? SyncText;
				if (!File.Exists(SyncVersionFile) || !File.Exists(ApplicationExe) || !TryReadAllText(SyncVersionFile, out SyncText) || SyncText != RequiredSyncText)
				{
					// Try to delete the directory contents. Retry for a while, in case we've been spawned by an application in this folder to do an update.
					for (int NumRetries = 0; !SafeDeleteDirectoryContents(ApplicationFolder); NumRetries++)
					{
						if (NumRetries > 20)
						{
							throw new UserErrorException($"Couldn't delete contents of {ApplicationFolder} (retried {NumRetries} times).");
						}
						Thread.Sleep(500);
					}

					// Find all the files in the sync path at this changelist
					List<FStatRecord> FileRecords = await Perforce.FStatAsync(FStatOptions.None, $"{SyncPath}@{RequiredChangeNumber}", CancellationToken).ToListAsync(CancellationToken);
					if (FileRecords.Count == 0)
					{
						throw new UserErrorException($"Couldn't find any matching files for {SyncPath}@{RequiredChangeNumber}");
					}

					// Sync all the files in this list to the same directory structure under the application folder
					string DepotPathPrefix = SyncPath.Substring(0, SyncPath.LastIndexOf('/') + 1);
					foreach (FStatRecord FileRecord in FileRecords)
					{
						if (FileRecord.DepotFile == null)
						{
							throw new UserErrorException("Missing depot path for returned file");
						}

						string LocalPath = Path.Combine(ApplicationFolder, FileRecord.DepotFile.Substring(DepotPathPrefix.Length).Replace('/', Path.DirectorySeparatorChar));
						if (!SafeCreateDirectory(Path.GetDirectoryName(LocalPath)!))
						{
							throw new UserErrorException($"Couldn't create folder {Path.GetDirectoryName(LocalPath)}");
						}

						await Perforce.PrintAsync(LocalPath, FileRecord.DepotFile, CancellationToken);
					}

					// If it was a zip file, extract it
					if (SyncPath.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
					{
						string LocalPath = Path.Combine(ApplicationFolder, SyncPath.Substring(DepotPathPrefix.Length).Replace('/', Path.DirectorySeparatorChar));
						ZipFile.ExtractToDirectory(LocalPath, ApplicationFolder);
					}

					// Check the application exists
					if (!File.Exists(ApplicationExe))
					{
						throw new UserErrorException($"Application was not synced from Perforce. Check that UnrealGameSync exists at {SyncPath}/UnrealGameSync.exe, and you have access to it.");
					}

					// Update the version
					if (!TryWriteAllText(SyncVersionFile, RequiredSyncText))
					{
						throw new UserErrorException("Couldn't write sync text to {SyncVersionFile}");
					}
				}
				Logger.LogInformation("");

				// Build the command line for the synced application, including the sync path to monitor for updates
				string OriginalExecutable = Assembly.GetEntryAssembly()!.Location;
                if (Path.GetExtension(OriginalExecutable).Equals(".dll", StringComparison.OrdinalIgnoreCase))
                {
                    string NewExecutable = Path.ChangeExtension(OriginalExecutable, ".exe");
                    if (File.Exists(NewExecutable))
                    {
                        OriginalExecutable = NewExecutable;
                    }
                }

				StringBuilder NewCommandLine = new StringBuilder(String.Format("-updatepath=\"{0}@>{1}\" -updatespawn=\"{2}\"{3}", SyncPath, RequiredChangeNumber, OriginalExecutable, bPreview ? " -unstable" : ""));
				foreach (string Arg in Args)
				{
					NewCommandLine.AppendFormat(" {0}", QuoteArgument(Arg));
				}

				// Release the mutex now so that the new application can start up
				InstanceMutex.Close();

				// Spawn the application
				Logger.LogInformation("Spawning {App} with command line: {CmdLine}", ApplicationExe, NewCommandLine.ToString());
				using (Process ChildProcess = new Process())
				{
					ChildProcess.StartInfo.FileName = ApplicationExe;
					ChildProcess.StartInfo.Arguments = NewCommandLine.ToString();
					ChildProcess.StartInfo.UseShellExecute = false;
					ChildProcess.StartInfo.CreateNoWindow = false;
					if (!ChildProcess.Start())
					{
						throw new UserErrorException("Failed to start process");
					}
				}
			}
			catch (UserErrorException Ex)
			{
				Logger.LogError("{Message}", Ex.Message);
				throw;
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Error while syncing application.");
				foreach (string Line in Ex.ToString().Split('\n'))
				{
					Logger.LogError("{Line}", Line);
				}
				throw;
			}
		}

		static string QuoteArgument(string Arg)
		{
			if(Arg.IndexOf(' ') != -1 && !Arg.StartsWith("\""))
			{
				return String.Format("\"{0}\"", Arg);
			}
			else
			{
				return Arg;
			}
		}

		static bool TryReadAllText(string FileName, [NotNullWhen(true)] out string? Text)
		{
			try
			{
				Text = File.ReadAllText(FileName);
				return true;
			}
			catch(Exception)
			{
				Text = null;
				return false;
			}
		}

		static bool TryWriteAllText(string FileName, string Text)
		{
			try
			{
				File.WriteAllText(FileName, Text);
				return true;
			}
			catch(Exception)
			{
				return false;
			}
		}

		static bool SafeCreateDirectory(string DirectoryName)
		{
			try
			{
				Directory.CreateDirectory(DirectoryName);
				return true;
			}
			catch(Exception)
			{
				return false;
			}
		}

		static bool SafeDeleteDirectory(string DirectoryName)
		{
			try
			{
				Directory.Delete(DirectoryName, true);
				return true;
			}
			catch(Exception)
			{
				return false;
			}
		}

		static bool SafeDeleteDirectoryContents(string DirectoryName)
		{
			try
			{
				DirectoryInfo Directory = new DirectoryInfo(DirectoryName);
				foreach(FileInfo ChildFile in Directory.EnumerateFiles("*", SearchOption.AllDirectories))
				{
					ChildFile.Attributes = ChildFile.Attributes & ~FileAttributes.ReadOnly;
					ChildFile.Delete();
				}
				foreach(DirectoryInfo ChildDirectory in Directory.EnumerateDirectories())
				{
					SafeDeleteDirectory(ChildDirectory.FullName);
				}
				return true;
			}
			catch(Exception)
			{
				return false;
			}
		}
	}
}
