// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Linq;
using System.Security.Cryptography;
using AutomationTool;
using UnrealBuildTool;
using System.Text;
using System.Text.RegularExpressions;

/*

General Device Notes (and areas for improvement):

1) We don't currently support parallel iOS tests, see https://jira.it.epicgames.net/browse/UEATM-219
2) Device Farm devices should be in airplane mode + wifi to avoid No Sim warning notification

*/

namespace Gauntlet
{

	class IOSAppInstance : IAppInstance
	{
		protected IOSAppInstall Install;
		public IOSAppInstance(IOSAppInstall InInstall, IProcessResult InProcess, string InCommandLine)			
		{
			Install = InInstall;
			this.CommandLine = InCommandLine;
			this.ProcessResult = InProcess;		
		}

		public string ArtifactPath
		{
			get
			{
				if (bHaveSavedArtifacts == false)
				{
					if (HasExited)
					{
						SaveArtifacts();
						bHaveSavedArtifacts = true;
					}
				}
				
				return Install.IOSDevice.LocalCachePath + "/" + Install.IOSDevice.DeviceArtifactPath;
			}
		}

		public ITargetDevice Device
		{
			get
			{
				return Install.Device;
			}
		}

		protected void SaveArtifacts()
		{
			TargetDeviceIOS Device = Install.IOSDevice;

			// copy remote artifacts to local		
			string CommandLine = String.Format("--bundle_id {0} --download={1} --to {2}", Install.PackageName, Device.DeviceArtifactPath, Device.LocalCachePath);

			IProcessResult DownloadCmd = Device.ExecuteIOSDeployCommand(CommandLine, 120);

			if (DownloadCmd.ExitCode != 0)
			{
				Log.Warning("Failed to retrieve artifacts. {0}", DownloadCmd.Output);
			}
			
		}

		public IProcessResult ProcessResult { get; private set; }

		public bool HasExited { get { return ProcessResult.HasExited; } }

		public bool WasKilled { get; protected set; }		

		public int ExitCode { get { return ProcessResult.ExitCode; } }

		public string CommandLine { get; private set; }

		public string StdOut 
		{  
			get
			{
				if (HasExited)
				{
					// The ios application is being run under lldb by ios-deploy
					// lldb catches crashes and we have it setup to dump thread callstacks
					// parse any crash dumps into Unreal crash format and append to output
					string CrashLog = LLDBCrashParser.GenerateCrashLog(ProcessResult.Output);

					if (!string.IsNullOrEmpty(CrashLog))
					{
						return String.Format("{0}\n{1}", ProcessResult.Output, CrashLog);
					}
				}

				return ProcessResult.Output;

			}
		}

		public int WaitForExit()
		{
			if (!HasExited)
			{
				ProcessResult.WaitForExit();
			}

			return ExitCode;
		}

		public void Kill()
		{
			if (!HasExited)
			{
				WasKilled = true;
				ProcessResult.ProcessObject.Kill();
			}
		}


		internal bool bHaveSavedArtifacts;		
	}


	class IOSAppInstall : IAppInstall
	{
		public string Name { get; protected set; }
			
		public string CommandLine { get; protected set; }

        public string PackageName { get; protected set; }

		public ITargetDevice Device { get { return IOSDevice; } }

		public TargetDeviceIOS IOSDevice;

		public IOSAppInstall(string InName, TargetDeviceIOS InDevice, string InPackageName, string InCommandLine)
		{
			Name = InName;			
			CommandLine = InCommandLine;
            PackageName = InPackageName;

			IOSDevice = InDevice;
		}

		public IAppInstance Run()
		{
			return Device.Run(this);
		}
	}

	public class IOSDeviceFactory : IDeviceFactory
	{
		public bool CanSupportPlatform(UnrealTargetPlatform Platform)
		{
			return Platform == UnrealTargetPlatform.IOS;
		}

