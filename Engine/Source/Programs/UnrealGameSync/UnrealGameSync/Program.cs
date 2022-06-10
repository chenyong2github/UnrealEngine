// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Sentry;
using Sentry.Infrastructure;
using System;
using System.Collections.Generic;
using System.Data.SqlClient;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Forms;

namespace UnrealGameSync
{
	static class Program
	{
		public static string GetVersionString()
		{
			AssemblyInformationalVersionAttribute? Version = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>();
			return Version?.InformationalVersion ?? "Unknown";
		}

		public static string? SyncVersion = null;

		public static void CaptureException(Exception exception)
		{
			if (DeploymentSettings.SentryDsn != null)
			{
				SentrySdk.CaptureException(exception);
			}
		}

		[STAThread]
		static void Main(string[] Args)
		{
			if (DeploymentSettings.SentryDsn != null)
			{
				SentryOptions sentryOptions = new SentryOptions();
				sentryOptions.Dsn = DeploymentSettings.SentryDsn;
				sentryOptions.StackTraceMode = StackTraceMode.Enhanced;
				sentryOptions.AttachStacktrace = true;
				sentryOptions.TracesSampleRate = 1.0;
				sentryOptions.SendDefaultPii = true;
				sentryOptions.Debug = true;
				sentryOptions.AutoSessionTracking = true;
				sentryOptions.DetectStartupTime = StartupTimeDetectionMode.Best;
				sentryOptions.ReportAssembliesMode = ReportAssembliesMode.InformationalVersion;
				sentryOptions.DiagnosticLogger = new TraceDiagnosticLogger(SentryLevel.Debug);
				SentrySdk.Init(sentryOptions);

				Application.ThreadException += Application_ThreadException_Sentry;
				AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException_Sentry;
				TaskScheduler.UnobservedTaskException += Application_UnobservedException_Sentry;
			}

			bool bFirstInstance;
			using (Mutex InstanceMutex = new Mutex(true, "UnrealGameSyncRunning", out bFirstInstance))
			{
				if (bFirstInstance)
				{
					Application.EnableVisualStyles();
					Application.SetCompatibleTextRenderingDefault(false);
				}

				using (EventWaitHandle ActivateEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "ActivateUnrealGameSync"))
				{
					// handle any url passed in, possibly exiting
					if (UriHandler.ProcessCommandLine(Args, bFirstInstance, ActivateEvent))
					{
						return;
					}

					if (bFirstInstance)
					{
						InnerMainAsync(InstanceMutex, ActivateEvent, Args).GetAwaiter().GetResult();
					}
					else
					{
						ActivateEvent.Set();
					}
				}
			}
		}

