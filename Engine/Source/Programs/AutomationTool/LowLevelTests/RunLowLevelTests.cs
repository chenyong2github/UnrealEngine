// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using UnrealBuildBase;
using Gauntlet;
using System.Text.RegularExpressions;
using AutomationTool.DeviceReservation;

namespace LowLevelTests
{
	public class RunLowLevelTests : BuildCommand
	{
		public override ExitCode Execute()
		{
			Log.Level = LogLevel.VeryVerbose;

			Globals.Params = new Params(Params);

			LowLevelTestExecutorOptions ContextOptions = new LowLevelTestExecutorOptions();
			AutoParam.ApplyParamsAndDefaults(ContextOptions, Globals.Params.AllArguments);

			if (ContextOptions.TestApp == string.Empty)
			{
				Log.Error("Error: -testapp flag is missing on the command line. Expected test project that extends LowLevelTests module.");
				return ExitCode.Error_Arguments;
			}

			if (string.IsNullOrEmpty(ContextOptions.Build))
			{
				Log.Error("No build path specified. Set -build= to test executable and resources directory.");
				return ExitCode.Error_Arguments;
			}

			return RunTests(ContextOptions);
		}

		public ExitCode RunTests(LowLevelTestExecutorOptions ContextOptions)
		{
			UnrealTargetPlatform TestPlatform = ContextOptions.Platform;

			LowLevelTestRoleContext RoleContext = new LowLevelTestRoleContext();
			RoleContext.Platform = TestPlatform;

			LowLevelTestsBuildSource BuildSource = new LowLevelTestsBuildSource(
				ContextOptions.TestApp,
				ContextOptions.Build,
				ContextOptions.Platform);

			SetupDevices(TestPlatform, ContextOptions);

			LowLevelTestContext TestContext = new LowLevelTestContext(BuildSource, RoleContext, ContextOptions);

			ITestNode NewTest = Gauntlet.Utils.TestConstructor.ConstructTest<ITestNode, LowLevelTestContext>(ContextOptions.TestApp, TestContext, new string[] { "LowLevelTests" });

			bool TestPassed = ExecuteTest(ContextOptions, NewTest);

			DevicePool.Instance.Dispose();

			DoCleanup(TestPlatform);

			return TestPassed ? ExitCode.Success : ExitCode.Error_TestFailure;
		}

		void DoCleanup(UnrealTargetPlatform Platform)
		{
			if (!Globals.Params.ParseParam("removedevices"))
			{
				return;
			}

			if (Platform == UnrealTargetPlatform.PS4)
			{
				string DevKitUtilPath = Path.Combine(Environment.CurrentDirectory, "Engine/Platforms/PS4/Binaries/DotNET/PS4DevKitUtil.exe");
				Log.Verbose("PS4DevkitUtil executing 'removeall'");
				IProcessResult BootResult = CommandUtils.Run(DevKitUtilPath, "removeall");
				Log.Verbose("PS4DevkitUtil 'removeall' completed with exit code {0}", BootResult.ExitCode);
			}
		}

		private bool ExecuteTest(LowLevelTestExecutorOptions Options, ITestNode LowLevelTestNode)
		{
			var Executor = new TextExecutor();

			try
			{
				bool Result = Executor.ExecuteTests(Options, new List<ITestNode>() { LowLevelTestNode });
				return Result;
			}
			catch (Exception ex)
			{
				Log.Info("");
				Log.Error("{0}.\r\n\r\n{1}", ex.Message, ex.StackTrace);

				return false;
			}
			finally
			{
				Executor.Dispose();

				DevicePool.Instance.Dispose();

				if (ParseParam("clean"))
				{
					LogInformation("Deleting temp dir {0}", Options.TempDir);
					DirectoryInfo TempDirInfo = new DirectoryInfo(Options.TempDir);
					if (TempDirInfo.Exists)
					{
						TempDirInfo.Delete(true);
					}
				}

				GC.Collect();
			}
		}