		public ITargetDevice CreateDevice(string InRef, string InParam)
		{
			return new TargetDeviceIOS(InRef);
		}
	}

	/// <summary>
	/// iOS implementation of a device to run applications
	/// </summary>
	public class TargetDeviceIOS : ITargetDevice
	{
		public string Name { get; protected set; }

		/// <summary>
		/// Low-level device name (uuid)
		/// </summary>
		public string DeviceName { get; protected set; }
		
		protected Dictionary<EIntendedBaseCopyDirectory, string> LocalDirectoryMappings { get; set; }

		public TargetDeviceIOS(string InName)
		{
			IsDefaultDevice = (String.IsNullOrEmpty(InName) || InName.Equals("default", StringComparison.OrdinalIgnoreCase));

			Name = InName;
			LocalDirectoryMappings = new Dictionary<EIntendedBaseCopyDirectory, string>();

			var DefaultDevices = GetConnectedDeviceUUID();			

			// If no device name or its 'default' then use the first default device
			if (IsDefaultDevice)
			{				
				if (DefaultDevices.Count() == 0)
				{
					throw new AutomationException("No default device available");
				}

				DeviceName = DefaultDevices.First();

				Log.Verbose("Selected device {0} as default", DeviceName);
			}
			else
			{
				DeviceName = InName.Trim();
				if (!DefaultDevices.Contains(DeviceName))
				{
					throw new AutomationException("Device with UUID {0} not found in device list", DeviceName);
				}
			}

			// setup local cache
			LocalCachePath = Path.Combine(GauntletAppCache, "Device_" + Name);
			if (Directory.Exists(LocalCachePath))
			{
				Directory.Delete(LocalCachePath, true);
			}

			Directory.CreateDirectory(LocalCachePath);
		}

		bool IsDefaultDevice = false;		

		#region IDisposable Support
		private bool disposedValue = false; // To detect redundant calls

		protected virtual void Dispose(bool disposing)
		{
			if (!disposedValue)
			{
				try
				{
					if (Directory.Exists(LocalCachePath))
					{
						Directory.Delete(LocalCachePath, true);
					}
				}
				catch (Exception Ex)
				{
					Log.Warning("TargetDeviceIOS.Dispose() threw: {0}", Ex.Message);
				}
				finally
				{
					disposedValue = true;
				}

			}
		}

		// This code added to correctly implement the disposable pattern.
		public void Dispose()
		{
			// Do not change this code. Put cleanup code in Dispose(bool disposing) above.
			Dispose(true);
			GC.SuppressFinalize(this);
		}
		#endregion

		public CommandUtils.ERunOptions RunOptions { get; set; }

		public IAppInstance Run(IAppInstall App)
		{
			IOSAppInstall IOSApp = App as IOSAppInstall;

			if (IOSApp == null)
			{
				throw new DeviceException("AppInstance is of incorrect type!");
			}

			string CommandLine = IOSApp.CommandLine.Replace("\"", "\\\"");
			
			Log.Info("Launching {0} on {1}", App.Name, ToString());
			Log.Verbose("\t{0}", CommandLine);

			// ios-deploy notes: -L launches detached, -I non-interactive  (exits when app exits), -r uninstalls before install (removes app Documents folder)
			// -t <seconds> number of seconds to wait for device to be connected

			// setup symbols if available
			string DSymBundle = "";
			string DSymDir = Path.Combine(GauntletAppCache, "Symbols");

			if (Directory.Exists(DSymDir))
			{
				DSymBundle = Directory.GetDirectories(DSymDir).Where(D => Path.GetExtension(D).ToLower() == ".dsym").FirstOrDefault();
				DSymBundle = string.IsNullOrEmpty(DSymBundle) ? "" : DSymBundle = " -s \"" + DSymBundle + "\"";
			}

			string CL = "--noinstall -I" + DSymBundle + " -b \"" + LocalAppBundle + "\" --args '" + CommandLine.Trim() + "'";

			IProcessResult Result = ExecuteIOSDeployCommand(CL, 0);

			Thread.Sleep(5000);

			// Give ios-deploy a chance to throw out any errors...
			if (Result.HasExited)
			{
				Log.Warning("ios-deploy exited early: " + Result.Output);
				throw new DeviceException("Failed to launch on {0}. {1}", Name, Result.Output);
			}

			return new IOSAppInstance(IOSApp, Result, IOSApp.CommandLine);
		}

