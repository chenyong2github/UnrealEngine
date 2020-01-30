// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Data.SqlClient;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace UnrealGameSync
{
	static class Program
	{
		public static string SyncVersion = null;

		[STAThread]
		static void Main(string[] Args)
		{
			Application.EnableVisualStyles();
			Application.SetCompatibleTextRenderingDefault(false);

			bool bFirstInstance;
			using(Mutex InstanceMutex = new Mutex(true, "UnrealGameSyncRunning", out bFirstInstance))
			{
				using(EventWaitHandle ActivateEvent = new EventWaitHandle(false, EventResetMode.AutoReset, "ActivateUnrealGameSync"))
				{
					if(bFirstInstance)
					{
						InnerMain(InstanceMutex, ActivateEvent, Args);
					}
					else
					{
						ActivateEvent.Set();
					}
				}
			}
		}

		static void InnerMain(Mutex InstanceMutex, EventWaitHandle ActivateEvent, string[] Args)
		{
			string ServerAndPort = null;
			string UserName = null;
			string UpdatePath = null;
			Utility.ReadGlobalPerforceSettings(ref ServerAndPort, ref UserName, ref UpdatePath);

			List<string> RemainingArgs = new List<string>(Args);

			string UpdateSpawn;
			ParseArgument(RemainingArgs, "-updatespawn=", out UpdateSpawn);

			bool bRestoreState;
			ParseOption(RemainingArgs, "-restorestate", out bRestoreState);

			bool bUnstable;
			ParseOption(RemainingArgs, "-unstable", out bUnstable);

            string ProjectFileName;
            ParseArgument(RemainingArgs, "-project=", out ProjectFileName);

			string UpdateConfigFile = Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location), "AutoUpdate.ini");
			MergeUpdateSettings(UpdateConfigFile, ref UpdatePath, ref UpdateSpawn);

			string SyncVersionFile = Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location), "SyncVersion.txt");
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

			string DataFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "UnrealGameSync");
			Directory.CreateDirectory(DataFolder);

			using(TelemetryWriter Telemetry = new TelemetryWriter(DeploymentSettings.ApiUrl, Path.Combine(DataFolder, "Telemetry.log")))
			{
				AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;

				// Create the log file
				using (TimestampLogWriter Log = new TimestampLogWriter(new BoundedLogWriter(Path.Combine(DataFolder, "UnrealGameSync.log"))))
				{
					Log.WriteLine("Application version: {0}", Assembly.GetExecutingAssembly().GetName().Version);
					Log.WriteLine("Started at {0}", DateTime.Now.ToString());

					if (ServerAndPort == null || UserName == null)
					{
						Log.WriteLine("Missing server settings; finding defaults.");
						GetDefaultServerSettings(ref ServerAndPort, ref UserName, Log);
						Utility.SaveGlobalPerforceSettings(ServerAndPort, UserName, UpdatePath);
					}

					PerforceConnection DefaultConnection = new PerforceConnection(UserName, null, ServerAndPort);
					using (UpdateMonitor UpdateMonitor = new UpdateMonitor(DefaultConnection, UpdatePath))
					{
						ProgramApplicationContext Context = new ProgramApplicationContext(DefaultConnection, UpdateMonitor, DeploymentSettings.ApiUrl, DataFolder, ActivateEvent, bRestoreState, UpdateSpawn, ProjectFileName, bUnstable, Log);
						Application.Run(Context);

						if(UpdateMonitor.IsUpdateAvailable && UpdateSpawn != null)
						{
							InstanceMutex.Close();
							bool bLaunchUnstable = UpdateMonitor.RelaunchUnstable ?? bUnstable;
							Utility.SpawnProcess(UpdateSpawn, "-restorestate" + (bLaunchUnstable? " -unstable" : ""));
						}
					}
				}
			}
		}

		public static void GetDefaultServerSettings(ref string ServerAndPort, ref string UserName, TextWriter Log)
		{
			// Read the P4PORT setting for the server, if necessary. Change to the project folder if set, so we can respect the contents of any P4CONFIG file.
			if(ServerAndPort == null)
			{
				PerforceConnection Perforce = new PerforceConnection(UserName, null, null);

				string NewServerAndPort;
				if (Perforce.GetSetting("P4PORT", out NewServerAndPort, Log))
				{
					ServerAndPort = NewServerAndPort;
				}
				else
				{
					ServerAndPort = PerforceConnection.DefaultServerAndPort;
				}
			}

			// Update the server and username from the reported server info if it's not set
			if(UserName == null)
			{
				PerforceConnection Perforce = new PerforceConnection(UserName, null, ServerAndPort);

				PerforceInfoRecord PerforceInfo;
				if(Perforce.Info(out PerforceInfo, Log) && !String.IsNullOrEmpty(PerforceInfo.UserName))
				{
					UserName = PerforceInfo.UserName;
				}
				else
				{
					UserName = Environment.UserName;
				}
			}
		}

		private static void CurrentDomain_UnhandledException(object Sender, UnhandledExceptionEventArgs Args)
		{
			Exception Ex = Args.ExceptionObject as Exception;
			if(Ex != null)
			{
				StringBuilder ExceptionTrace = new StringBuilder(Ex.ToString());
				for(Exception InnerEx = Ex.InnerException; InnerEx != null; InnerEx = InnerEx.InnerException)
				{
					ExceptionTrace.Append("\nInner Exception:\n");
					ExceptionTrace.Append(InnerEx.ToString());
				}
				TelemetryWriter.Enqueue(TelemetryErrorType.Crash, ExceptionTrace.ToString(), null, DateTime.Now);
			}
		}

		static void MergeUpdateSettings(string UpdateConfigFile, ref string UpdatePath, ref string UpdateSpawn)
		{
			try
			{
				ConfigFile UpdateConfig = new ConfigFile();
				if(File.Exists(UpdateConfigFile))
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

		static bool ParseArgument(List<string> RemainingArgs, string Prefix, out string Value)
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
	}
}
