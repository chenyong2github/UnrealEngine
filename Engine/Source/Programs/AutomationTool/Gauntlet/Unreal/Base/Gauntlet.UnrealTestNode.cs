// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using System.Threading;
using System.Text.RegularExpressions;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Security.Cryptography;

namespace Gauntlet
{

	public abstract class UnrealTestNode<TConfigClass> : BaseTest, IDisposable
		where TConfigClass : UnrealTestConfiguration, new()
	{
		/// <summary>
		/// Returns an identifier for this test
		/// </summary>
		public override string Name { get { return this.GetType().FullName; } }

		/// <summary>
		/// How long this test should run for, set during LaunchTest based on results of GetConfiguration
		/// </summary>
		public override float MaxDuration { get; protected set; }

		/// <summary>
		/// Priority of this test
		/// </summary>
		public override TestPriority Priority { get { return GetPriority(); } }

		/// <summary>
		/// Returns Warnings found during tests. By default only ensures are considered
		/// </summary>
		public override IEnumerable<string> GetWarnings()
		{
			if (SessionArtifacts == null)
			{
				return new string[0];
			}

			return SessionArtifacts.SelectMany(A =>
			{
				return A.LogSummary.Ensures.Select(E => E.Message);
			}); 
		}

		/// <summary>
		/// Returns Errors found during tests. By default only fatal errors are considered
		/// </summary>
		public override IEnumerable<string> GetErrors()
		{
			if (SessionArtifacts == null)
			{
				return new string[0];
			}

			var FailedArtifacts = GetArtifactsWithFailures();

			return FailedArtifacts.Where(A => A.LogSummary.FatalError != null).Select(A => A.LogSummary.FatalError.Message);
		}

		// Begin UnrealTestNode properties and members

		/// <summary>
		/// Our context that holds environment wide info about the required conditions for the test
		/// </summary>
		public UnrealTestContext Context { get; private set; }

		/// <summary>
		/// When the test is running holds all running Unreal processes (clients, servers etc).
		/// </summary>
		public UnrealSessionInstance TestInstance { get; private set; }

		/// <summary>
		/// After the test completes holds artifacts for each process (clients, servers etc).
		/// </summary>
		public IEnumerable<UnrealRoleArtifacts> SessionArtifacts { get; private set; }

		/// <summary>
		/// Whether we submit to the dashboard
		/// </summary>
		public virtual bool ShouldSubmitDashboardResult { get { return CommandUtils.IsBuildMachine; } }

		/// <summary>
		/// Helper class that turns our wishes into reallity
		/// </summary>
		protected UnrealSession UnrealApp;

		/// <summary>
		/// Used to track how much of our log has been written out
		/// </summary>
		private int LastLogCount;

		private int CurrentPass;

		private int NumPasses;

		static protected DateTime SessionStartTime = DateTime.MinValue;

		/// <summary>
		/// Standard semantic versioning for tests. Should be overwritten within individual tests, and individual test maintainers
		/// are responsible for updating their own versions. See https://semver.org/ for more info on maintaining semantic versions.
		/// </summary>
		/// 
		protected Version TestVersion;
		

		/// <summary>
		/// Path to the directory that logs and other artifacts are copied to after the test run.
		/// </summary>
		protected string ArtifactPath { get; private set; }

		/// <summary>
		/// Our test result. May be set directly, or by overriding GetUnrealTestResult()
		/// </summary>
		private TestResult UnrealTestResult;

		protected TConfigClass CachedConfig = null;

		/// <summary>
		/// If our test should exit suddenly, this is the process that caused it
		/// </summary>
		protected List<IAppInstance> MissingProcesses;

		protected DateTime TimeOfFirstMissingProcess;

		protected int TimeToWaitForProcesses { get; set; }
		
		protected DateTime LastHeartbeatTime = DateTime.MinValue;
		protected DateTime LastActiveHeartbeatTime = DateTime.MinValue;

		// End  UnrealTestNode properties and members 

		// artifact paths that have been used in this run
		static protected HashSet<string> ReservedArtifcactPaths = new HashSet<string>();

		public UnrealTestNode(UnrealTestContext InContext)
		{
			Context = InContext;

			UnrealTestResult = TestResult.Invalid;
			MissingProcesses = new List<IAppInstance>();
			TimeToWaitForProcesses = 5;
			LastLogCount = 0;
			CurrentPass = 0;
			NumPasses = 0;
			TestVersion = new Version("1.0.0");
			ArtifactPath = string.Empty;
		}

		 ~UnrealTestNode()
		{
			Dispose(false);
		}

		#region IDisposable Support
		private bool disposedValue = false; // To detect redundant calls

		protected virtual void Dispose(bool disposing)
		{
			if (!disposedValue)
			{
				if (disposing)
				{
					// TODO: dispose managed state (managed objects).
				}

				CleanupTest();

				disposedValue = true;
			}
		}

		// This code added to correctly implement the disposable pattern.
		public void Dispose()
		{
			// Do not change this code. Put cleanup code in Dispose(bool disposing) above.
			Dispose(true);
		}
		#endregion

		public override String ToString()
		{
			if (Context == null)
			{
				return Name;
			}

			return string.Format("{0} ({1})", Name, Context);
		}

		/// <summary>
		/// Sets the context that tests run under. Called once during creation
		/// </summary>
		/// <param name="InContext"></param>
		/// <returns></returns>
		public override void SetContext(ITestContext InContext)
		{
			Context = InContext as UnrealTestContext;
		}


		/// <summary>
		/// Returns information about how to configure our Unreal processes. For the most part the majority
		/// of Unreal tests should only need to override this function
		/// </summary>
		/// <returns></returns>
		public virtual TConfigClass GetConfiguration()
		{
			if (CachedConfig == null)
			{
				CachedConfig = new TConfigClass();
				AutoParam.ApplyParamsAndDefaults(CachedConfig, this.Context.TestParams.AllArguments);
			}
			return CachedConfig;
		}

		/// <summary>
		/// Returns the cached version of our config. Avoids repeatedly calling GetConfiguration() on derived nodes
		/// </summary>
		/// <returns></returns>
		protected TConfigClass GetCachedConfiguration()
		{
			if (CachedConfig == null)
			{
				return GetConfiguration();
			}

			return CachedConfig;
		}

		/// <summary>
		/// Returns a priority value for this test
		/// </summary>
		/// <returns></returns>
		protected TestPriority GetPriority()
		{
			IEnumerable<UnrealTargetPlatform> DesktopPlatforms = UnrealBuildTool.Utils.GetPlatformsInClass(UnrealPlatformClass.Desktop);

			UnrealTestRoleContext ClientContext = Context.GetRoleContext(UnrealTargetRole.Client);

			// because these need deployed we want them in flight asap
			if (ClientContext.Platform == UnrealTargetPlatform.PS4 || ClientContext.Platform == UnrealTargetPlatform.XboxOne)
			{
				return TestPriority.High;
			}

			return TestPriority.Normal;
		}

		protected virtual IEnumerable<UnrealSessionInstance.RoleInstance> FindAnyMissingRoles()
		{
			return TestInstance.RunningRoles.Where(R => R.AppInstance.HasExited);
		}

		/// <summary>
		/// Checks whether the test is still running. The default implementation checks whether all of our processes
		/// are still alive.
		/// </summary>
		/// <returns></returns>
		public virtual bool IsTestRunning()
		{
			var MissingRoles = FindAnyMissingRoles().ToList();

			if (MissingRoles.Count == 0)
			{
				// nothing missing, keep going.
				return true;
			}
			
			// if all roles are gone, we're done
			if (MissingRoles.Count == TestInstance.RunningRoles.Count())
			{
				return false;
			}

			// This test only ends when all roles are gone
			if (GetCachedConfiguration().AllRolesExit)
			{
				return true;
			}

			if (TimeOfFirstMissingProcess == DateTime.MinValue)
			{
				TimeOfFirstMissingProcess = DateTime.Now;
				Log.Verbose("Role {0} exited. Waiting {1} seconds for others to exit", MissingRoles.First().ToString(), TimeToWaitForProcesses);
			}

			if ((DateTime.Now - TimeOfFirstMissingProcess).TotalSeconds < TimeToWaitForProcesses)
			{
				// give other processes time to exit normally
				return true;
			}

			Log.Info("Ending {0} due to exit of Role {1}. {2} processes still running", Name, MissingRoles.First().ToString(), TestInstance.RunningRoles.Count());

			// Done!
			return false;
		}

		protected bool PrepareUnrealApp()
		{
			// Get our configuration
			TConfigClass Config = GetCachedConfiguration();

			if (Config == null)
			{
				throw new AutomationException("Test {0} returned null config!", this);
			}

			if (UnrealApp != null)
			{
				throw new AutomationException("Node already has an UnrealApp, was PrepareUnrealSession called twice?");
			}

			// pass through any arguments such as -TestNameArg or -TestNameArg=Value
			var TestName = this.GetType().Name;
			var ShortName = TestName.Replace("Test", "");

			var PassThroughArgs = Context.TestParams.AllArguments
				.Where(A => A.StartsWith(TestName, System.StringComparison.OrdinalIgnoreCase) || A.StartsWith(ShortName, System.StringComparison.OrdinalIgnoreCase))
				.Select(A =>
				{
					A = "-" + A;

					var EqIndex = A.IndexOf("=");

					// no =? Just a -switch then
					if (EqIndex == -1)
					{
						return A;
					}

					var Cmd = A.Substring(0, EqIndex + 1);
					var Args = A.Substring(EqIndex + 1);

					// if no space in the args, just leave it
					if (Args.IndexOf(" ") == -1)
					{
						return A;
					}

					return string.Format("{0}\"{1}\"", Cmd, Args);
				});

			List<UnrealSessionRole> SessionRoles = new List<UnrealSessionRole>();

			// Go through each type of role that was required and create a session role
			foreach (var TypesToRoles in Config.RequiredRoles)
			{
				// get the actual context of what this role means.
				UnrealTestRoleContext RoleContext = Context.GetRoleContext(TypesToRoles.Key);

				foreach (UnrealTestRole TestRole in TypesToRoles.Value)
				{
					// If a config has overriden a platform then we can't use the context constraints from the commandline
					bool UseContextConstraint = TestRole.Type == UnrealTargetRole.Client && TestRole.PlatformOverride == null;

					// important, use the type from the ContextRolke because Server may have been mapped to EditorServer etc
					UnrealTargetPlatform SessionPlatform = TestRole.PlatformOverride ?? RoleContext.Platform;

					UnrealSessionRole SessionRole = new UnrealSessionRole(RoleContext.Type, SessionPlatform, RoleContext.Configuration, TestRole.CommandLine);

					SessionRole.RoleModifier = TestRole.RoleType;
					SessionRole.Constraint = UseContextConstraint ? Context.Constraint : new UnrealTargetConstraint(SessionPlatform);
					
					Log.Verbose("Created SessionRole {0} from RoleContext {1} (RoleType={2})", SessionRole, RoleContext, TypesToRoles.Key);

					// TODO - this can all / mostly go into UnrealTestConfiguration.ApplyToConfig

					// Deal with command lines
					if (string.IsNullOrEmpty(TestRole.ExplicitClientCommandLine) == false)
					{
						SessionRole.CommandLine = TestRole.ExplicitClientCommandLine;
					}
					else
					{
						// start with anything from our context
						SessionRole.CommandLine = RoleContext.ExtraArgs;

						// did the test ask for anything?
						if (string.IsNullOrEmpty(TestRole.CommandLine) == false)
						{
							SessionRole.CommandLine += " " + TestRole.CommandLine;
						}

						// add controllers
						SessionRole.CommandLine += TestRole.Controllers.Count > 0 ?
							string.Format(" -gauntlet=\"{0}\"", string.Join(",", TestRole.Controllers)) 
							: " -gauntlet";

						if (PassThroughArgs.Count() > 0)
						{
							SessionRole.CommandLine += " " + string.Join(" ", PassThroughArgs);
						}

						// add options
						SessionRole.Options = Config;
					}

					if (RoleContext.Skip)
					{
						SessionRole.RoleModifier = ERoleModifier.Null;
					}

					// copy over relevant settings from test role
                    SessionRole.FilesToCopy = TestRole.FilesToCopy;
					SessionRole.AdditionalArtifactDirectories = TestRole.AdditionalArtifactDirectories;
					SessionRole.ConfigureDevice = TestRole.ConfigureDevice;
					SessionRole.MapOverride = TestRole.MapOverride;

					SessionRoles.Add(SessionRole);
				}
			}

			UnrealApp = new UnrealSession(Context.BuildInfo, SessionRoles) { Sandbox = Context.Options.Sandbox };

			return true;
		}

		public override bool IsReadyToStart()
		{
			if (UnrealApp == null)
			{
				PrepareUnrealApp();
			}

			return UnrealApp.TryReserveDevices();
		}

		/// <summary>
		/// Called by the test executor to start our test running. After this
		/// Test.Status should return InProgress or greater
		/// </summary>
		/// <returns></returns>
		public override bool StartTest(int Pass, int InNumPasses)
		{
			if (UnrealApp == null)
			{
				throw new AutomationException("Node already has a null UnrealApp, was PrepareUnrealSession or IsReadyToStart called?");
			}

			TConfigClass Config = GetCachedConfiguration();

			CurrentPass = Pass;
			NumPasses = InNumPasses;

			// Either use the ArtifactName param or name of this test
			string TestFolder = string.IsNullOrEmpty(Context.Options.ArtifactName) ? this.ToString() : Context.Options.ArtifactName;

			if (string.IsNullOrEmpty(Context.Options.ArtifactPostfix) == false)
			{
				TestFolder += "_" + Context.Options.ArtifactPostfix;
			}

			TestFolder = TestFolder.Replace(" ", "_");
			TestFolder = TestFolder.Replace(":", "_");
			TestFolder = TestFolder.Replace("|", "_");
			TestFolder = TestFolder.Replace(",", "");

			ArtifactPath = Path.Combine(Context.Options.LogDir, TestFolder);
		
			// if doing multiple passes, put each in a subdir
			if (NumPasses > 1)
			{
				ArtifactPath = Path.Combine(ArtifactPath, string.Format("Pass_{0}_of_{1}", CurrentPass + 1, NumPasses));
			}

			// When running with -parallel we could have several identical tests (same test, configurations) in flight so
			// we need unique artifact paths. We also don't overwrite dest directories from the build machine for the same
			// reason of multiple tests for a build. Really though these should use ArtifactPrefix to save to
			// SmokeTest_HighQuality etc
			int ArtifactNumericPostfix = 0;
			bool ArtifactPathIsTaken = false;

			do
			{
				string PotentialPath = ArtifactPath;

				if (ArtifactNumericPostfix > 0)
				{
					PotentialPath = string.Format("{0}_{1}", ArtifactPath, ArtifactNumericPostfix);
				}

				ArtifactPathIsTaken = ReservedArtifcactPaths.Contains(PotentialPath) || (CommandUtils.IsBuildMachine && Directory.Exists(PotentialPath));

				if (ArtifactPathIsTaken)
				{
					Log.Info("Directory already exists at {0}", PotentialPath);
					ArtifactNumericPostfix++;
				}
				else
				{
					ArtifactPath = PotentialPath;
				}

			} while (ArtifactPathIsTaken);

			ReservedArtifcactPaths.Add(ArtifactPath);

			// Launch the test
			TestInstance = UnrealApp.LaunchSession();

			// track the overall session time
			if (SessionStartTime == DateTime.MinValue)
			{
				SessionStartTime = DateTime.Now;
			}

			if (TestInstance != null)
			{
				// Update these for the executor
				MaxDuration = Config.MaxDuration;
				UnrealTestResult = TestResult.Invalid;
				MarkTestStarted();
			}
			
			return TestInstance != null;
		}

		/// <summary>
		/// Cleanup all resources
		/// </summary>
		/// <param name="Node"></param>
		/// <returns></returns>
		public override void CleanupTest()
		{
			if (TestInstance != null)
			{
				TestInstance.Dispose();
				TestInstance = null;
			}

			if (UnrealApp != null)
			{
				UnrealApp.Dispose();
				UnrealApp = null;
			}			
		}

		/// <summary>
		/// Restarts the provided test. Only called if one of our derived
		/// classes requests it via the Status result
		/// </summary>
		/// <param name="Node"></param>
		/// <returns></returns>
		public override bool RestartTest()
		{
			TestInstance = UnrealApp.RestartSession();

			bool bWasRestarted = (TestInstance != null);
			if (bWasRestarted)
			{
				MarkTestStarted();
			}

			return bWasRestarted;
		}

		/// <summary>
		/// Periodically called while the test is running. A chance for tests to examine their
		/// health, log updates etc. Base classes must call this or take all responsibility for
		/// setting Status as necessary
		/// </summary>
		/// <returns></returns>
		public override void TickTest()
		{
			IAppInstance App = null;

			if (TestInstance.ClientApps == null)
			{
				App = TestInstance.ServerApp;
			}
			else
			{
				if (TestInstance.ClientApps.Length > 0)
				{
					App = TestInstance.ClientApps.First();
				}
			}

			if (App != null)
			{
				UnrealLogParser Parser = new UnrealLogParser(App.StdOut);
				
				// TODO - hardcoded for Orion
				List<string> TestLines = Parser.GetLogChannel("Gauntlet").ToList();

				TestLines.AddRange(Parser.GetLogChannel("OrionTest"));

				for (int i = LastLogCount; i < TestLines.Count; i++)
				{
					Log.Info(TestLines[i]);

					if (Regex.IsMatch(TestLines[i], @".*GauntletHeartbeat\: Active.*"))
					{
						LastHeartbeatTime = DateTime.Now;
						LastActiveHeartbeatTime = DateTime.Now;
					}
					else if (Regex.IsMatch(TestLines[i], @".*GauntletHeartbeat\: Idle.*"))
					{
						LastHeartbeatTime = DateTime.Now;
					}
				}

				LastLogCount = TestLines.Count;

				// Detect missed heartbeats and fail the test
				CheckHeartbeat();
			}


			// Check status and health after updating logs
			if (GetTestStatus() == TestStatus.InProgress && IsTestRunning() == false)
			{
				MarkTestComplete();
			}
		}

		/// <summary>
		/// Called when a test has completed. By default saves artifacts and calles CreateReport
		/// </summary>
		/// <param name="Result"></param>
		/// <returns></returns>
		public override void StopTest(bool WasCancelled)
		{
			base.StopTest(WasCancelled);

			// Shutdown the instance so we can access all files, but do not null it or shutdown the UnrealApp because we still need
			// access to these objects and their resources! Final cleanup is done in CleanupTest()
			TestInstance.Shutdown();

			try
			{
				Log.Info("Saving artifacts to {0}", ArtifactPath);
				Directory.CreateDirectory(ArtifactPath);
				Utils.SystemHelpers.MarkDirectoryForCleanup(ArtifactPath);

				SessionArtifacts = SaveRoleArtifacts(ArtifactPath);

				// call legacy version
				SaveArtifacts_DEPRECATED(ArtifactPath);
			}
			catch (Exception Ex)
			{
				Log.Warning("Failed to save artifacts. {0}", Ex);
			}

			try
			{
				// Artifacts have been saved, release devices back to pool for other tests to use
				UnrealApp.ReleaseDevices();
			}
			catch (Exception Ex)
			{
				Log.Warning("Failed to release devices. {0}", Ex);
			}

			string Message = string.Empty;

			try
			{
				CreateReport(GetTestResult(), Context, Context.BuildInfo, SessionArtifacts, ArtifactPath);
			}
			catch (Exception Ex)
			{
				CreateReportFailed = true;
				Message = Ex.Message;				
				Log.Warning("Failed to save completion report. {0}", Ex);
			}

			if (CreateReportFailed)
			{
				try
				{
					HandleCreateReportFailure(Context, Message);
				}
				catch (Exception Ex)
				{
					Log.Warning("Failed to handle completion report failure. {0}", Ex);
				}
			}

			try
			{
				SubmitToDashboard(GetTestResult(), Context, Context.BuildInfo, SessionArtifacts, ArtifactPath);
			}
			catch (Exception Ex)
			{				
				Log.Warning("Failed to submit results to dashboard. {0}", Ex);
			}
		}

		/// <summary>
		/// Called when report creation fails, by default logs warning with failure message
		/// </summary>
		protected virtual void HandleCreateReportFailure(UnrealTestContext Context, string Message = "")
		{
			if (string.IsNullOrEmpty(Message))
			{
				Message = string.Format("See Gauntlet.log for details");
			}

			if (Globals.IsWorker)
			{
				// log for worker to parse context
				Log.Info("GauntletWorker:CreateReportFailure:{0}", Context.WorkerJobID);
			}

			Log.Warning("CreateReport Failed: {0}", Message);
		}

		/// <summary>
		/// Whether creating the test report failed
		/// </summary>
		public bool CreateReportFailed { get; protected set; }
		 

		/// <summary>
		/// Optional function that is called on test completion and gives an opportunity to create a report
		/// </summary>
		/// <param name="Result"></param>
		/// <param name="Contex"></param>
		/// <param name="Build"></param>
		public virtual void CreateReport(TestResult Result, UnrealTestContext Context, UnrealBuildSource Build, IEnumerable<UnrealRoleArtifacts> Artifacts, string ArtifactPath)
		{
		}

		/// <summary>
		/// Optional function that is called on test completion and gives an opportunity to create a report
		/// </summary>
		/// <param name="Result"></param>
		/// <param name="Contex"></param>
		/// <param name="Build"></param>
		public virtual void SubmitToDashboard(TestResult Result, UnrealTestContext Contex, UnrealBuildSource Build, IEnumerable<UnrealRoleArtifacts> Artifacts, string ArtifactPath)
		{
		}

		/// <summary>
		/// Called to request that the test save all artifacts from the completed test to the specified 
		/// output path. By default the app will save all logs and crash dumps
		/// </summary>
		/// <param name="Completed"></param>
		/// <param name="Node"></param>
		/// <param name="OutputPath"></param>
		/// <returns></returns>
		public virtual void SaveArtifacts_DEPRECATED(string OutputPath)
		{
			// called for legacy reasons
		}

		/// <summary>
		/// Called to request that the test save all artifacts from the completed test to the specified 
		/// output path. By default the app will save all logs and crash dumps
		/// </summary>
		/// <param name="Completed"></param>
		/// <param name="Node"></param>
		/// <param name="OutputPath"></param>
		/// <returns></returns>
		public virtual IEnumerable<UnrealRoleArtifacts> SaveRoleArtifacts(string OutputPath)
		{
			return UnrealApp.SaveRoleArtifacts(Context, TestInstance, ArtifactPath);
		}

		/// <summary>
		/// Parses the provided artifacts to determine the cause of an exit and whether it was abnormal
		/// </summary>
		/// <param name="InArtifacts"></param>
		/// <param name="Reason"></param>
		/// <param name="WasAbnormal"></param>
		/// <returns></returns>
		protected virtual int GetExitCodeAndReason(UnrealRoleArtifacts InArtifacts, out string ExitReason)
		{
			UnrealLogParser.LogSummary LogSummary = InArtifacts.LogSummary;

			// Assume failure!
			int ExitCode = -1;
			ExitReason = "Unknown";

			if (LogSummary.FatalError != null)
			{
				ExitReason = "Process encountered fatal error";
			}
			else if (LogSummary.Ensures.Count() > 0 && CachedConfig.FailOnEnsures)
			{
				ExitReason = string.Format("Process encountered {0} Ensures", LogSummary.Ensures.Count());
			}
			else if (InArtifacts.AppInstance.WasKilled)
			{
				ExitReason = "Process was killed";
			}
			else if (LogSummary.HasTestExitCode)
			{
				if (LogSummary.TestExitCode == 0)
				{
					ExitReason = "Process exited with code 0";
				}
				else
				{
					ExitReason = string.Format("Process exited with error code {0}", LogSummary.TestExitCode);
				}

				ExitCode = LogSummary.TestExitCode;
			}
			else if (LogSummary.RequestedExit)
			{
				ExitReason = string.Format("Process requested exit with no fatal errors");
				ExitCode = 0;
			}
			else
			{
				bool WasGauntletTest = InArtifacts.SessionRole.CommandLine.ToLower().Contains("-gauntlet");
				// ok, process appears to have exited for no good reason so try to divine a result...
				if (WasGauntletTest)
				{
					if (LogSummary.HasTestExitCode == false)
					{
						Log.Verbose("Role {0} had 0 exit code but used Gauntlet and no TestExitCode was found. Assuming failure", InArtifacts.SessionRole.RoleType);
						ExitCode = -1;
						ExitReason = "No test result from Gauntlet controller";
					}
				}
				else
				{
					// if all else fails, fall back to the exit code from the process. Not great.
					ExitCode = InArtifacts.AppInstance.ExitCode;
					if (ExitCode == 0)
					{
						ExitReason = "app exited with code 0";
					}
				}
			}

			// Normal exits from server are not ok if we had clients running!
			if (ExitCode == 0 && InArtifacts.SessionRole.RoleType.IsServer())
			{
				bool ClientsKilled = SessionArtifacts.Any(A => A.AppInstance.WasKilled && A.SessionRole.RoleType.IsClient());

				if (ClientsKilled)
				{
					ExitCode = -1;
					ExitReason = "Server exited while clients were running";
				}
			}

			if (ExitCode == -1 && string.IsNullOrEmpty(ExitReason))
			{
				ExitReason = "Process exited with no indication of success";
			}

			return ExitCode;
		}

		private void CheckHeartbeat()
		{
			if (CachedConfig == null 
				|| CachedConfig.DisableHeartbeatTimeout
				|| CachedConfig.HeartbeatOptions.bExpectHeartbeats == false
				|| GetTestStatus() != TestStatus.InProgress)
			{
				return;
			}

			UnrealHeartbeatOptions HeartbeatOptions = CachedConfig.HeartbeatOptions;

			// First active heartbeat has not happened yet and timeout before first active heartbeat is enabled
			if (LastActiveHeartbeatTime == DateTime.MinValue && HeartbeatOptions.TimeoutBeforeFirstActiveHeartbeat > 0)
			{
				double SecondsSinceSessionStart = DateTime.Now.Subtract(SessionStartTime).TotalSeconds;
				if (SecondsSinceSessionStart > HeartbeatOptions.TimeoutBeforeFirstActiveHeartbeat)
				{
					Log.Error("{0} seconds have passed without detecting the first active Gauntlet heartbeat.", HeartbeatOptions.TimeoutBeforeFirstActiveHeartbeat);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}

			// First active heartbeat has happened and timeout between active heartbeats is enabled
			if (LastActiveHeartbeatTime != DateTime.MinValue && HeartbeatOptions.TimeoutBetweenActiveHeartbeats > 0)
			{
				double SecondsSinceLastActiveHeartbeat = DateTime.Now.Subtract(LastActiveHeartbeatTime).TotalSeconds;
				if (SecondsSinceLastActiveHeartbeat > HeartbeatOptions.TimeoutBetweenActiveHeartbeats)
				{
					Log.Error("{0} seconds have passed without detecting any active Gauntlet heartbeats.", HeartbeatOptions.TimeoutBetweenActiveHeartbeats);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}

			// First heartbeat has happened and timeout between heartbeats is enabled
			if (LastHeartbeatTime != DateTime.MinValue && HeartbeatOptions.TimeoutBetweenAnyHeartbeats > 0)
			{
				double SecondsSinceLastHeartbeat = DateTime.Now.Subtract(LastHeartbeatTime).TotalSeconds;
				if (SecondsSinceLastHeartbeat > HeartbeatOptions.TimeoutBetweenAnyHeartbeats)
				{
					Log.Error("{0} seconds have passed without detecting any Gauntlet heartbeats.", HeartbeatOptions.TimeoutBetweenAnyHeartbeats);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}
		}

		/// <summary>
		/// Returns a hash that represents the results of a role. Should be 0 if no fatal errors or ensures
		/// </summary>
		/// <param name="InArtifacts"></param>
		/// <returns></returns>
		protected virtual string GetRoleResultHash(UnrealRoleArtifacts InArtifacts)
		{
			const int MaxCallstackLines = 10;			

			UnrealLogParser.LogSummary LogSummary = InArtifacts.LogSummary;

			string TotalString = "";

			//Func<int, string> ComputeHash = (string Str) => { return Hasher.ComputeHash(Encoding.UTF8.GetBytes(Str)); };
			
			if (LogSummary.FatalError != null)
			{				
				TotalString += string.Join("\n", InArtifacts.LogSummary.FatalError.Callstack.Take(MaxCallstackLines));
				TotalString += "\n";
			}

			foreach (var Ensure in LogSummary.Ensures)
			{
				TotalString += string.Join("\n", Ensure.Callstack.Take(MaxCallstackLines));
				TotalString += "\n";
			}

			string Hash = Hasher.ComputeHash(TotalString);

			return Hash;
		}

		/// <summary>
		/// Returns a hash that represents the failure results of this test. If the test failed this should be an empty string
		/// </summary>
		/// <returns></returns>
		protected virtual string GetTestResultHash()
		{
			IEnumerable<string> RoleHashes = SessionArtifacts.Select(A => GetRoleResultHash(A)).OrderBy(S => S);

			RoleHashes = RoleHashes.Where(S => S.Length > 0 && S != "0");

			string Combined = string.Join("\n", RoleHashes);

			string CombinedHash = Hasher.ComputeHash(Combined);

			return CombinedHash;
		}

		/// <summary>
		/// Parses the output of an application to try and determine a failure cause (if one exists). Returns
		/// 0 for graceful shutdown
		/// </summary>
		/// <param name="Prefix"></param>
		/// <param name="App"></param>
		/// <returns></returns>
		protected virtual int GetRoleSummary(UnrealRoleArtifacts InArtifacts, out string Summary)
		{

			const int MaxLogLines = 10;
			const int MaxCallstackLines = 20;

			UnrealLogParser.LogSummary LogSummary = InArtifacts.LogSummary;
						
			string ExitReason = "Unknown";
			int ExitCode = GetExitCodeAndReason(InArtifacts, out ExitReason);

			MarkdownBuilder MB = new MarkdownBuilder();

			MB.H3(string.Format("Role: {0} ({1} {2})", InArtifacts.SessionRole.RoleType, InArtifacts.SessionRole.Platform, InArtifacts.SessionRole.Configuration));

			if (ExitCode != 0)
			{
				MB.H4(string.Format("Result: Abnormal Exit: Reason={0}, Code={1}", ExitReason, ExitCode));
			}
			else
			{
				MB.H4(string.Format("Result: Reason={0}, Code=0", ExitReason));
			}

			int FatalErrors = LogSummary.FatalError != null ? 1 : 0;

			if (LogSummary.FatalError != null)
			{
				MB.H4(string.Format("Fatal Error: {0}", LogSummary.FatalError.Message));
				MB.UnorderedList(InArtifacts.LogSummary.FatalError.Callstack.Take(MaxCallstackLines));

				if (InArtifacts.LogSummary.FatalError.Callstack.Count() > MaxCallstackLines)
				{
					MB.Paragraph("See log for full callstack");
				}
			}

			if (LogSummary.Ensures.Count() > 0)
			{
				foreach (var Ensure in LogSummary.Ensures)
				{
					MB.H4(string.Format("Ensure: {0}", Ensure.Message));
					MB.UnorderedList(Ensure.Callstack.Take(MaxCallstackLines));

					if (Ensure.Callstack.Count() > MaxCallstackLines)
					{
						MB.Paragraph("See log for full callstack");
					}
				}
			}

			MB.Paragraph(string.Format("FatalErrors: {0}, Ensures: {1}, Errors: {2}, Warnings: {3}, Hash: {4}",
				FatalErrors, LogSummary.Ensures.Count(), LogSummary.Errors.Count(), LogSummary.Warnings.Count(), GetRoleResultHash(InArtifacts)));

			if (GetCachedConfiguration().ShowErrorsInSummary && InArtifacts.LogSummary.Errors.Count() > 0)
			{
				MB.H4("Errors");
				MB.UnorderedList(LogSummary.Errors.Take(MaxLogLines));

				if (LogSummary.Errors.Count() > MaxLogLines)
				{
					MB.Paragraph(string.Format("(First {0} of {1} errors)", MaxLogLines, LogSummary.Errors.Count()));
				}
			}

			if (GetCachedConfiguration().ShowWarningsInSummary && InArtifacts.LogSummary.Warnings.Count() > 0)
			{
				MB.H4("Warnings");
				MB.UnorderedList(LogSummary.Warnings.Take(MaxLogLines));

				if (LogSummary.Warnings.Count() > MaxLogLines)
				{
					MB.Paragraph(string.Format("(First {0} of {1} warnings)", MaxLogLines, LogSummary.Warnings.Count()));
				}
			}

			MB.H4("Artifacts");
			MB.Paragraph(string.Format("Log: {0}", InArtifacts.LogPath));
			MB.Paragraph(string.Format("Commandline: {0}", InArtifacts.AppInstance.CommandLine));
			MB.Paragraph(InArtifacts.ArtifactPath);

			Summary = MB.ToString();
			return ExitCode;
		}

		/// <summary>
		/// Returns the current Pass count for this node
		/// </summary>
		public int GetCurrentPass()
		{
			return CurrentPass;
		}

		/// <summary>
		/// Returns the current total Pass count for this node
		/// </summary>
		/// <returns></returns>
		public int GetNumPasses()
		{
			return NumPasses;
		}

		/// <summary>
		/// Result of the test once completed. Nodes inheriting from us should override
		/// this if custom results are necessary
		/// </summary>
		public sealed override TestResult GetTestResult()
		{
			if (UnrealTestResult == TestResult.Invalid)
			{
				UnrealTestResult = GetUnrealTestResult();
			}

			return UnrealTestResult;
		}

		/// <summary>
		/// Allows tests to set this at anytime. If not called then GetUnrealTestResult() will be called when
		/// the framework first calls GetTestResult()
		/// </summary>
		/// <param name="Result"></param>
		/// <returns></returns>
		protected void SetUnrealTestResult(TestResult Result)
		{
			if (GetTestStatus() != TestStatus.Complete)
			{
				throw new Exception("SetUnrealTestResult() called while test is incomplete!");
			}

			UnrealTestResult = Result;
		}

		/// <summary>
		/// Return all artifacts that are considered to have caused the test to fail
		/// </summary>
		/// <returns></returns>
		protected virtual IEnumerable<UnrealRoleArtifacts> GetArtifactsWithFailures()
		{
			if (SessionArtifacts == null)
			{
				Log.Warning("SessionArtifacts was null, unable to check for failures");
				return new UnrealRoleArtifacts[0] { };
			}

			bool DidKillClients = SessionArtifacts.Any(A => A.SessionRole.RoleType.IsClient() && A.AppInstance.WasKilled);

			Dictionary<UnrealRoleArtifacts, int> ErrorCodes = new Dictionary<UnrealRoleArtifacts, int>();

			var FailureList = SessionArtifacts.Where(A =>
			{
				// ignore anything we killed
				if (A.AppInstance.WasKilled)
				{
					return false;
				}

				string ExitReason;
				int ExitCode = GetExitCodeAndReason(A, out ExitReason);

				ErrorCodes.Add(A, ExitCode);

				return ExitCode != 0;
			});

			return FailureList.OrderByDescending(A =>
			{
				int Score = 0;

				if (A.LogSummary.FatalError != null || (ErrorCodes[A] != 0 && A.AppInstance.WasKilled == false))
				{
					Score += 100000;
				}

				Score += A.LogSummary.Ensures.Count();
				return Score;
			}).ToList();
		}

		/// <summary>
		/// THe base implementation considers  considers Classes can override this to implement more custom detection of success/failure than our
		/// log parsing. Not guaranteed to be called if a test is marked complete
		/// </summary>
		/// <returns></returns>in
		protected virtual TestResult GetUnrealTestResult()
		{
			int ExitCode = 0;

			// Let the test try and diagnose things as best it can
			var ProblemArtifact = GetArtifactsWithFailures().FirstOrDefault();

			if (ProblemArtifact != null)
			{
				string ExitReason;

				ExitCode = GetExitCodeAndReason(ProblemArtifact, out ExitReason);
				Log.Info("{0} exited with {1}. ({2})", ProblemArtifact.SessionRole, ExitCode, ExitReason);
			}

			// If it didn't find an error, overrule it as a failure if the test was cancelled
			if (ExitCode == 0 && WasCancelled)
			{
				return TestResult.Failed;
			}

			return ExitCode == 0 ? TestResult.Passed : TestResult.Failed;
		}

		/// <summary>
		/// Return the header for the test summary. The header is the first block of text and will be
		/// followed by the summary of each individual role in the test
		/// </summary>
		/// <returns></returns>
		protected virtual string GetTestSummaryHeader()
		{
			int AbnormalExits = 0;
			int FatalErrors = 0;
			int Ensures = 0;
			int Errors = 0;
			int Warnings = 0;

			// create a quicck summary of total failures, ensures, errors, etc
			foreach (var Artifact in SessionArtifacts)
			{
				string Summary;
				int ExitCode = GetRoleSummary(Artifact, out Summary);

				if (ExitCode != 0 && Artifact.AppInstance.WasKilled == false)
				{
					AbnormalExits++;
				}

				FatalErrors += Artifact.LogSummary.FatalError != null ? 1 : 0;
				Ensures += Artifact.LogSummary.Ensures.Count();
				Errors += Artifact.LogSummary.Errors.Count();
				Warnings += Artifact.LogSummary.Warnings.Count();
			}

			MarkdownBuilder MB = new MarkdownBuilder();

			string WarningStatement = HasWarnings ? " With Warnings" : "";

			var FailureArtifacts = GetArtifactsWithFailures();

			// Create a summary
			MB.H2(string.Format("{0} {1}{2}", Name, GetTestResult(), WarningStatement));

			if (GetTestResult() != TestResult.Passed)
			{
				if (FailureArtifacts.Count() > 0)
				{
					foreach (var FailedArtifact in FailureArtifacts)
					{
						string FirstProcessCause = "";
						int FirstExitCode = GetExitCodeAndReason(FailedArtifact, out FirstProcessCause);
						MB.H3(string.Format("{0}: {1}", FailedArtifact.SessionRole.RoleType, FirstProcessCause));

						if (FailedArtifact.LogSummary.FatalError != null)
						{
							MB.H4(FailedArtifact.LogSummary.FatalError.Message);
						}
					}

					MB.Paragraph("See below for logs and any callstacks");
				}
			}
			MB.Paragraph(string.Format("Context: {0}", Context.ToString()));
			MB.Paragraph(string.Format("FatalErrors: {0}, Ensures: {1}, Errors: {2}, Warnings: {3}", FatalErrors, Ensures, Errors, Warnings));
			MB.Paragraph(string.Format("Result: {0}, ResultHash: {1}", GetTestResult(), GetTestResultHash()));
			//MB.Paragraph(string.Format("Artifacts: {0}", CachedArtifactPath));

			return MB.ToString();
		}

		/// <summary>
		/// Returns a summary of this test
		/// </summary>
		/// <returns></returns>
		public override string GetTestSummary()
		{
			// Handle case where there aren't any session artifacts, for example with device starvation
			if (SessionArtifacts == null)
			{
				return "NoSummary";
			}

			MarkdownBuilder ReportBuilder = new MarkdownBuilder();

			// add header
			ReportBuilder.Append(GetTestSummaryHeader());
			ReportBuilder.Append("--------");

			StringBuilder SB = new StringBuilder();

			// Get any artifacts with failures
			var FailureArtifacts = GetArtifactsWithFailures();

			// Any with warnings (ensures)
			var WarningArtifacts = SessionArtifacts.Where(A => A.LogSummary.Ensures.Count() > 0);

			// combine artifacts into order as Failures, Warnings, Other
			var AllArtifacts = FailureArtifacts.Union(WarningArtifacts);
			AllArtifacts = AllArtifacts.Union(SessionArtifacts);

			// Add a summary of each 
			foreach ( var Artifact in AllArtifacts)
			{
				string Summary = "NoSummary";
				int ExitCode = GetRoleSummary(Artifact, out Summary);

				if (SB.Length > 0)
				{
					SB.AppendLine();
				}
				SB.Append(Summary);
			}

			ReportBuilder.Append(SB.ToString());

			return ReportBuilder.ToString();
		}
	}
}