		protected void SetupDevices(UnrealTargetPlatform TestPlatform, LowLevelTestExecutorOptions Options)
		{
			Reservation.ReservationDetails = Options.JobDetails;

			DevicePool.Instance.SetLocalOptions(Options.TempDir, Options.Parallel > 1, Options.DeviceURL);
			DevicePool.Instance.AddLocalDevices(10);

			if (!string.IsNullOrEmpty(Options.Device))
			{
				DevicePool.Instance.AddDevices(TestPlatform, Options.Device);
			}
		}
	}

	public class LowLevelTestExecutorOptions : TestExecutorOptions, IAutoParamNotifiable
	{
		public Params Params { get; protected set; }

		public string TempDir;

		[AutoParam("")]
		public string DeviceURL;

		[AutoParam("")]
		public string JobDetails;

		public string TestApp;

		public string Build;

		[AutoParam("")]
		public string LogDir;

		public Type BuildSourceType { get; protected set; }

		[AutoParam(UnrealTargetConfiguration.Development)]
		public UnrealTargetConfiguration Configuration;

		public UnrealTargetPlatform Platform;
		public string Device;

		public LowLevelTestExecutorOptions()
		{
			BuildSourceType = typeof(LowLevelTestsBuildSource);
		}

		public virtual void ParametersWereApplied(string[] InParams)
		{
			Params = new Params(InParams);
			if (string.IsNullOrEmpty(TempDir))
			{
				TempDir = Globals.TempDir;
			}
			else
			{
				Globals.TempDir = TempDir;
			}

			if (string.IsNullOrEmpty(LogDir))
			{
				LogDir = Globals.LogDir;
			}
			else
			{
				Globals.LogDir = LogDir;
			}

			LogDir = Path.GetFullPath(LogDir);
			TempDir = Path.GetFullPath(TempDir);

			Build = Params.ParseValue("build=", null);
			TestApp = Globals.Params.ParseValue("testapp=", "");

			string PlatformArgString = Params.ParseValue("platform=", null);
			Platform = string.IsNullOrEmpty(PlatformArgString) ? BuildHostPlatform.Current.Platform : UnrealTargetPlatform.Parse(PlatformArgString);

			string DeviceArgString = Params.ParseValue("device=", null);
			Device = string.IsNullOrEmpty(PlatformArgString) ? "default" : DeviceArgString;

			string[] CleanArgs = Params.AllArguments
				.Where(Arg => !Arg.StartsWith("test=", StringComparison.OrdinalIgnoreCase)
					&& !Arg.StartsWith("platform=", StringComparison.OrdinalIgnoreCase)
					&& !Arg.StartsWith("device=", StringComparison.OrdinalIgnoreCase))
				.ToArray();
			Params = new Params(CleanArgs);
		}
	}

	public class LowLevelTestsSession : IDisposable
	{
		public IAppInstance Instance { get; protected set; }
		private LowLevelTestsBuildSource BuildSource { get; set; }

		public LowLevelTestsSession(LowLevelTestsBuildSource InBuildSource)
		{
			BuildSource = InBuildSource;
		}

		public bool TryReserveDevices()
		{
			Dictionary<UnrealDeviceTargetConstraint, int> RequiredDeviceTypes = new Dictionary<UnrealDeviceTargetConstraint, int>();
			// Only one device required.
			RequiredDeviceTypes.Add(new UnrealDeviceTargetConstraint(BuildSource.Platform), 1);
			return UnrealDeviceReservation.TryReserveDevices(RequiredDeviceTypes, 1);
		}