		/// <summary>
		/// Remove the application entirely from the iOS device, this includes any persistent app data in /Documents
		/// </summary>
		private void RemoveApplication(IOSBuild Build)
		{
			string CommandLine = String.Format("--bundle_id {0} --uninstall_only", Build.PackageName);
			ExecuteIOSDeployCommand(CommandLine);
		}

		/// <summary>
		/// Remove artifacts from device
		/// </summary>
		private bool CleanDeviceArtifacts(IOSBuild Build)
		{			
			try
			{
				Log.Verbose("Cleaning device artifacts");

				string CleanCommand = String.Format("--bundle_id {0} --rm_r {1}", Build.PackageName, DeviceArtifactPath);
				IProcessResult Result = ExecuteIOSDeployCommand(CleanCommand, 120);

				if (Result.ExitCode != 0)
				{
					Log.Warning("Failed to clean artifacts from device");
					return false;
				}

			}
			catch (Exception Ex)
			{
				Log.Verbose("Exception while cleaning artifacts from device: {0}", Ex.Message);
			}

			return true;

		}

		/// <summary>
		/// Checks whether version of deployed bundle matches local IPA
		/// </summary>
		bool CheckDeployedIPA(IOSBuild Build)
		{
			try
			{
				Log.Verbose("Checking deployed IPA hash");

				string CommandLine = String.Format("--bundle_id {0} --download={1} --to {2}", Build.PackageName, "/Documents/IPAHash.txt", LocalCachePath);
				IProcessResult Result = ExecuteIOSDeployCommand(CommandLine, 120);

				if (Result.ExitCode != 0)
				{
					return false;
				}

				string Hash = File.ReadAllText(LocalCachePath + "/Documents/IPAHash.txt").Trim();
				string StoredHash = File.ReadAllText(IPAHashFilename).Trim();

				if (Hash == StoredHash)
				{
					Log.Verbose("Deployed app hash matched cached IPA hash");
					return true;
				}

			}
			catch (Exception Ex)
			{
				if (!Ex.Message.Contains("is denied"))
				{
					Log.Verbose("Unable to pull cached IPA cache from device, cached file may not exist: {0}", Ex.Message);
				}
			}

			Log.Verbose("Deployed app hash doesn't match, IPA will be installed");
			return false;

		}

