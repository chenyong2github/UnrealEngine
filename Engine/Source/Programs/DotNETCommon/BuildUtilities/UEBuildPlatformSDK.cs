// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace Tools.DotNETCommon
{
	/// <summary>
	/// SDK installation status
	/// </summary>
	public enum SDKStatus
	{
		/// <summary>
		/// Desired SDK is installed and set up.
		/// </summary>
		Valid,

		/// <summary>
		/// Could not find the desired SDK, SDK setup failed, etc.		
		/// </summary>
		Invalid,
	};

	/// <summary>
	/// SDK for a platform
	/// </summary>
	abstract public class UEBuildPlatformSDK
	{
		// Public SDK handling, not specific to AutoSDK

		public UEBuildPlatformSDK()
		{
		}

		#region Global SDK Registration

		/// <summary>
		/// Registers the SDK for a given platform (as a string, but equivalent to UnrealTargetPlatform)
		/// </summary>
		/// <param name="SDK">SDK object</param>
		/// <param name="PlatformName">Platform name for this SDK</param>
		public static void RegisterSDKForPlatform(UEBuildPlatformSDK SDK, string PlatformName, bool bIsSdkAllowedOnHost)
		{
			// verify that neither platform or sdk were added before
			if (SDKRegistry.Count(x => x.Key == PlatformName || x.Value == SDK) > 0)
			{
				throw new Exception(string.Format("Re-registering SDK for {0}. All Platforms must have a unique SDK object", PlatformName));
			}

			SDKRegistry.Add(PlatformName, SDK);

			SDK.Init(PlatformName, bIsSdkAllowedOnHost);
		}

		private void Init(string InPlatformName, bool bInIsSdkAllowedOnHost)
		{
			PlatformName = InPlatformName;
			bIsSdkAllowedOnHost = bInIsSdkAllowedOnHost;

			// if the parent set up autosdk, the env vars will be wrong, but we can still get the manual SDK version from before it was setup
			if (HasParentProcessSetupAutoSDK(out CachedManualSDKVersion))
			{
				// we pass along __None to indicate the parent didn't have a manual sdk installed
				if (CachedManualSDKVersion == "__None")
				{
					CachedManualSDKVersion = null;
				}
			}
			else
			{
				// if there was no parent, get the SDK version before we run AutoSDK to get the manual version
				CachedManualSDKVersion = GetInstalledSDKVersion();
			}
		}

		#endregion

		#region Main Public Interface/Utilties

		/// <summary>
		/// Retrieves a previously registered SDK for a given platform
		/// </summary>
		/// <param name="PlatformName">String name of the platform (equivalent to UnrealTargetPlatform)</param>
		/// <returns></returns>
		public static UEBuildPlatformSDK GetSDKForPlatform(string PlatformName)
		{
			UEBuildPlatformSDK SDK;
			SDKRegistry.TryGetValue(PlatformName, out SDK);

			return SDK;
		}

		/// <summary>
		/// Gets the set of all known SDKs
		/// </summary>
		public static UEBuildPlatformSDK[] AllSDKs
		{
			get	{ return SDKRegistry.Values.ToArray(); }
		}

		// String name of the platform (will match an UnrealTargetPlatform)
		public string PlatformName;

		// True if this Sdk is allowed to be used by this host - if not, we can skip a lot 
		public bool bIsSdkAllowedOnHost;

		public string GetInstalledVersion(out bool bIsAutoSDK)
		{
			bIsAutoSDK = HasSetupAutoSDK();
			return GetInstalledSDKVersion();
		}

		public string GetInstalledVersion()
		{
			return GetInstalledSDKVersion();
		}

		public void GetInstalledVersions(out string ManualSDKVersion, out string AutoSDKVersion)
		{
			// if we support AutoSDKs, then return both versions
			if (PlatformSupportsAutoSDKs())
			{
				AutoSDKVersion = (HasRequiredAutoSDKInstalled() == SDKStatus.Valid) ? GetInstalledSDKVersion() : null;
//				AutoSDKVersion = GetInstalledSDKVersion();
			}
			else
			{
				AutoSDKVersion = null;
				if (CachedManualSDKVersion != GetInstalledSDKVersion())
				{
					throw new Exception("Manual SDK version changed, this is not supported yet");
				}
			}

			ManualSDKVersion = CachedManualSDKVersion;
		}


		public virtual bool IsVersionValid(string Version, bool bForAutoSDK)
		{
			return IsVersionValidInternal(Version, bForAutoSDK);
		}
		public virtual bool IsSoftwareVersionValid(string Version)
		{
			return IsSoftwareVersionValidInternal(Version);
		}

		public void ReactivateAutoSDK()
		{
			// @todo turnkey: this needs to force re-doing it, as it is likely a no-op, need to investigate what to clear out
			ManageAndValidateSDK();
		}

		#endregion

		#region Platform Overrides

		/// <summary>
		/// Returns the installed SDK version, used to determine if up to date or not (
		/// </summary>
		/// <returns></returns>
		public abstract string GetInstalledSDKVersion();

		/// <summary>
		/// Return the SDK version that the platform wants to use (AutoSDK dir must match this, full SDKs can be in a valid range)
		/// </summary>
		/// <returns></returns>
		public abstract string GetMainVersion();

		/// <summary>
		/// Gets the valid string range of Sdk versions. TryConvertVersionToInt() will need to succeed to make this usable for range checks
		/// </summary>
		/// <param name="MinVersion">Smallest version allowed</param>
		/// <param name="MaxVersion">Largest version allowed (inclusive)</param>
		public abstract void GetValidVersionRange(out string MinVersion, out string MaxVersion);


		/// <summary>
		/// Gets the valid string range of software/flash versions. TryConvertVersionToInt() will need to succeed to make this usable for range checks
		/// </summary>
		/// <param name="MinVersion">Smallest version allowed, or null if no minmum (in other words, 0 - MaxVersion)</param>
		/// <param name="MaxVersion">Largest version allowed (inclusive), or null if no maximum (in other words, MinVersion - infinity)y</param>
		public abstract void GetValidSoftwareVersionRange(out string MinVersion, out string MaxVersion);

		/// <summary>
		/// For a platform that doesn't use properly named AutoSDK directories, the directory name may not be convertible to an integer,
		/// and IsVersionValid checks could fail when checking AutoSDK version for an exact match. GetMainVersion() would return the 
		/// proper, integer-convertible version number of the SDK inside of the directory returned by GetAutoSDKDirectoryForMasterVersion()
		/// </summary>
		/// <returns></returns>
		public virtual string GetAutoSDKDirectoryForMasterVersion()
		{
			return GetMainVersion();
		}


		/// <summary>
		/// Gets the valid (integer) range of Sdk versions. Must be an integer to easily check a range vs a particular version
		/// </summary>
		/// <param name="MinVersion">Smallest version allowed</param>
		/// <param name="MaxVersion">Largest version allowed (inclusive)</param>
		/// <returns>True if the versions are valid, false if the platform is unable to convert its versions into an integer</returns>
		public virtual void GetValidVersionRange(out UInt64 MinVersion, out UInt64 MaxVersion)
		{
			string MinVersionString, MaxVersionString;
			GetValidVersionRange(out MinVersionString, out MaxVersionString);

			// failures to convert here are bad
			if (!TryConvertVersionToInt(MinVersionString, out MinVersion) || !TryConvertVersionToInt(MaxVersionString, out MaxVersion))
			{
				throw new Exception(string.Format("Unable to convert Min and Max valid versions to integers in {0} (Versions are {1} - {2})", GetType().Name, MinVersionString, MaxVersionString));
			}
		}
		public virtual void GetValidSoftwareVersionRange(out UInt64 MinVersion, out UInt64 MaxVersion)
		{
			string MinVersionString, MaxVersionString;
			GetValidSoftwareVersionRange(out MinVersionString, out MaxVersionString);

			MinVersion = UInt64.MinValue;
			MaxVersion = UInt64.MaxValue - 1; // MaxValue is always bad

			// failures to convert here are bad
			if ((MinVersionString != null && !TryConvertVersionToInt(MinVersionString, out MinVersion)) || 
				(MaxVersionString != null && !TryConvertVersionToInt(MaxVersionString, out MaxVersion)))
			{
				throw new Exception(string.Format("Unable to convert Min and Max valid Software versions to integers in {0} (Versions are {1} - {2})", GetType().Name, MinVersionString, MaxVersionString));
			}
		}


		// Let platform override behavior to determine if a version is a valid (useful for non-numeric versions)
		protected virtual bool IsVersionValidInternal(string Version, bool bForAutoSDK)
		{
			// we could have null if no SDK is installed at all, etc, which is always a failure
			if (Version == null)
			{
				return false;
			}

			// convert it to an integer
			UInt64 IntVersion;
			if (!TryConvertVersionToInt(Version, out IntVersion))
			{
				return false;
			}

			UInt64 DesiredVersion;
			if (!TryConvertVersionToInt(GetMainVersion(), out DesiredVersion))
			{
				return false;
			}

			// short circuit range check if the Version is the desired version already
			if (IntVersion == DesiredVersion)
			{
				return true;
			}
			else
			{
				// AutoSDK must match the desired version exactly, since that is the only one we will use
				if (bForAutoSDK)
				{
					return false;
				}
			}

			// get numeric range
			UInt64 MinVersion, MaxVersion;
			GetValidVersionRange(out MinVersion, out MaxVersion);
			return IntVersion >= MinVersion && IntVersion <= MaxVersion;
		}

		protected virtual bool IsSoftwareVersionValidInternal(string Version)
		{
			// we could have null if no SDK is installed at all, etc, which is always a failure
			if (Version == null)
			{
				return false;
			}

			// convert it to an integer
			UInt64 IntVersion;
			if (!TryConvertVersionToInt(Version, out IntVersion))
			{
				return false;
			}

			// get numeric range
			UInt64 MinVersion, MaxVersion;
			GetValidSoftwareVersionRange(out MinVersion, out MaxVersion);
			return IntVersion >= MinVersion && IntVersion <= MaxVersion;
		}


		/// <summary>
		/// Only the platform can convert a version string into an integer that is usable for comparison
		/// </summary>
		/// <param name="StringValue">Version that comes from the installed SDK or a Turnkey manifest or the like</param>
		/// <param name="OutValue">The integer version of StringValue, can be used to compare against a valid range</param>
		/// <returns>If the StringValue was able to be be converted to an integer</returns>
		public virtual bool TryConvertVersionToInt(string StringValue, out UInt64 OutValue)
		{
			// @todo turnkey make this abstract?
			OutValue = 0;
			return false;
		}

		/// <summary>
		/// Allow the platform SDK to override the name it will use in AutoSDK, but default to the platform name
		/// </summary>
		/// <returns>The name of the directory to use inside the AutoSDK system</returns>
		public virtual string GetAutoSDKPlatformName()
		{
			return PlatformName;
		}

		#endregion

		#region Print SDK Info
		private static bool bHasShownTurnkey = false;
		public virtual SDKStatus PrintSDKInfoAndReturnValidity(LogEventType Verbosity = LogEventType.Console, LogFormatOptions Options = LogFormatOptions.None,
			LogEventType ErrorVerbosity = LogEventType.Error, LogFormatOptions ErrorOptions = LogFormatOptions.None)
		{
			string ManualSDKVersion, AutoSDKVersion;
			GetInstalledVersions(out ManualSDKVersion, out AutoSDKVersion);

			SDKStatus Validity = SDKStatus.Valid;

			if (HasSetupAutoSDK())
			{
				string PlatformSDKRoot = GetPathToPlatformAutoSDKs();

				UInt64 Ver;
				TryConvertVersionToInt(AutoSDKVersion, out Ver);
				Log.WriteLine(Verbosity, Options, "{0} using Auto SDK from: {1} 0x{2:X}", PlatformName, Path.Combine(PlatformSDKRoot, AutoSDKVersion), Ver);
			}
			else if (HasRequiredManualSDK() == SDKStatus.Valid)
			{
				Log.WriteLine(Verbosity, Options, "{0} using Manual SDK {1}", PlatformName, ManualSDKVersion);
			}
			else
			{
				Validity = SDKStatus.Invalid;

				string MinVersionString, MaxVersionString;
				GetValidVersionRange(out MinVersionString, out MaxVersionString);

				StringBuilder Msg = new StringBuilder();
				Msg.AppendFormat("Unable to find a valid SDK for {0}.", PlatformName);
				if (ManualSDKVersion != null)
				{
					Msg.AppendFormat(" Found Version: {0}.", ManualSDKVersion);
				}

				if (MinVersionString != MaxVersionString)
				{
					Msg.AppendLine(" Must be between {0} and {1}", MinVersionString, MaxVersionString);
				}
				else
				{
					Msg.AppendLine(" Must be {0}", MinVersionString);
				}

				if (!bHasShownTurnkey)
				{
					Msg.AppendLine("  If your Studio has it set up, you can run this command to find the SDK to install:");
					Msg.AppendLine("    RunUAT Turnkey -command=InstallSdk -platform={0} -BestAvailable", PlatformName);

					if ((ErrorOptions & LogFormatOptions.NoConsoleOutput) == LogFormatOptions.None)
					{
						bHasShownTurnkey = true;
					}
				}

				// always print errors to the screen
				Log.WriteLine(ErrorVerbosity, ErrorOptions, Msg.ToString());
			}

			return Validity;
		}

		#endregion





		#region Private/Protected general functionality

		// this is the SDK version that was set before activating AutoSDK, since AutoSDK may remove ability to retrieve the Manual SDK version
		protected string CachedManualSDKVersion;
		private static Dictionary<string, UEBuildPlatformSDK> SDKRegistry = new Dictionary<string, UEBuildPlatformSDK>();

		#endregion

		// AutoSDKs handling portion

		#region protected AutoSDKs Utility

		/// <summary>
		/// Name of the file that holds currently install SDK version string
		/// </summary>
		protected const string CurrentlyInstalledSDKStringManifest = "CurrentlyInstalled.txt";

		/// <summary>
		/// name of the file that holds the last succesfully run SDK setup script version
		/// </summary>
		protected const string LastRunScriptVersionManifest = "CurrentlyInstalled.Version.txt";

		/// <summary>
		/// Name of the file that holds environment variables of current SDK
		/// </summary>
		protected const string SDKEnvironmentVarsFile = "OutputEnvVars.txt";

		protected const string SDKRootEnvVar = "UE_SDKS_ROOT";

		protected const string AutoSetupEnvVar = "AutoSDKSetup";


		protected static bool IsWindows()
		{
			return Environment.OSVersion.Platform == PlatformID.Win32NT || Environment.OSVersion.Platform == PlatformID.Win32S || Environment.OSVersion.Platform == PlatformID.Win32Windows;
		}

		protected static bool IsMac()
		{
			// mono tends to return Unix on Mac, so check for a Mac-only file
			return Environment.OSVersion.Platform == PlatformID.MacOSX || (Environment.OSVersion.Platform == PlatformID.Unix && File.Exists ("/System/Library/CoreServices/SystemVersion.plist"));
		}

		private static string GetAutoSDKHostPlatform()
		{
			if (IsWindows())
			{
				return "Win64";
			}
			else if (IsMac())
			{
				return "Mac";
			}
			else if (Environment.OSVersion.Platform == PlatformID.Unix)
			{
				return "Linux";
			}
			throw new Exception("Unknown host platform!");
		}

		/// <summary>
		/// Whether platform supports switching SDKs during runtime
		/// </summary>
		/// <returns>true if supports</returns>
		protected virtual bool PlatformSupportsAutoSDKs()
		{
			return false;
		}

		static private bool bCheckedAutoSDKRootEnvVar = false;
		static private bool bAutoSDKSystemEnabled = false;
		static private bool HasAutoSDKSystemEnabled()
		{
			if (!bCheckedAutoSDKRootEnvVar)
			{
				string SDKRoot = Environment.GetEnvironmentVariable(SDKRootEnvVar);
				if (SDKRoot != null)
				{
					bAutoSDKSystemEnabled = true;
				}
				bCheckedAutoSDKRootEnvVar = true;
			}
			return bAutoSDKSystemEnabled;
		}

		// Whether AutoSDK setup is safe. AutoSDKs will damage manual installs on some platforms.
		protected bool IsAutoSDKSafe()
		{
			return !IsAutoSDKDestructive() || !HasAnyManualInstall();
		}

		/// <summary>
		/// Gets the version number of the SDK setup script itself.  The version in the base should ALWAYS be the master revision from the last refactor.
		/// If you need to force a rebuild for a given platform, override this for the given platform.
		/// </summary>
		/// <returns>Setup script version</returns>
		protected virtual String GetRequiredScriptVersionString()
		{
			return "3.0";
		}

		/// <summary>
		/// Returns path to platform SDKs
		/// </summary>
		/// <returns>Valid SDK string</returns>
		protected string GetPathToPlatformAutoSDKs()
		{
			string SDKPath = "";
			string SDKRoot = Environment.GetEnvironmentVariable(SDKRootEnvVar);
			if (SDKRoot != null)
			{
				if (SDKRoot != "")
				{
					SDKPath = Path.Combine(SDKRoot, "Host" + GetAutoSDKHostPlatform(), GetAutoSDKPlatformName());
				}
			}
			return SDKPath;
		}

		/// <summary>
		/// Returns path to platform SDKs
		/// </summary>
		/// <returns>Valid SDK string</returns>
		public static bool TryGetHostPlatformAutoSDKDir(out DirectoryReference OutPlatformDir)
		{
			string SDKRoot = Environment.GetEnvironmentVariable(SDKRootEnvVar);
			if (String.IsNullOrEmpty(SDKRoot))
			{
				OutPlatformDir = null;
				return false;
			}
			else
			{
				OutPlatformDir = DirectoryReference.Combine(new DirectoryReference(SDKRoot), "Host" + GetAutoSDKHostPlatform());
				return true;
			}
		}

		/// <summary>
		/// Because most ManualSDK determination depends on reading env vars, if this process is spawned by a process that ALREADY set up
		/// AutoSDKs then all the SDK env vars will exist, and we will spuriously detect a Manual SDK. (children inherit the environment of the parent process).
		/// Therefore we write out an env variable to set in the command file (OutputEnvVars.txt) such that child processes can determine if their manual SDK detection
		/// is bogus.  Make it platform specific so that platforms can be in different states.
		/// </summary>
		protected string GetPlatformAutoSDKSetupEnvVar()
		{
			return GetAutoSDKPlatformName() + AutoSetupEnvVar;
		}

		/// <summary>
		/// Gets currently installed version
		/// </summary>
		/// <param name="PlatformSDKRoot">absolute path to platform SDK root</param>
		/// <param name="OutInstalledSDKVersionString">version string as currently installed</param>
		/// <returns>true if was able to read it</returns>
		protected bool GetCurrentlyInstalledSDKString(string PlatformSDKRoot, out string OutInstalledSDKVersionString)
		{
			if (Directory.Exists(PlatformSDKRoot))
			{
				string VersionFilename = Path.Combine(PlatformSDKRoot, CurrentlyInstalledSDKStringManifest);
				if (File.Exists(VersionFilename))
				{
					using (StreamReader Reader = new StreamReader(VersionFilename))
					{
						string Version = Reader.ReadLine();
						string Type = Reader.ReadLine();

						// don't allow ManualSDK installs to count as an AutoSDK install version.
						if (Type != null && Type == "AutoSDK")
						{
							if (Version != null)
							{
								OutInstalledSDKVersionString = Version;
								return true;
							}
						}
					}
				}
			}

			OutInstalledSDKVersionString = "";
			return false;
		}

		/// <summary>
		/// Gets the version of the last successfully run setup script.
		/// </summary>
		/// <param name="PlatformSDKRoot">absolute path to platform SDK root</param>
		/// <param name="OutLastRunScriptVersion">version string</param>
		/// <returns>true if was able to read it</returns>
		protected bool GetLastRunScriptVersionString(string PlatformSDKRoot, out string OutLastRunScriptVersion)
		{
			if (Directory.Exists(PlatformSDKRoot))
			{
				string VersionFilename = Path.Combine(PlatformSDKRoot, LastRunScriptVersionManifest);
				if (File.Exists(VersionFilename))
				{
					using (StreamReader Reader = new StreamReader(VersionFilename))
					{
						string Version = Reader.ReadLine();
						if (Version != null)
						{
							OutLastRunScriptVersion = Version;
							return true;
						}
					}
				}
			}

			OutLastRunScriptVersion = "";
			return false;
		}

		/// <summary>
		/// Sets currently installed version
		/// </summary>
		/// <param name="InstalledSDKVersionString">SDK version string to set</param>
		/// <returns>true if was able to set it</returns>
		protected bool SetCurrentlyInstalledAutoSDKString(String InstalledSDKVersionString)
		{
			String PlatformSDKRoot = GetPathToPlatformAutoSDKs();
			if (Directory.Exists(PlatformSDKRoot))
			{
				string VersionFilename = Path.Combine(PlatformSDKRoot, CurrentlyInstalledSDKStringManifest);
				if (File.Exists(VersionFilename))
				{
					File.Delete(VersionFilename);
				}

				using (StreamWriter Writer = File.CreateText(VersionFilename))
				{
					Writer.WriteLine(InstalledSDKVersionString);
					Writer.WriteLine("AutoSDK");
					return true;
				}
			}

			return false;
		}

		protected void SetupManualSDK()
		{
			if (PlatformSupportsAutoSDKs() && HasAutoSDKSystemEnabled())
			{
				String InstalledSDKVersionString = GetAutoSDKDirectoryForMasterVersion();
				String PlatformSDKRoot = GetPathToPlatformAutoSDKs();
                if (!Directory.Exists(PlatformSDKRoot))
                {
                    Directory.CreateDirectory(PlatformSDKRoot);
                }

				{
					string VersionFilename = Path.Combine(PlatformSDKRoot, CurrentlyInstalledSDKStringManifest);
					if (File.Exists(VersionFilename))
					{
						File.Delete(VersionFilename);
					}

					string EnvVarFile = Path.Combine(PlatformSDKRoot, SDKEnvironmentVarsFile);
					if (File.Exists(EnvVarFile))
					{
						File.Delete(EnvVarFile);
					}

					using (StreamWriter Writer = File.CreateText(VersionFilename))
					{
						Writer.WriteLine(InstalledSDKVersionString);
						Writer.WriteLine("ManualSDK");
					}
				}
			}
		}

		protected bool SetLastRunAutoSDKScriptVersion(string LastRunScriptVersion)
		{
			String PlatformSDKRoot = GetPathToPlatformAutoSDKs();
			if (Directory.Exists(PlatformSDKRoot))
			{
				string VersionFilename = Path.Combine(PlatformSDKRoot, LastRunScriptVersionManifest);
				if (File.Exists(VersionFilename))
				{
					File.Delete(VersionFilename);
				}

				using (StreamWriter Writer = File.CreateText(VersionFilename))
				{
					Writer.WriteLine(LastRunScriptVersion);
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Returns Hook names as needed by the platform
		/// (e.g. can be overridden with custom executables or scripts)
		/// </summary>
		/// <param name="Hook">Hook type</param>
		protected virtual string GetHookExecutableName(SDKHookType Hook)
		{
			if (IsWindows())
			{
				if (Hook == SDKHookType.Uninstall)
				{
					return "unsetup.bat";
				}
				else
				{
					return "setup.bat";
				}
			}
			else
			{
				if (Hook == SDKHookType.Uninstall)
				{
					return "unsetup.sh";
				}
				else
				{
					return "setup.sh";
				}
			}
		}

		/// <summary>
		/// Whether the hook must be run with administrator privileges.
		/// </summary>
		/// <param name="Hook">Hook for which to check the required privileges.</param>
		/// <returns>true if the hook must be run with administrator privileges.</returns>
		protected virtual bool DoesHookRequireAdmin(SDKHookType Hook)
		{
			return true;
		}

		private void LogAutoSDKHook(object Sender, DataReceivedEventArgs Args)
		{
			if (Args.Data != null)
			{
				LogFormatOptions Options = Log.OutputLevel >= LogEventType.Verbose ? LogFormatOptions.None : LogFormatOptions.NoConsoleOutput;
				Log.WriteLine(LogEventType.Log, Options, Args.Data);
			}
		}

		/// <summary>
		/// Runs install/uninstall hooks for SDK
		/// </summary>
		/// <param name="PlatformSDKRoot">absolute path to platform SDK root</param>
		/// <param name="SDKVersionString">version string to run for (can be empty!)</param>
		/// <param name="Hook">which one of hooks to run</param>
		/// <param name="bHookCanBeNonExistent">whether a non-existing hook means failure</param>
		/// <returns>true if succeeded</returns>
		protected virtual bool RunAutoSDKHooks(string PlatformSDKRoot, string SDKVersionString, SDKHookType Hook, bool bHookCanBeNonExistent = true)
		{
			if (!IsAutoSDKSafe())
			{
				Log.TraceLog(GetAutoSDKPlatformName() + " attempted to run SDK hook which could have damaged manual SDK install!");
				return false;
			}
			if (SDKVersionString != "")
			{
				string SDKDirectory = Path.Combine(PlatformSDKRoot, SDKVersionString);
				string HookExe = Path.Combine(SDKDirectory, GetHookExecutableName(Hook));

				if (File.Exists(HookExe))
				{
					Log.TraceLog("Running {0} hook {1}", Hook, HookExe);

					// run it
					Process HookProcess = new Process();
					HookProcess.StartInfo.WorkingDirectory = SDKDirectory;
					HookProcess.StartInfo.FileName = HookExe;
					HookProcess.StartInfo.Arguments = "";
					HookProcess.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;

					bool bHookRequiresAdmin = DoesHookRequireAdmin(Hook);
					if (bHookRequiresAdmin)
					{
						// installers may require administrator access to succeed. so run as an admin.
						HookProcess.StartInfo.Verb = "runas";
					}
					else
					{
						HookProcess.StartInfo.UseShellExecute = false;
						HookProcess.StartInfo.RedirectStandardOutput = true;
						HookProcess.StartInfo.RedirectStandardError = true;
						HookProcess.OutputDataReceived += LogAutoSDKHook;
						HookProcess.ErrorDataReceived += LogAutoSDKHook;
					}

					//using (ScopedTimer HookTimer = new ScopedTimer("Time to run hook: ", LogEventType.Log))
					{
						HookProcess.Start();
						if (!bHookRequiresAdmin)
						{
							HookProcess.BeginOutputReadLine();
							HookProcess.BeginErrorReadLine();
						}
						HookProcess.WaitForExit();
					}

					if (HookProcess.ExitCode != 0)
					{
						Log.TraceLog("Hook exited uncleanly (returned {0}), considering it failed.", HookProcess.ExitCode);
						return false;
					}

					return true;
				}
				else
				{
					Log.TraceLog("File {0} does not exist", HookExe);
				}
			}
			else
			{
				Log.TraceLog("Version string is blank for {0}. Can't determine {1} hook.", PlatformSDKRoot, Hook.ToString());
			}

			return bHookCanBeNonExistent;
		}

		/// <summary>
		/// Loads environment variables from SDK
		/// If any commands are added or removed the handling needs to be duplicated in
		/// TargetPlatformManagerModule.cpp
		/// </summary>
		/// <param name="PlatformSDKRoot">absolute path to platform SDK</param>
		/// <returns>true if succeeded</returns>
		protected bool SetupEnvironmentFromAutoSDK(string PlatformSDKRoot)
		{
			string EnvVarFile = Path.Combine(PlatformSDKRoot, SDKEnvironmentVarsFile);
			if (File.Exists(EnvVarFile))
			{
				using (StreamReader Reader = new StreamReader(EnvVarFile))
				{
					List<string> PathAdds = new List<string>();
					List<string> PathRemoves = new List<string>();

					List<string> EnvVarNames = new List<string>();
					List<string> EnvVarValues = new List<string>();

					bool bNeedsToWriteAutoSetupEnvVar = true;
					String PlatformSetupEnvVar = GetPlatformAutoSDKSetupEnvVar();
					for (; ; )
					{
						string VariableString = Reader.ReadLine();
						if (VariableString == null)
						{
							break;
						}

						string[] Parts = VariableString.Split('=');
						if (Parts.Length != 2)
						{
							Log.TraceLog("Incorrect environment variable declaration:");
							Log.TraceLog(VariableString);
							return false;
						}

						if (String.Compare(Parts[0], "strippath", true) == 0)
						{
							PathRemoves.Add(Parts[1]);
						}
						else if (String.Compare(Parts[0], "addpath", true) == 0)
						{
							PathAdds.Add(Parts[1]);
						}
						else
						{
							if (String.Compare(Parts[0], PlatformSetupEnvVar) == 0)
							{
								bNeedsToWriteAutoSetupEnvVar = false;
							}
							// convenience for setup.bat writers.  Trim any accidental whitespace from variable names/values.
							EnvVarNames.Add(Parts[0].Trim());
							EnvVarValues.Add(Parts[1].Trim());
						}
					}

					// don't actually set anything until we successfully validate and read all values in.
					// we don't want to set a few vars, return a failure, and then have a platform try to
					// build against a manually installed SDK with half-set env vars.
					for (int i = 0; i < EnvVarNames.Count; ++i)
					{
						string EnvVarName = EnvVarNames[i];
						string EnvVarValue = EnvVarValues[i];
						Log.TraceVerbose("Setting variable '{0}' to '{1}'", EnvVarName, EnvVarValue);
						Environment.SetEnvironmentVariable(EnvVarName, EnvVarValue);
					}


                    // actually perform the PATH stripping / adding.
                    String OrigPathVar = Environment.GetEnvironmentVariable("PATH");
                    String PathDelimiter = IsWindows() ? ";" : ":";
                    String[] PathVars = { };
                    if (!String.IsNullOrEmpty(OrigPathVar))
                    {
                        PathVars = OrigPathVar.Split(PathDelimiter.ToCharArray());
                    }
                    else
                    {
                        Log.TraceVerbose("Path environment variable is null during AutoSDK");
                    }

					List<String> ModifiedPathVars = new List<string>();
					ModifiedPathVars.AddRange(PathVars);

					// perform removes first, in case they overlap with any adds.
					foreach (String PathRemove in PathRemoves)
					{
						foreach (String PathVar in PathVars)
						{
							if (PathVar.IndexOf(PathRemove, StringComparison.OrdinalIgnoreCase) >= 0)
							{
								Log.TraceVerbose("Removing Path: '{0}'", PathVar);
								ModifiedPathVars.Remove(PathVar);
							}
						}
					}

					// remove all the of ADDs so that if this function is executed multiple times, the paths will be guaranteed to be in the same order after each run.
					// If we did not do this, a 'remove' that matched some, but not all, of our 'adds' would cause the order to change.
					foreach (String PathAdd in PathAdds)
					{
						foreach (String PathVar in PathVars)
						{
							if (String.Compare(PathAdd, PathVar, true) == 0)
							{
								Log.TraceVerbose("Removing Path: '{0}'", PathVar);
								ModifiedPathVars.Remove(PathVar);
							}
						}
					}

					// perform adds, but don't add duplicates
					foreach (String PathAdd in PathAdds)
					{
						if (!ModifiedPathVars.Contains(PathAdd))
						{
							Log.TraceVerbose("Adding Path: '{0}'", PathAdd);
							ModifiedPathVars.Add(PathAdd);
						}
					}

					String ModifiedPath = String.Join(PathDelimiter, ModifiedPathVars);
					Environment.SetEnvironmentVariable("PATH", ModifiedPath);

					Reader.Close();

					// write out environment variable command so any process using this commandfile will mark itself as having had autosdks set up.
					// avoids child processes spuriously detecting manualsdks.
					if (bNeedsToWriteAutoSetupEnvVar)
					{
						// write out the manual sdk version since child processes won't be able to detect manual with AutoSDK messing up env vars
						using (StreamWriter Writer = File.AppendText(EnvVarFile))
						{
							Writer.WriteLine("{0}={1}", PlatformSetupEnvVar, "1");
						}
						// set the variable in the local environment in case this process spawns any others.
						Environment.SetEnvironmentVariable(PlatformSetupEnvVar, "1");
					}

					// make sure we know that we've modified the local environment, invalidating manual installs for this run.
					bLocalProcessSetupAutoSDK = true;

					// tell any child processes what our manual version was before setting up autosdk
					string ValueToWrite = CachedManualSDKVersion != null ? CachedManualSDKVersion : "__None";
					Environment.SetEnvironmentVariable(GetPlatformAutoSDKSetupEnvVar(), ValueToWrite);

					return true;
				}
			}
			else
			{
				Log.TraceLog("Cannot set up environment for {1} because command file {2} does not exist.", PlatformSDKRoot, EnvVarFile);
			}

			return false;
		}

		protected void InvalidateCurrentlyInstalledAutoSDK()
		{
			String PlatformSDKRoot = GetPathToPlatformAutoSDKs();
			if (Directory.Exists(PlatformSDKRoot))
			{
				string SDKFilename = Path.Combine(PlatformSDKRoot, CurrentlyInstalledSDKStringManifest);
				if (File.Exists(SDKFilename))
				{
					File.Delete(SDKFilename);
				}

				string VersionFilename = Path.Combine(PlatformSDKRoot, LastRunScriptVersionManifest);
				if (File.Exists(VersionFilename))
				{
					File.Delete(VersionFilename);
				}

				string EnvVarFile = Path.Combine(PlatformSDKRoot, SDKEnvironmentVarsFile);
				if (File.Exists(EnvVarFile))
				{
					File.Delete(EnvVarFile);
				}
			}
		}

		/// <summary>
		/// Currently installed AutoSDK is written out to a text file in a known location.
		/// This function just compares the file's contents with the current requirements.
		/// </summary>
		public SDKStatus HasRequiredAutoSDKInstalled()
		{
			if (PlatformSupportsAutoSDKs() && HasAutoSDKSystemEnabled())
			{
				string AutoSDKRoot = GetPathToPlatformAutoSDKs();
				if (AutoSDKRoot != "")
				{
					// check script version so script fixes can be propagated without touching every build machine's CurrentlyInstalled file manually.
					bool bScriptVersionMatches = false;
					string CurrentScriptVersionString;
					if (GetLastRunScriptVersionString(AutoSDKRoot, out CurrentScriptVersionString) && CurrentScriptVersionString == GetRequiredScriptVersionString())
					{
						bScriptVersionMatches = true;
					}

					// check to make sure OutputEnvVars doesn't need regenerating
					string EnvVarFile = Path.Combine(AutoSDKRoot, SDKEnvironmentVarsFile);
					bool bEnvVarFileExists = File.Exists(EnvVarFile);

					string CurrentSDKString;
					if (bEnvVarFileExists && GetCurrentlyInstalledSDKString(AutoSDKRoot, out CurrentSDKString) && CurrentSDKString == GetAutoSDKDirectoryForMasterVersion() && bScriptVersionMatches)
					{
						return SDKStatus.Valid;
					}
					return SDKStatus.Invalid;
				}
			}
			return SDKStatus.Invalid;
		}

		// This tracks if we have already checked the sdk installation.
		private Int32 SDKCheckStatus = -1;

		// true if we've ever overridden the process's environment with AutoSDK data.  After that, manual installs cannot be considered valid ever again.
		private bool bLocalProcessSetupAutoSDK = false;

		protected bool HasSetupAutoSDK()
		{
			return bLocalProcessSetupAutoSDK || HasParentProcessSetupAutoSDK(out _);
		}

		protected bool HasParentProcessSetupAutoSDK(out string OutAutoSDKSetupValue)
		{
			bool bParentProcessSetupAutoSDK = false;
			String AutoSDKSetupVarName = GetPlatformAutoSDKSetupEnvVar();
			OutAutoSDKSetupValue = Environment.GetEnvironmentVariable(AutoSDKSetupVarName);
			if (!String.IsNullOrEmpty(OutAutoSDKSetupValue))
			{
				bParentProcessSetupAutoSDK = true;
			}
			return bParentProcessSetupAutoSDK;
		}

		public SDKStatus HasRequiredManualSDK()
		{
// 			if (HasSetupAutoSDK())
// 			{
// 				return SDKStatus.Invalid;
// 			}
//
//			// manual installs are always invalid if we have modified the process's environment for AutoSDKs
			return HasRequiredManualSDKInternal();
		}

		// for platforms with destructive AutoSDK.  Report if any manual sdk is installed that may be damaged by an autosdk.
		protected virtual bool HasAnyManualInstall()
		{
			return false;
		}

		// tells us if the user has a valid manual install.
		protected virtual SDKStatus HasRequiredManualSDKInternal()
		{
			string ManualSDKVersion;
			GetInstalledVersions(out ManualSDKVersion, out _);

			return IsVersionValid(ManualSDKVersion, bForAutoSDK:false) ? SDKStatus.Valid : SDKStatus.Invalid;
		}

		// some platforms will fail if there is a manual install that is the WRONG manual install.
		protected virtual bool AllowInvalidManualInstall()
		{
			return true;
		}

		// platforms can choose if they prefer a correct the the AutoSDK install over the manual install.
		protected virtual bool PreferAutoSDK()
		{
			return true;
		}

		// some platforms don't support parallel SDK installs.  AutoSDK on these platforms will
		// actively damage an existing manual install by overwriting files in it.  AutoSDK must NOT
		// run any setup if a manual install exists in this case.
		protected virtual bool IsAutoSDKDestructive()
		{
			return false;
		}

		/// <summary>
		/// Runs batch files if necessary to set up required AutoSDK.
		/// AutoSDKs are SDKs that have not been setup through a formal installer, but rather come from
		/// a source control directory, or other local copy.
		/// </summary>
		private void SetupAutoSDK()
		{
			if (IsAutoSDKSafe() && PlatformSupportsAutoSDKs() && HasAutoSDKSystemEnabled())
			{
				// run installation for autosdk if necessary.
				if (HasRequiredAutoSDKInstalled() == SDKStatus.Invalid)
				{
					//reset check status so any checking sdk status after the attempted setup will do a real check again.
					SDKCheckStatus = -1;

					string AutoSDKRoot = GetPathToPlatformAutoSDKs();
					string CurrentSDKString;
					GetCurrentlyInstalledSDKString(AutoSDKRoot, out CurrentSDKString);

					// switch over (note that version string can be empty)
					if (!RunAutoSDKHooks(AutoSDKRoot, CurrentSDKString, SDKHookType.Uninstall))
					{
						Log.TraceLog("Failed to uninstall currently installed SDK {0}", CurrentSDKString);
						InvalidateCurrentlyInstalledAutoSDK();
						return;
					}
					// delete Manifest file to avoid multiple uninstalls
					InvalidateCurrentlyInstalledAutoSDK();

					if (!RunAutoSDKHooks(AutoSDKRoot, GetAutoSDKDirectoryForMasterVersion(), SDKHookType.Install, false))
					{
						Log.TraceLog("Failed to install required SDK {0}.  Attemping to uninstall", GetAutoSDKDirectoryForMasterVersion());
						RunAutoSDKHooks(AutoSDKRoot, GetAutoSDKDirectoryForMasterVersion(), SDKHookType.Uninstall, false);
						return;
					}

					string EnvVarFile = Path.Combine(AutoSDKRoot, SDKEnvironmentVarsFile);
					if (!File.Exists(EnvVarFile))
					{
						Log.TraceLog("Installation of required SDK {0}.  Did not generate Environment file {1}", GetAutoSDKDirectoryForMasterVersion(), EnvVarFile);
						RunAutoSDKHooks(AutoSDKRoot, GetAutoSDKDirectoryForMasterVersion(), SDKHookType.Uninstall, false);
						return;
					}

					SetCurrentlyInstalledAutoSDKString(GetAutoSDKDirectoryForMasterVersion());
					SetLastRunAutoSDKScriptVersion(GetRequiredScriptVersionString());
				}

				// fixup process environment to match autosdk
				SetupEnvironmentFromAutoSDK();
			}
		}

		#endregion

		#region public AutoSDKs Utility

		/// <summary>
		/// Enum describing types of hooks a platform SDK can have
		/// </summary>
		public enum SDKHookType
		{
			Install,
			Uninstall
		};

		/* Whether or not we should try to automatically switch SDKs when asked to validate the platform's SDK state. */
		public static bool bAllowAutoSDKSwitching = true;

		public SDKStatus SetupEnvironmentFromAutoSDK()
		{
			string PlatformSDKRoot = GetPathToPlatformAutoSDKs();

			// load environment variables from current SDK
			if (!SetupEnvironmentFromAutoSDK(PlatformSDKRoot))
			{
				Log.TraceLog("Failed to load environment from required SDK {0}", GetAutoSDKDirectoryForMasterVersion());
				InvalidateCurrentlyInstalledAutoSDK();
				return SDKStatus.Invalid;
			}
			return SDKStatus.Valid;
		}

		/// <summary>
		/// Whether the required external SDKs are installed for this platform.
		/// Could be either a manual install or an AutoSDK.
		/// </summary>
		public SDKStatus HasRequiredSDKsInstalled()
		{
			// avoid redundant potentially expensive SDK checks.
			if (SDKCheckStatus == -1)
			{
				bool bHasManualSDK = HasRequiredManualSDK() == SDKStatus.Valid;
				bool bHasAutoSDK = HasRequiredAutoSDKInstalled() == SDKStatus.Valid;

				// Per-Platform implementations can choose how to handle non-Auto SDK detection / handling.
				SDKCheckStatus = (bHasManualSDK || bHasAutoSDK) ? 1 : 0;
			}
			return SDKCheckStatus == 1 ? SDKStatus.Valid : SDKStatus.Invalid;
		}

		// Arbitrates between manual SDKs and setting up AutoSDK based on program options and platform preferences.
		public void ManageAndValidateSDK()
		{
			// do not modify installed manifests if parent process has already set everything up.
			// this avoids problems with determining IsAutoSDKSafe and doing an incorrect invalidate.
			if (bAllowAutoSDKSwitching && !HasParentProcessSetupAutoSDK(out _))
			{
				bool bSetSomeSDK = false;
				bool bHasRequiredManualSDK = HasRequiredManualSDK() == SDKStatus.Valid;
				if (IsAutoSDKSafe() && (PreferAutoSDK() || !bHasRequiredManualSDK))
				{
					SetupAutoSDK();
					bSetSomeSDK = true;
				}

				//Setup manual SDK if autoSDK setup was skipped or failed for whatever reason.
				if (bHasRequiredManualSDK && (HasRequiredAutoSDKInstalled() != SDKStatus.Valid))
				{
					SetupManualSDK();
					bSetSomeSDK = true;
				}

				if (!bSetSomeSDK)
				{
					InvalidateCurrentlyInstalledAutoSDK();
				}
			}

			// print all SDKs to log file (errors will print out later for builds and generateprojectfiles)
			PrintSDKInfoAndReturnValidity(LogEventType.Log, LogFormatOptions.NoConsoleOutput, LogEventType.Verbose, LogFormatOptions.NoConsoleOutput);
		}
		#endregion

	}
}