		static async Task InnerMainAsync(Mutex InstanceMutex, EventWaitHandle ActivateEvent, string[] Args)
		{
			string? ServerAndPort = null;
			string? UserName = null;
			string? BaseUpdatePath = null;
			GlobalPerforceSettings.ReadGlobalPerforceSettings(ref ServerAndPort, ref UserName, ref BaseUpdatePath);

			List<string> RemainingArgs = new List<string>(Args);

			string? UpdateSpawn;
			ParseArgument(RemainingArgs, "-updatespawn=", out UpdateSpawn);

			string? UpdatePath;
			ParseArgument(RemainingArgs, "-updatepath=", out UpdatePath);

			bool bRestoreState;
			ParseOption(RemainingArgs, "-restorestate", out bRestoreState);

			bool bUnstable;
			ParseOption(RemainingArgs, "-unstable", out bUnstable);

            string? ProjectFileName;
            ParseArgument(RemainingArgs, "-project=", out ProjectFileName);

			string? Uri;
			ParseArgument(RemainingArgs, "-uri=", out Uri);

			FileReference UpdateConfigFile = FileReference.Combine(new FileReference(Assembly.GetExecutingAssembly().Location).Directory, "AutoUpdate.ini");
			MergeUpdateSettings(UpdateConfigFile, ref UpdatePath, ref UpdateSpawn);

			// Set the current working directory to the update directory to prevent child-process file handles from disrupting auto-updates
			if (UpdateSpawn != null)
			{
				if (File.Exists(UpdateSpawn))
				{
					Directory.SetCurrentDirectory(Path.GetDirectoryName(UpdateSpawn));
				}
			}

			string SyncVersionFile = Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location!)!, "SyncVersion.txt");
			if(File.Exists(SyncVersionFile))
			{
				try
				{
					SyncVersion = File.ReadAllText(SyncVersionFile).Trim();
				}
				catch(Exception)
				{
					SyncVersion = null;
				}
			}

			DirectoryReference DataFolder = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.LocalApplicationData)!, "UnrealGameSync");
			DirectoryReference.CreateDirectory(DataFolder);

			// Enable TLS 1.1 and 1.2. TLS 1.0 is now deprecated and not allowed by default in NET Core servers.
			ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls11 | SecurityProtocolType.Tls12;

			// Create a new logger
			using (ILoggerProvider LoggerProvider = Logging.CreateLoggerProvider(FileReference.Combine(DataFolder, "UnrealGameSync.log")))
			{
				ServiceCollection Services = new ServiceCollection();
				Services.AddLogging(Builder => Builder.AddProvider(LoggerProvider));
				Services.AddSingleton<IAsyncDisposer, AsyncDisposer>();

				await using (ServiceProvider ServiceProvider = Services.BuildServiceProvider())
				{
					ILoggerFactory LoggerFactory = ServiceProvider.GetRequiredService<ILoggerFactory>();

					ILogger Logger = LoggerFactory.CreateLogger("Startup");
					Logger.LogInformation("Application version: {Version}", Assembly.GetExecutingAssembly().GetName().Version);
					Logger.LogInformation("Started at {Time}", DateTime.Now.ToString());

					string SessionId = Guid.NewGuid().ToString();
					Logger.LogInformation("SessionId: {SessionId}", SessionId);

					if (ServerAndPort == null || UserName == null)
					{
						Logger.LogInformation("Missing server settings; finding defaults.");
						ServerAndPort ??= PerforceSettings.Default.ServerAndPort;
						UserName ??= PerforceSettings.Default.UserName;
						GlobalPerforceSettings.SaveGlobalPerforceSettings(ServerAndPort, UserName, BaseUpdatePath);
					}

					ILogger TelemetryLogger = LoggerProvider.CreateLogger("Telemetry");
					TelemetryLogger.LogInformation("Creating telemetry sink for session {SessionId}", SessionId);

					using (ITelemetrySink TelemetrySink = DeploymentSettings.CreateTelemetrySink(UserName, SessionId, TelemetryLogger))
					{
						ITelemetrySink? PrevTelemetrySink = Telemetry.ActiveSink;
						try
						{
							Telemetry.ActiveSink = TelemetrySink;

							Telemetry.SendEvent("Startup", new { User = Environment.UserName, Machine = Environment.MachineName });

							AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;

							IPerforceSettings DefaultSettings = new PerforceSettings(ServerAndPort, UserName) { PreferNativeClient = true };

							using (UpdateMonitor UpdateMonitor = new UpdateMonitor(DefaultSettings, UpdatePath, ServiceProvider))
							{
								using ProgramApplicationContext Context = new ProgramApplicationContext(DefaultSettings, UpdateMonitor, DeploymentSettings.ApiUrl, DataFolder, ActivateEvent, bRestoreState, UpdateSpawn, ProjectFileName, bUnstable, ServiceProvider, Uri);
								Application.Run(Context);

								if (UpdateMonitor.IsUpdateAvailable && UpdateSpawn != null)
								{
									InstanceMutex.Close();
									bool bLaunchUnstable = UpdateMonitor.RelaunchUnstable ?? bUnstable;
									Utility.SpawnProcess(UpdateSpawn, "-restorestate" + (bLaunchUnstable ? " -unstable" : ""));
								}
							}
						}
						catch (Exception Ex)
						{
							Telemetry.SendEvent("Crash", new { Exception = Ex.ToString() });
							throw;
						}
						finally
						{
							Telemetry.ActiveSink = PrevTelemetrySink;
						}
					}
				}
			}
		}

		private static void CurrentDomain_UnhandledException(object Sender, UnhandledExceptionEventArgs Args)
		{
			Exception? Ex = Args.ExceptionObject as Exception;
			if(Ex != null)
			{
				Telemetry.SendEvent("Crash", new {Exception = Ex.ToString()});
			}
		}

		private static void CurrentDomain_UnhandledException_Sentry(object Sender, UnhandledExceptionEventArgs Args)
		{
			Exception? Ex = Args.ExceptionObject as Exception;
			if (Ex != null)
			{
				SentrySdk.CaptureException(Ex);
			}
		}

		private static void Application_ThreadException_Sentry(object sender, ThreadExceptionEventArgs e)
		{
			SentrySdk.CaptureException(e.Exception);

			ThreadExceptionDialog dialog = new ThreadExceptionDialog(e.Exception);
			dialog.ShowDialog();
		}

		private static void Application_UnobservedException_Sentry(object? sender, UnobservedTaskExceptionEventArgs args)
		{
			Exception? innerException = args.Exception?.InnerException;
			if (innerException != null)
			{
				SentrySdk.CaptureException(innerException, s => s.SetTag("Unobserved", "1"));
			}
		}

		static void MergeUpdateSettings(FileReference UpdateConfigFile, ref string? UpdatePath, ref string? UpdateSpawn)
		{
			try
			{
				ConfigFile UpdateConfig = new ConfigFile();
				if(FileReference.Exists(UpdateConfigFile))
				{
					UpdateConfig.Load(UpdateConfigFile);
				}

				if(UpdatePath == null)
				{
					UpdatePath = UpdateConfig.GetValue("Update.Path", null);
				}
				else
				{
					UpdateConfig.SetValue("Update.Path", UpdatePath);
				}

				if(UpdateSpawn == null)
				{
					UpdateSpawn = UpdateConfig.GetValue("Update.Spawn", null);
				}
				else
				{
					UpdateConfig.SetValue("Update.Spawn", UpdateSpawn);
				}

				UpdateConfig.Save(UpdateConfigFile);
			}
			catch(Exception)
			{
			}
		}

		static bool ParseOption(List<string> RemainingArgs, string Option, out bool Value)
		{
			for(int Idx = 0; Idx < RemainingArgs.Count; Idx++)
			{
				if(RemainingArgs[Idx].Equals(Option, StringComparison.InvariantCultureIgnoreCase))
				{
					Value = true;
					RemainingArgs.RemoveAt(Idx);
					return true;
				}
			}

			Value = false;
			return false;
		}

		static bool ParseArgument(List<string> RemainingArgs, string Prefix, [NotNullWhen(true)] out string? Value)
		{
			for(int Idx = 0; Idx < RemainingArgs.Count; Idx++)
			{
				if(RemainingArgs[Idx].StartsWith(Prefix, StringComparison.InvariantCultureIgnoreCase))
				{
					Value = RemainingArgs[Idx].Substring(Prefix.Length);
					RemainingArgs.RemoveAt(Idx);
					return true;
				}
			}

			Value = null;
			return false;
		}

		public static IEnumerable<string> GetPerforcePaths()
		{
			string? PathList = Environment.GetEnvironmentVariable("PATH");
			if (!String.IsNullOrEmpty(PathList))
			{
				foreach (string PathEntry in PathList.Split(Path.PathSeparator))
				{
					string? PerforcePath = null;
					try
					{
						string TestPerforcePath = Path.Combine(PathEntry, "p4.exe");
						if (File.Exists(TestPerforcePath))
						{
							PerforcePath = TestPerforcePath;
						}
					}
					catch
					{
					}

					if (PerforcePath != null)
					{
						yield return PerforcePath;
					}
				}
			}
		}

		public static void SpawnP4VC(string Arguments)
		{
			string Executable = "p4vc.exe";

			foreach (string PerforcePath in GetPerforcePaths())
			{
				string? PerforceDir = Path.GetDirectoryName(PerforcePath);
				if (PerforceDir != null && File.Exists(Path.Combine(PerforceDir, "p4vc.bat")) && !File.Exists(Path.Combine(PerforceDir, "p4vc.exe")))
				{
					Executable = Path.Combine(PerforceDir, "p4v.exe");
					Arguments = "-p4vc " + Arguments;
					break;
				}
			}

			if (!Utility.SpawnHiddenProcess(Executable, Arguments))
			{
				MessageBox.Show("Unable to spawn p4vc. Check you have P4V installed.");
			}
		}
	}
}