		public IAppInstall InstallApplication(UnrealAppConfig AppConfig)
		{            
            IOSBuild Build = AppConfig.Build as IOSBuild;

			// Ensure Build exists
			if (Build == null)
			{
				throw new AutomationException("Invalid build for IOS!");
			}

			Log.Info("Installing using IPA {0}", Build.SourceIPAPath);

			// device artifact path
			DeviceArtifactPath = string.Format("/Documents/{0}/Saved", AppConfig.ProjectName);

			PrepareIPA(Build);

			if (!CheckDeployedIPA(Build))
			{
				// uninstall will clean all device artifacts
				ExecuteIOSDeployCommand(String.Format("--uninstall -b \"{0}\"", LocalAppBundle), 10 * 60);
			}
			else
			{
				// remove device artifacts
				CleanDeviceArtifacts(Build);
			}
			
			// parallel iOS tests use same app install folder, so lock it as setup is quick
			lock (Globals.MainLock)
			{				
				// local app install with additional files, this directory will be mirrored to device in a single operation
				string AppInstallPath;

				AppInstallPath = Path.Combine(Globals.TempDir, "iOSAppInstall");

				if (Directory.Exists(AppInstallPath))
				{
					Directory.Delete(AppInstallPath, true);
				}

				Directory.CreateDirectory(AppInstallPath);

				if (LocalDirectoryMappings.Count == 0)
				{
					PopulateDirectoryMappings(AppInstallPath);
				}

				//@todo: Combine Build and AppConfig files, this should be done in higher level code, not per device implementation

				if (AppConfig.FilesToCopy != null)
				{
					foreach (UnrealFileToCopy FileToCopy in AppConfig.FilesToCopy)
					{
						string PathToCopyTo = Path.Combine(LocalDirectoryMappings[FileToCopy.TargetBaseDirectory], FileToCopy.TargetRelativeLocation);

						if (File.Exists(FileToCopy.SourceFileLocation))
						{
							FileInfo SrcInfo = new FileInfo(FileToCopy.SourceFileLocation);
							SrcInfo.IsReadOnly = false;
							string DirectoryToCopyTo = Path.GetDirectoryName(PathToCopyTo);
							if (!Directory.Exists(DirectoryToCopyTo))
							{
								Directory.CreateDirectory(DirectoryToCopyTo);
							}
							if (File.Exists(PathToCopyTo))
							{
								FileInfo ExistingFile = new FileInfo(PathToCopyTo);
								ExistingFile.IsReadOnly = false;
							}

							SrcInfo.CopyTo(PathToCopyTo, true);
							Log.Verbose("Copying app install: {0} to {1}", FileToCopy, DirectoryToCopyTo);
						}
						else
						{
							Log.Warning("File to copy {0} not found", FileToCopy);
						}
					}
				}

				// copy mapped files in a single pass
				string CopyCommand = String.Format("--bundle_id {0} --upload={1} --to {2}", Build.PackageName, AppInstallPath, DeviceArtifactPath);
				ExecuteIOSDeployCommand(CopyCommand, 120);

				// store the IPA hash to avoid redundant deployments
				CopyCommand = String.Format("--bundle_id {0} --upload={1} --to {2}", Build.PackageName, IPAHashFilename, "/Documents/IPAHash.txt");
				ExecuteIOSDeployCommand(CopyCommand, 120);				
			}

			IOSAppInstall IOSApp = new IOSAppInstall(AppConfig.Name, this, Build.PackageName, AppConfig.CommandLine);	
			return IOSApp;
		}

