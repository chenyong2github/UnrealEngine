// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Services;
using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Management;
using System.Reflection;
using System.ServiceProcess;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

namespace HordeAgent.Commands
{
	/// <summary>
	/// Upgrades a running service to the current application
	/// </summary>
	[Command("Service", "Upgrade", "Replaces a running service with the application in the current directory")]
	class UpgradeCommand : Command
	{
		/// <summary>
		/// The process id to replace
		/// </summary>
		[CommandLine("-ProcessId=", Required = true)]
		int ProcessId = -1;

		/// <summary>
		/// The target directory to install to
		/// </summary>
		[CommandLine("-TargetDir=", Required = true)]
		DirectoryReference TargetDir = null!;

		/// <summary>
		/// Arguments to forwar to the target executable
		/// </summary>
		[CommandLine("-Arguments=", Required = true)]
		string Arguments = null!;

		/// <summary>
		/// Upgrades the application to a new version
		/// </summary>
		/// <param name="Logger">The log output device</param>
		/// <returns>True if the upgrade succeeded</returns>
		public override Task<int> ExecuteAsync(ILogger Logger)
		{
			// Stop the other process
			Logger.LogInformation("Attempting to perform upgrade on process {ProcessId}", ProcessId);
			using (Process OtherProcess = Process.GetProcessById(ProcessId))
			{
				// Get the directory containing the target application
				DirectoryInfo TargetDir = new DirectoryInfo(this.TargetDir.FullName);
				HashSet<string> TargetFiles = new HashSet<string>(TargetDir.EnumerateFiles("*", SearchOption.AllDirectories).Select(x => x.FullName), StringComparer.OrdinalIgnoreCase);

				// Find all the source files
				DirectoryInfo SourceDir = new DirectoryInfo(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location));
				HashSet<string> SourceFiles = new HashSet<string>(SourceDir.EnumerateFiles("*", SearchOption.AllDirectories).Select(x => x.FullName), StringComparer.OrdinalIgnoreCase);

				// Exclude all the source files from the list of target files, since we may be in a subdirectory
				TargetFiles.ExceptWith(SourceFiles);

				// Ignore any files that are in the saved directory
				string SourceDataDir = Path.Combine(SourceDir.FullName, "Saved") + Path.DirectorySeparatorChar;
				SourceFiles.RemoveWhere(x => x.StartsWith(SourceDataDir, StringComparison.OrdinalIgnoreCase));

				string TargetDataDir = Path.Combine(TargetDir.FullName, "Saved") + Path.DirectorySeparatorChar;
				TargetFiles.RemoveWhere(x => x.StartsWith(TargetDataDir, StringComparison.OrdinalIgnoreCase));

				// Copy all the files into the target directory
				List<Tuple<string, string>> RenameFiles = new List<Tuple<string, string>>();
				foreach (string SourceFile in SourceFiles)
				{
					if (!SourceFile.StartsWith(SourceDir.FullName, StringComparison.OrdinalIgnoreCase))
					{
						throw new InvalidDataException($"Expected {SourceFile} to be under {SourceDir.FullName}");
					}

					string TargetFile = TargetDir.FullName + SourceFile.Substring(SourceDir.FullName.Length);
					Directory.CreateDirectory(Path.GetDirectoryName(TargetFile));

					string TargetFileBeforeRename = TargetFile + ".new";
					Logger.LogDebug("Copying {SourceFile} to {TargetFileBeforeRename}", SourceFile, TargetFileBeforeRename);
					File.Copy(SourceFile, TargetFileBeforeRename, true);

					RenameFiles.Add(Tuple.Create(TargetFileBeforeRename, TargetFile));
					TargetFiles.Remove(TargetFileBeforeRename);
				}

				if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					UpgradeWindowsService(Logger, OtherProcess, TargetFiles, RenameFiles);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
				{
					UpgradeMacService(Logger, OtherProcess, TargetFiles, RenameFiles);
				}
				else if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
				{
					UpgradeLinuxService(Logger, OtherProcess, TargetFiles, RenameFiles);
				}
				else
				{
					Logger.LogError("Agent is not running a platform that supports Upgrades. Platform: {0}", RuntimeInformation.OSDescription);
					return Task.FromResult(-1);
				}
			}
			Logger.LogInformation("Upgrade complete");
			return Task.FromResult(0);
		}