		/// <summary>
		/// Copies build folder on device and launches app natively.
		/// Does not retry.
		/// No packaging required.
		/// </summary>
		public IAppInstance InstallAndRunNativeTestApp()
		{
			bool InstallSuccess = false;
			bool RunSuccess = false;

			// TargetDevice<Platform> classes have a hard dependency on UnrealAppConfig instead of IAppConfig.
			// More refactoring needed to support non-packaged applications that can be run natively from a path on the device.
			UnrealAppConfig AppConfig = BuildSource.CreateUnrealAppConfig();

			IEnumerable<ITargetDevice> DevicesToInstallOn = UnrealDeviceReservation.ReservedDevices.ToArray();
			ITargetDevice Device = DevicesToInstallOn.Where(D => D.IsConnected && D.Platform == BuildSource.Platform).First();

			IAppInstall Install = null;

			IDeviceUsageReporter.RecordStart(Device.Name, (UnrealTargetPlatform)Device.Platform, IDeviceUsageReporter.EventType.Device, IDeviceUsageReporter.EventState.Success);
			IDeviceUsageReporter.RecordStart(Device.Name, (UnrealTargetPlatform)Device.Platform, IDeviceUsageReporter.EventType.Install, IDeviceUsageReporter.EventState.Success, BuildSource.BuildName);
			try
			{
				Install = Device.InstallApplication(AppConfig);
				InstallSuccess = true;
				IDeviceUsageReporter.RecordEnd(Device.Name, (UnrealTargetPlatform)Device.Platform, IDeviceUsageReporter.EventType.Install, IDeviceUsageReporter.EventState.Success);
			}
			catch (Exception Ex)
			{
				InstallSuccess = false;

				Log.Info("Failed to install low level tests app onto device {0}: {1}", Device, Ex);

				UnrealDeviceReservation.MarkProblemDevice(Device);
				IDeviceUsageReporter.RecordEnd(Device.Name, (UnrealTargetPlatform)Device.Platform, IDeviceUsageReporter.EventType.Install, IDeviceUsageReporter.EventState.Failure);
			}

			if (!InstallSuccess)
			{
				// release all devices
				UnrealDeviceReservation.ReleaseDevices();
				Log.Info("\nUnable to install low level tests app.\n");
			}
			else
			{
				try
				{
					Instance = Install.Run();
					IDeviceUsageReporter.RecordStart(Instance.Device.Name, (UnrealTargetPlatform)Instance.Device.Platform, IDeviceUsageReporter.EventType.Test);
					RunSuccess = true;
				}
				catch (DeviceException DeviceEx)
				{
					Log.Warning("Device {0} threw an exception during launch. \nException={1}", Install.Device, DeviceEx.Message);
					RunSuccess = false;
				}

				if (RunSuccess == false)
				{
					Log.Warning("Failed to start low level test on {0}. Marking as problem device. Will not retry.", Device);

					if (Instance != null)
					{
						Instance.Kill();
					}

					UnrealDeviceReservation.MarkProblemDevice(Device);
					UnrealDeviceReservation.ReleaseDevices();

					throw new AutomationException("Unable to start low level tests app, see warnings for details.");
				}
			}

			return Instance;
		}

		public void Dispose()
		{
			if (Instance != null)
			{
				Instance.Kill();
				IDeviceUsageReporter.RecordEnd(Instance.Device.Name, (UnrealTargetPlatform)Instance.Device.Platform, IDeviceUsageReporter.EventType.Test, IDeviceUsageReporter.EventState.Success);

				if (Instance.HasExited)
				{
					IDeviceUsageReporter.RecordEnd(Instance.Device.Name, (UnrealTargetPlatform)Instance.Device.Platform, IDeviceUsageReporter.EventType.Test, IDeviceUsageReporter.EventState.Failure);
				}
			}
		}
	}

	public class LowLevelTestRoleContext : ICloneable
	{
		public UnrealTargetRole Type { get { return UnrealTargetRole.Client; } }
		public UnrealTargetPlatform Platform;
		public UnrealTargetConfiguration Configuration { get { return UnrealTargetConfiguration.Development; } }

		public object Clone()
		{
			return this.MemberwiseClone();
		}

		public override string ToString()
		{
			string Description = string.Format("{0} {1} {2}", Platform, Configuration, Type);
			return Description;
		}
	};

	public class LowLevelTestContext : ITestContext, ICloneable
	{
		public LowLevelTestsBuildSource BuildInfo { get; private set; }

		public string WorkerJobID;