		public void PopulateDirectoryMappings(string ProjectDir)
        {
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Binaries, Path.Combine(ProjectDir, "Binaries"));
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Config, Path.Combine(ProjectDir, "Config"));
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Content, Path.Combine(ProjectDir, "Content"));
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Demos, Path.Combine(ProjectDir, "Demos"));
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Profiling, Path.Combine(ProjectDir, "Profiling"));
            LocalDirectoryMappings.Add(EIntendedBaseCopyDirectory.Saved, ProjectDir);
        }


		public UnrealTargetPlatform Platform { get { return UnrealTargetPlatform.IOS; } }

		/// <summary>
		/// Temp path we use to push/pull things from the device
		/// </summary>
		public string LocalCachePath { get; protected set; }


		/// <summary>
		/// Artifact (e.g. Saved) path on the device		
		/// </summary>
		public string DeviceArtifactPath { get; protected set;  }

		
		#region Device State Management

		// NOTE: We check that a default device UUID or the one specifed is connected with 'ios-deploy --detect' at device creation time
		// otherwise, ios-deploy doesn't currently support this style of queries
		// It might be possible to add additional lldb queries/commands through the python interface (there are various solutions for reboot, though the ones I have found require jailbreaking)
		public bool IsAvailable { get { return true; } }
		public bool IsConnected { get { return Connected; } }
		public bool IsOn { get { return true; } }
		public bool PowerOn() { return true; }
		public bool PowerOff() { return true; }
		public bool Reboot() { return true; }

		// catch attempts to connect multiple iOS devices in parallel, see https://jira.it.epicgames.net/browse/UEATM-219
		static int ConnectedDeviceCount = 0;
		bool Connected = false;

		public bool Connect() 
		{ 
			lock (Globals.MainLock)
			{
				if (Connected)
				{
					return true;								
				}

				ConnectedDeviceCount++;

				if (ConnectedDeviceCount > 1)
				{

					throw new AutomationException("Parallel iOS device connections are not currently supported, see https://jira.it.epicgames.net/browse/UEATM-219");
				}

				Connected = true;

				// Get rid of any zombies, this needs to be reworked to use tracked process id's when running parallel tests
				// may need to kill by processid for spawned children
				IOSBuild.ExecuteCommand("killall", "ios-deploy");
				Thread.Sleep(2500);
				IOSBuild.ExecuteCommand("killall", "lldb");			
				Thread.Sleep(2500);
			}

			return true; 
		}
		public bool Disconnect()
		{ 
			lock (Globals.MainLock)
			{
				if (!Connected)
				{
					return true;								
				}

				Connected = false;

				ConnectedDeviceCount--;

				if (ConnectedDeviceCount < 0)
				{
					throw new AutomationException("iOS device connection mismatch");
				}
			}

			return true; 
		}

		#endregion

		public override string ToString()
		{
			return Name;
		}


		/// <summary>
		/// Get UUID of all connected iOS devices
		/// </summary>
		List<string> GetConnectedDeviceUUID()
		{
			var Result = ExecuteIOSDeployCommand("--detect");

			if (Result.ExitCode != 0)
			{
				return new List<string>();
			}

			MatchCollection DeviceMatches = Regex.Matches(Result.Output, @"(.?)Found\ ([a-z0-9]{40})");

			return DeviceMatches.Cast<Match>().Select<Match, string>(
				M => M.Groups[2].ToString()
			).ToList();
		}
		
		// Gauntlet cache folder for tracking device/ipa state
		string GauntletAppCache { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Personal), ".gauntletappcache");} }

		// path to locally extracted (and possibly resigned) app bundle
		// Note: ios-deploy works with app bundles, which requires the IPA be unzipped for deployment (this will allow us to resign in the future as well)
		string LocalAppBundle = null;

		// the current IPA MD5 hash, which is tracked to avoid unneccessary deployments and unzip operations
		string IPAHashFilename { get { return Path.Combine(GauntletAppCache, "IPAHash.txt"); } }

		/// <summary>
		/// Generate MD5 and cache IPA bundle files
		/// </summary>
		private bool PrepareIPA(IOSBuild Build)
		{	
			Log.Info("Preparing IPA {0}", Build.SourceIPAPath);

			try
			{	
				// cache the unzipped app using a MD5 checksum, avoiding needing to unzip		
				string Hash = null;
				string StoredHash = null;
				using (var MD5Hash = MD5.Create())
				{
					using (var Stream = File.OpenRead(Build.SourceIPAPath))
					{
						Hash = BitConverter.ToString(MD5Hash.ComputeHash(Stream)).Replace("-", "").ToLowerInvariant();
					}
				}
				string PayloadDir = Path.Combine(GauntletAppCache, "Payload");
				string SymbolsDir = Path.Combine(GauntletAppCache, "Symbols");
				
				if (File.Exists(IPAHashFilename) && Directory.Exists(PayloadDir))
				{
					StoredHash = File.ReadAllText(IPAHashFilename).Trim();
					if (Hash != StoredHash)
					{
						Log.Verbose("IPA hash out of date, clearing cache");
						StoredHash = null;
					}
				}

				if (String.IsNullOrEmpty(StoredHash) || Hash != StoredHash)
				{
					if (Directory.Exists(PayloadDir))
					{
						Directory.Delete(PayloadDir, true);
					}

					if (Directory.Exists(SymbolsDir))
					{
						Directory.Delete(SymbolsDir, true);
					}

					Log.Verbose("Unzipping IPA {0} to cache at: {1}", Build.SourceIPAPath, GauntletAppCache);
					
					IProcessResult Result = IOSBuild.ExecuteCommand("unzip", String.Format("{0} -d {1}", Build.SourceIPAPath, GauntletAppCache));
					if (Result.ExitCode != 0 || !Directory.Exists(PayloadDir))
					{
						throw new Exception(String.Format("Unable to extract IPA {0}", Build.SourceIPAPath));
					}

					// Cache symbols for symbolicated callstacks
					string SymbolsZipFile = string.Format("{0}/../../Symbols/{1}.dSYM.zip", Path.GetDirectoryName(Build.SourceIPAPath), Path.GetFileNameWithoutExtension(Build.SourceIPAPath));

					Log.Verbose("Checking Symbols at {0}", SymbolsZipFile);

					if (File.Exists(SymbolsZipFile))
					{						
						Log.Verbose("Unzipping Symbols {0} to cache at: {1}", SymbolsZipFile, SymbolsDir);

						Result = IOSBuild.ExecuteCommand("unzip", String.Format("{0} -d {1}", SymbolsZipFile, SymbolsDir));
						if (Result.ExitCode != 0 || !Directory.Exists(SymbolsDir))
						{
							throw new Exception(String.Format("Unable to extract build symbols {0} -> {1}", SymbolsZipFile, SymbolsDir));
						}
					}					

					// store hash
					File.WriteAllText(IPAHashFilename, Hash);

					Log.Verbose("IPA cached");
				}
				else
				{
					Log.Verbose("Using cached IPA");
				}

				LocalAppBundle = Directory.GetDirectories(PayloadDir).Where(D => Path.GetExtension(D) == ".app").FirstOrDefault();

				if (String.IsNullOrEmpty(LocalAppBundle))
				{
					throw new Exception(String.Format("Unable to find app in local app bundle {0}", PayloadDir));
				}

			}
			catch (Exception Ex)
			{
				throw new AutomationException("Unable to prepare {0} : {1}", Build.SourceIPAPath, Ex.Message);
			}		

			return true;
		}

		public Dictionary<EIntendedBaseCopyDirectory, string> GetPlatformDirectoryMappings()
		{
			if (LocalDirectoryMappings.Count == 0)
			{
				Log.Warning("Platform directory mappings have not been populated for this platform! This should be done within InstallApplication()");
			}
			return LocalDirectoryMappings;
		}

		public IProcessResult ExecuteIOSDeployCommand(String CommandLine, int WaitTime = 60, bool WarnOnTimeout = true)
		{
			if (!IsDefaultDevice)
			{
				CommandLine = String.Format("--id {0} {1}", DeviceName, CommandLine);
			}

			String IOSDeployPath = Path.Combine(Globals.UE4RootDir, "Engine/Extras/ThirdPartyNotUE/ios-deploy/bin/ios-deploy");

			if (!File.Exists(IOSDeployPath))
			{
				throw new AutomationException("Unable to run ios-deploy binary at {0}", IOSDeployPath);
			}

			CommandUtils.ERunOptions RunOptions = CommandUtils.ERunOptions.NoWaitForExit;

			if (Log.IsVeryVerbose)
			{
				RunOptions |= CommandUtils.ERunOptions.AllowSpew;
			}
			else
			{
				RunOptions |= CommandUtils.ERunOptions.NoLoggingOfRunCommand;
			}

			Log.Verbose("ios-deploy executing '{0}'", CommandLine);

			IProcessResult Result = CommandUtils.Run(IOSDeployPath, CommandLine, Options: RunOptions);

			if (WaitTime > 0)
			{
				DateTime StartTime = DateTime.Now;

				Result.ProcessObject.WaitForExit(WaitTime * 1000);

				if (Result.HasExited == false)
				{
					if ((DateTime.Now - StartTime).TotalSeconds >= WaitTime)
					{
						string Message = String.Format("IOSDeployPath timeout after {0} secs: {1}, killing process", WaitTime, CommandLine);

						if (WarnOnTimeout)
						{ 
							Log.Warning(Message);
						}
						else
						{
							Log.Info(Message);
						}
						
						Result.ProcessObject.Kill();
						// wait up to 15 seconds for process exit
						Result.ProcessObject.WaitForExit(15000);
					}
				}
			}

			return Result;
		}

	}


	/// <summary>
	/// Helper class to parses LLDB crash threads and generate Unreal compatible log callstack
	/// </summary>
	static class LLDBCrashParser
	{
		// Frame in callstack
		class FrameInfo
		{
			public string Module;
			public string Symbol;
			public string Address;
			public string Offset;
			public string Source;
			public string Line;

			public override string ToString()
			{
				// symbolicated
				if (!String.IsNullOrEmpty(Source))
				{
					return string.Format("Error: [Callstack] 0x{0} {1}!{2} [{3}{4}]", Address, Module, Symbol.Replace(" ", "^"), Source, String.IsNullOrEmpty(Line) ? "" : ":" + Line);
				}

				// unsymbolicated
				return string.Format("Error: [Callstack] 0x{0} {1}!{2} [???]", Address, Module, Symbol.Replace(" ", "^"));
			}

		}

		// Parsed thread callstack
		class ThreadInfo
		{
			public int Num;
			public string Status;
			public bool Current;
			public List<FrameInfo> Frames = new List<FrameInfo>();

			public override string ToString()
			{
				return string.Format("{0}{1}{2}\n{3}", Num, string.IsNullOrEmpty(Status) ? "" : " " + Status + " ", Current ? " (Current)" : "", string.Join("\n", Frames));
			}
		}

		/// <summary>
		/// Parse lldb thread crash dump to Unreal log format 
		/// </summary>		
		public static string GenerateCrashLog(string LogOutput)
		{
			DateTime TimeStamp;
			int Frame;
			ThreadInfo Thread = ParseCallstack(LogOutput, out TimeStamp, out Frame);
			if (Thread == null)
			{
				return null;
			}

			StringBuilder CrashLog = new StringBuilder();
			CrashLog.Append(string.Format("[{0}:000][{1}]LogCore: === Fatal Error: ===\n", TimeStamp.ToString("yyyy.mm.dd - H.mm.ss"), Frame));
			CrashLog.Append(string.Format("Error: Thread #{0} {1}\n", Thread.Num, Thread.Status));
			CrashLog.Append(string.Join("\n", Thread.Frames));

			return CrashLog.ToString();

		}

		static ThreadInfo ParseCallstack(string LogOutput, out DateTime Timestamp, out int FrameNum)
		{
			Timestamp = DateTime.UtcNow;
			FrameNum = 0;

			Regex LogLineRegex = new Regex(@"(?<timestamp>\s\[\d.+\]\[\s*\d+\])(?<log>.*)");
			Regex TimeRegex = new Regex(@"\[(?<year>\d+)\.(?<month>\d+)\.(?<day>\d+)-(?<hour>\d+)\.(?<minute>\d+)\.(?<second>\d+):(?<millisecond>\d+)\]\[(?<frame>\s*\d+)\]", RegexOptions.IgnoreCase);
			Regex ThreadRegex = new Regex(@"(thread\s#)(?<threadnum>\d+),?(?<status>.+)");
			Regex SymbolicatedFrameRegex = new Regex(@"\s#(?<framenum>\d+):\s0x(?<address>[\da-f]+)\s(?<module>.+)\`(?<symbol>.+)(\sat\s)(?<source>.+)\s\[opt\]");
			Regex UnsymbolicatedFrameRegex = new Regex(@"frame\s#(?<framenum>\d+):\s0x(?<address>[\da-f]+)\s(?<module>.+)\`(?<symbol>.+)\s\+\s(?<offset>\d+)");

			LinkedList<string> CrashLog = new LinkedList<string>(Regex.Split(LogOutput, "\r\n|\r|\n"));
						
			List<ThreadInfo> Threads = new List<ThreadInfo>();
			ThreadInfo Thread = null;
			ThreadInfo CrashThread = null;

			var LineNode = CrashLog.First;			
			while (LineNode != null)
			{
				string Line = LineNode.Value.Trim();

				// If Gauntlet marks the test as complete, ignore any thread dumps from forcing process to exit
				if (Line.Contains("**** TEST COMPLETE. EXIT CODE: 0 ****"))
				{
					return null;
				}

				// Parse log timestamps
				if (LogLineRegex.IsMatch(Line))
				{
					GroupCollection LogGroups = LogLineRegex.Match(Line).Groups;
					if (TimeRegex.IsMatch(LogGroups["timestamp"].Value))
					{
						GroupCollection TimeGroups = TimeRegex.Match(LogGroups["timestamp"].Value).Groups;
						int Year = int.Parse(TimeGroups["year"].Value);
						int Month = int.Parse(TimeGroups["month"].Value);
						int Day = int.Parse(TimeGroups["day"].Value);
						int Hour = int.Parse(TimeGroups["hour"].Value);
						int Minute = int.Parse(TimeGroups["minute"].Value);
						int Second = int.Parse(TimeGroups["second"].Value);
						FrameNum = int.Parse(TimeGroups["frame"].Value);
						Timestamp = new DateTime(Year, Month, Day, Hour, Minute, Second);
					}

					LineNode = LineNode.Next;
					continue;
				}

				if (Thread != null)
				{
					FrameInfo Frame = null;
					GroupCollection FrameGroups = null;

					// Parse symbolicated frame
					if (SymbolicatedFrameRegex.IsMatch(Line))
					{
						FrameGroups = SymbolicatedFrameRegex.Match(Line).Groups;

						Frame = new FrameInfo()
						{
							Address = FrameGroups["address"].Value,
							Module = FrameGroups["module"].Value,
							Symbol = FrameGroups["symbol"].Value,
						};

						Frame.Source = FrameGroups["source"].Value;
						if (Frame.Source.Contains(":"))
						{
							Frame.Source = FrameGroups["source"].Value.Split(':')[0];
							Frame.Line = FrameGroups["source"].Value.Split(':')[1];
						}
					}

					// Parse unsymbolicated frame
					if (UnsymbolicatedFrameRegex.IsMatch(Line))
					{
						FrameGroups = UnsymbolicatedFrameRegex.Match(Line).Groups;

						Frame = new FrameInfo()
						{
							Address = FrameGroups["address"].Value,
							Offset = FrameGroups["offset"].Value,
							Module = FrameGroups["module"].Value,
							Symbol = FrameGroups["symbol"].Value
						};
					}

					if (Frame != null)
					{
						Thread.Frames.Add(Frame);
					}
					else
					{						
						Thread = null;
					}

				}

				// Parse thread
				if (ThreadRegex.IsMatch(Line))
				{
					GroupCollection ThreadGroups = ThreadRegex.Match(Line).Groups;
					Thread = new ThreadInfo()
					{
						Num = int.Parse(ThreadGroups["threadnum"].Value),
						Status = ThreadGroups["status"].Value.Trim()
					};

					if (Line.StartsWith("*"))
					{
						Thread.Current = true;
					}

					if (CrashThread == null)
					{
						CrashThread = Thread;
					}
					else
					{
						Threads.Add(Thread);
					}
				}

				LineNode = LineNode.Next;
			}

			if (CrashThread == null)
			{
				return null;
			}

			Thread = Threads.Single(T => T.Num == CrashThread.Num);

			if (Thread == null)
			{
				Log.Warning("Unable to parse full crash callstack");
				Thread = CrashThread;
			}

			return Thread;

		}

	}
}