		void UpgradeFilesInPlace(ILogger Logger, HashSet<string> TargetFiles, List<Tuple<string, string>> RenameFiles)
		{
			// Remove all the target files
			foreach (string TargetFile in TargetFiles)
			{
				Logger.LogDebug("Deleting {File}", TargetFile);
				File.SetAttributes(TargetFile, FileAttributes.Normal);
				File.Delete(TargetFile);
			}

			// Rename all the new files into place
			foreach (Tuple<string, string> Pair in RenameFiles)
			{
				Logger.LogDebug("Renaming {SourceFile} to {TargetFile}", Pair.Item1, Pair.Item2);
				File.Move(Pair.Item1, Pair.Item2, true);
			}
		}

		void UpgradeMacService(ILogger Logger, Process OtherProcess, HashSet<string> TargetFiles, List<Tuple<string, string>> RenameFiles)
		{
			UpgradeFilesInPlace(Logger, TargetFiles, RenameFiles);
			Logger.LogDebug("Upgrade completed, restarting...");
			OtherProcess.Kill();
		}

		void UpgradeLinuxService(ILogger Logger, Process OtherProcess, HashSet<string> TargetFiles, List<Tuple<string, string>> RenameFiles)
		{
			UpgradeFilesInPlace(Logger, TargetFiles, RenameFiles);
			Logger.LogDebug("Upgrade completed, restarting...");
			OtherProcess.Kill();
		}

		void UpgradeWindowsService(ILogger Logger, Process OtherProcess, HashSet<string> TargetFiles, List<Tuple<string, string>> RenameFiles)
		{
			// Try to get the service associated with the passed-in process id
			using (ServiceController? Service = GetServiceForProcess(ProcessId))
			{
				// Stop the process
				if (Service == null)
				{
					Logger.LogInformation("Terminating other process");
					OtherProcess.Kill();
				}
				else
				{
					Logger.LogInformation("Stopping service");
					Service.Stop();
				}
				OtherProcess.WaitForExit();

				UpgradeFilesInPlace(Logger, TargetFiles, RenameFiles);

				// Run the new application
				if (Service == null)
				{
					string DriverFileName = "dotnet";
					string AssemblyFileName = Path.Combine(TargetDir.FullName, Path.GetFileName(Assembly.GetExecutingAssembly().Location));

					StringBuilder DriverArguments = new StringBuilder();
					DriverArguments.AppendArgument(AssemblyFileName);
					DriverArguments.Append(' ');
					DriverArguments.Append(Arguments);

					StringBuilder Launch = new StringBuilder();
					Launch.AppendArgument(DriverFileName);
					Launch.Append(' ');
					Launch.Append(DriverArguments);
					Logger.LogInformation("Launching: {Launch}", Launch.ToString());

					using Process NewProcess = Process.Start(DriverFileName, DriverArguments.ToString());
				}
				else
				{
					// Start the service again
					Logger.LogInformation("Restarting service");
					Service.Start();
				}
			}
		}

		/// <summary>
		/// Try to find the service controller for the given process id
		/// </summary>
		/// <param name="ProcessId">The process id to search for</param>
		/// <returns>The service controller corresponding to this process</returns>
		static ServiceController? GetServiceForProcess(int ProcessId)
		{
			try
			{
				using (ManagementObjectSearcher Searcher = new ManagementObjectSearcher($"SELECT Name FROM Win32_Service WHERE ProcessId={ProcessId}"))
				{
					foreach (ManagementObject Service in Searcher.Get())
					{
						PropertyData Property = Service.Properties["Name"];
						if (Property != null)
						{
							string? Name = Property.Value as string;
							if (Name != null)
							{
								return new ServiceController(Name);
							}
						}
					}
				}
			}
			catch
			{
			}
			return null;
		}
	}
}