		public LowLevelTestExecutorOptions Options { get; set; }

		public Params TestParams { get; set; }

		public LowLevelTestRoleContext RoleContext { get; set; }

		public UnrealDeviceTargetConstraint Constraint;

		public LowLevelTestContext(LowLevelTestsBuildSource InBuildInfo, LowLevelTestRoleContext InRoleContext, LowLevelTestExecutorOptions InOptions)
		{
			BuildInfo = InBuildInfo;
			Options = InOptions;
			TestParams = new Params(new string[0]);
			RoleContext = InRoleContext;
		}

		public object Clone()
		{
			LowLevelTestContext Copy = (LowLevelTestContext)MemberwiseClone();
			Copy.RoleContext = (LowLevelTestRoleContext)RoleContext.Clone();
			return Copy;
		}

		public override string ToString()
		{
			string Description = string.Format("{0}", RoleContext);
			if (WorkerJobID != null)
			{
				Description += " " + WorkerJobID;
			}
			return Description;
		}
	}

	public class LowLevelTestsBuildSource : IBuildSource
	{
		private string TestApp;

		public UnrealTargetPlatform Platform { get; protected set; }
		public LowLevelTestsBuild DiscoveredBuild { get; protected set; }

		public LowLevelTestsBuildSource(string InTestApp, string InBuildPath, UnrealTargetPlatform InTargetPlatform)
		{
			TestApp = InTestApp;
			Platform = InTargetPlatform;
			InitBuildSource(InTestApp, InBuildPath, InTargetPlatform);
		}

		protected void InitBuildSource(string InTestApp, string InBuildPath, UnrealTargetPlatform InTargetPlatform)
		{
			DiscoveredBuild = LowLevelTestsBuild.CreateFromPath(InTargetPlatform, InTestApp, InBuildPath);
			if (DiscoveredBuild == null)
			{
				throw new AutomationException("No builds were discovered at path {0} matching test app name {1} and target platform {2}", InBuildPath, InTestApp, InTargetPlatform);
			}
		}

		public UnrealAppConfig CreateUnrealAppConfig()
		{
			UnrealAppConfig Config = new UnrealAppConfig();
			Config.Name = BuildName;
			Config.ProjectName = TestApp;
			Config.ProcessType = UnrealTargetRole.Client;
			Config.Platform = Platform;
			Config.Configuration = UnrealTargetConfiguration.Development;
			Config.Build = DiscoveredBuild;
			Config.Sandbox = "LowLevelTests";
			Config.FilesToCopy = new List<UnrealFileToCopy>();
			return Config;
		}

		public bool CanSupportPlatform(UnrealTargetPlatform Platform)
		{
			return true;
		}

		public static string ExeFileRegEx { get { return @"\w+Tests.exe$"; } }

		public static string SelfFileRegEx { get { return @"\w+Tests.self$"; } }

		// To be used when *both* Mac and Linux apps can be launched from Gauntlet
		public static string PosixFileRegEx { get { return @"\w+Tests$"; } }

		public static string SwitchFileRegEx { get { return @"\w+Tests.nspd"; } }

		public string BuildName { get { return TestApp; } }

		public class LooseStagedBuild : StagedBuild
		{
			public LooseStagedBuild(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfig, UnrealTargetRole InRole, string InBuildPath, string InExecutablePath)
				: base(InPlatform, InConfig, InRole, InBuildPath, InExecutablePath)
			{
			}
		}

		public class LowLevelTestsBuild : LooseStagedBuild
		{
			public LowLevelTestsBuild(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfig, UnrealTargetRole InRole, string InBuildPath, string InExecutablePath)
				: base(InPlatform, InConfig, InRole, InBuildPath, InExecutablePath)
			{
				Platform = InPlatform;
				Configuration = InConfig;
				Role = InRole;
				BuildPath = InBuildPath;
				ExecutablePath = InExecutablePath;
				Flags = BuildFlags.CanReplaceCommandLine | BuildFlags.CanReplaceExecutable | BuildFlags.Loose;
			}

			public bool CopyPlatformSpecificFiles()
			{
				try
				{
					if (Platform.IsInGroup(UnrealPlatformGroup.GDK))
					{
						const string MsGameConfigFile = "MicrosoftGame.Config";
						const string GameOSXvdFile = "gameos.xvd";
						const string ResourcesPriFile = "resources.pri";
						string LayoutDirectory = Path.Combine(Globals.UE4RootDir, "Engine", "Saved", Platform.ToString(), "Layout", "Image", "Loose");
						File.Copy(Path.Combine(LayoutDirectory, MsGameConfigFile), Path.Combine(BuildPath, MsGameConfigFile), true);
						File.Copy(Path.Combine(LayoutDirectory, GameOSXvdFile), Path.Combine(BuildPath, GameOSXvdFile), true);
						File.Copy(Path.Combine(LayoutDirectory, ResourcesPriFile), Path.Combine(BuildPath, ResourcesPriFile), true);
					}
					if (Platform.IsInGroup(UnrealPlatformGroup.Sony))
					{
						const string SceModuleDir = "sce_module";
						string UEBuildDirectory = Path.Combine(Globals.UE4RootDir, "Engine", "Build", Platform.ToString());
						string SourceDir = Path.Combine(UEBuildDirectory, SceModuleDir);
						string TargetDir = Path.Combine(BuildPath, SceModuleDir);
						if (!Directory.Exists(TargetDir))
						{
							Directory.CreateDirectory(TargetDir);
						}
						foreach(string SceFile in Directory.EnumerateFiles(SourceDir))
						{
							File.Copy(SceFile, SceFile.Replace(SourceDir, TargetDir), true);
						}
					}
				}
				catch (Exception copyEx)
				{
					Log.Error("Could not copy {0} specific files: {1}.", Platform.ToString(), copyEx);
					return false;
				}
				
				return true;
			}

			public bool CleanupUnusedFiles()
			{
				try
				{
					string[] BuildFiles = Directory.GetFiles(BuildPath);
					foreach (string BuildFile in BuildFiles)
					{
						if (new FileInfo(BuildFile).Extension == ".pdb")
						{
							File.Delete(BuildFile);
						}
					}

				}
				catch (Exception cleanupEx)
				{
					Log.Error("Could not cleanup files for {0} build: {1}.", Platform.ToString(), cleanupEx);
					return false;
				}
				return true;
			}

			public static LowLevelTestsBuild CreateFromPath(UnrealTargetPlatform InPlatform, string InTestApp, string InBuildPath)
			{
				LowLevelTestsBuild DiscoveredBuild = null;
				IEnumerable<string> Executables = new List<string>();
				if (InPlatform.IsInGroup(UnrealPlatformGroup.GDK))
				{
					Executables = DirectoryUtils.FindFiles(InBuildPath, new Regex(ExeFileRegEx));
				}
				else if (InPlatform.IsInGroup(UnrealPlatformGroup.Sony))
				{
					Executables = DirectoryUtils.FindFiles(InBuildPath, new Regex(SelfFileRegEx));
				}
				else if (InPlatform == UnrealTargetPlatform.Switch)
				{
					Executables = DirectoryUtils.FindFiles(InBuildPath, new Regex(SwitchFileRegEx));
				}
				foreach (string Executable in Executables)
				{
					UnrealTargetConfiguration UnrealConfig = UnrealTargetConfiguration.Development;
					if (InBuildPath.ToLower().Contains(InPlatform.ToString().ToLower()))
					{
						if (InBuildPath.ToLower().Contains(InTestApp.ToString().ToLower()))
						{
							DiscoveredBuild = new LowLevelTestsBuild(InPlatform, UnrealConfig, UnrealTargetRole.Client, InBuildPath, Executable);
							DiscoveredBuild.CopyPlatformSpecificFiles();
							DiscoveredBuild.CleanupUnusedFiles();
							break;
						}
					}
				}
				return DiscoveredBuild;
			}
		}
	}
}