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
	public class GauntletParamDescription
	{
		/// <summary>
		/// The param name that is passed in on the commandline
		/// In -PARAMNAME or -PARAMNAME=val format
		/// </summary>
		public string ParamName;

		/// <summary>
		/// Is this required for the test to run?
		/// </summary>
		public bool Required;

		/// <summary>		
		/// Very brief desc of what to pass in -
		/// Will show up in -param=<InputFormat> format.
		/// Ex - a value of "Map To Use" would show up as -param=<Map To use>
		/// Leave blank for a param that is just a flag.
		/// </summary>
		public string InputFormat;

		/// <summary>
		/// Helpful description for what this Parameter or flag represents and what can be passed in.
		/// </summary>
		public string ParamDesc;

		/// <summary>
		/// If you would like to provide a sample input for this field, do so here. Will show up as (ex: SampleInput) at the end of the param description
		/// </summary>
		public string SampleInput;

		/// <summary>
		///  If this param has a default value, put it here. Will show ups as (default: DefaultValue)
		/// </summary>
		public string DefaultValue;

		/// <summary>
		/// Whether this is a Test-specific param or a generic gauntlet param.
		/// </summary>
		public bool TestSpecificParam;

		public GauntletParamDescription()
		{
			TestSpecificParam = true;
		}

		public override string ToString()
		{
			string ParamFormat = ParamName;
			if (!string.IsNullOrEmpty(InputFormat))
			{
				ParamFormat += "=" + InputFormat;
			}
			string DefaultFormat = "";
			if (!string.IsNullOrEmpty(DefaultValue))
			{
				DefaultFormat = string.Format("(default: {0}) ", DefaultValue);
			}
			string SampleFormat = "";
			if (!string.IsNullOrEmpty(SampleInput))
			{
				SampleFormat = string.Format(" (ex: {0})", SampleInput);
			}
			ParamFormat = string.Format("{0}:\t\t{1}{2}{3}{4}", ParamFormat, Required ? "*Required* " : "", DefaultFormat, ParamDesc, SampleInput);
			return ParamFormat;
		}
	}
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


		/// <summary>
		/// Returns Errors found during tests. Including Abnornal Exit reasons
		/// </summary>
		public virtual IEnumerable<string> GetErrorsAndAbnornalExits()
		{
			IEnumerable<string> Errors = GetErrors();

			Dictionary<UnrealRoleArtifacts, Tuple<int, string>> ErrorCodesAndReasons = new Dictionary<UnrealRoleArtifacts, Tuple<int, string>>();

			var FailedArtifacts = SessionArtifacts.Where(
				A => {
					if (A.AppInstance.WasKilled)
					{
						return false;
					}
					string ExitReason;
					int ExitCode = GetExitCodeAndReason(A, out ExitReason);
					ErrorCodesAndReasons.Add(A, new Tuple<int, string>(ExitCode, ExitReason));
					return ExitCode != 0;
				}
			);

			return Errors.Union(FailedArtifacts.Select(
				A => {
					int ExitCode = ErrorCodesAndReasons[A].Item1;
					string ExitReason = ErrorCodesAndReasons[A].Item2;
					return string.Format("Abnormal Exit: Reason={0}, ExitCode={1}, Log={2}", ExitReason, ExitCode, Path.GetFileName(A.LogPath));
				}
			));
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

		/// <summary>
		/// Help doc-style list of parameters supported by this test. List can be divided into test-specific and general arguments.
		/// </summary>
		public List<GauntletParamDescription> SupportedParameters = new List<GauntletParamDescription>();

		/// <summary>
		/// Optional list of provided commandlines to be displayed to users who want to look at test help docs.
		/// Key should be the commandline to use, value should be the description for that commandline.
		/// </summary>
		protected List<KeyValuePair<string, string>> SampleCommandlines = new List<KeyValuePair<string, string>>();

		public void AddSampleCommandline(string Commandline, string Description)
		{
			SampleCommandlines.Add(new KeyValuePair<string, string>(Commandline, Description));
		}
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
			PopulateCommandlineInfo();
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
		private TConfigClass GetCachedConfiguration()
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

					SessionRole.CommandLineParams = TestRole.CommandLineParams;
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
						SessionRole.CommandLine += RoleContext.ExtraArgs;

						// did the test ask for anything?
						if (string.IsNullOrEmpty(TestRole.CommandLine) == false)
						{
							SessionRole.CommandLine += TestRole.CommandLine;
						}

						// add controllers
						SessionRole.CommandLineParams.Add("gauntlet", 
							TestRole.Controllers.Count > 0 ? string.Join(",", TestRole.Controllers) : null);				

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

			// We need to create this directory at the start of the test rather than the end of the test - we are running into instances where multiple A/B tests
			// on the same build are seeing the directory as non-existent and thinking it is safe to write to.
			Directory.CreateDirectory(ArtifactPath);

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
				MaxDurationReachedResult = Config.MaxDurationReachedResult;
				UnrealTestResult = TestResult.Invalid;
				MarkTestStarted();
			}
			
			return TestInstance != null;
		}

		public virtual void PopulateCommandlineInfo()
		{
			SupportedParameters.Add(new GauntletParamDescription()
			{
				ParamName = "nomcp",
				TestSpecificParam = false,
				ParamDesc = "Run test without an mcp backend",
				DefaultValue = "false"
			});
			SupportedParameters.Add(new GauntletParamDescription()
			{
				ParamName = "ResX",
				InputFormat = "1280",
				Required = false,
				TestSpecificParam = false,
				ParamDesc = "Horizontal resolution for the game client.",
				DefaultValue = "1280"
			});
			SupportedParameters.Add(new GauntletParamDescription()
			{
				ParamName = "ResY",
				InputFormat = "720",
				Required = false,
				TestSpecificParam = false,
				ParamDesc = "Vertical resolution for the game client.",
				DefaultValue = "720"
			});
			SupportedParameters.Add(new GauntletParamDescription()
			{
				ParamName = "FailOnEnsure",
				Required = false,
				TestSpecificParam = false,
				ParamDesc = "Consider the test a fail if we encounter ensures."
			});
			SupportedParameters.Add(new GauntletParamDescription()
			{
				ParamName = "MaxDuration",
				InputFormat = "600",
				Required = false,
				TestSpecificParam = false,
				ParamDesc = "Time in seconds for test to run before it is timed out",
				DefaultValue = "Test-defined"
			});
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
				// run create dir again just in case the already made dir was cleaned up by another buildfarm job or something similar.
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
		/// Display all of the defined commandline information for this test.
		/// Will display generic gauntlet params as well if -help=full is passed in.
		/// </summary>
		public override void DisplayCommandlineHelp()
		{
			Log.Info(string.Format("--- Available Commandline Parameters for {0} ---", Name));
			Log.Info("--------------------");
			Log.Info("TEST-SPECIFIC PARAMS");
			Log.Info("--------------------");
			foreach (GauntletParamDescription ParamDesc in SupportedParameters)
			{
				if (ParamDesc.TestSpecificParam)
				{
					Log.Info(ParamDesc.ToString());
				}
			}
			if (Context.TestParams.ParseParam("listallargs"))
			{
				Log.Info("\n--------------");
				Log.Info("GENERIC PARAMS");
				Log.Info("--------------");
				foreach (GauntletParamDescription ParamDesc in SupportedParameters)
				{
					if (!ParamDesc.TestSpecificParam)
					{
						Log.Info(ParamDesc.ToString());
					}
				}
			}
			else
			{
				Log.Info("\nIf you need information on base Gauntlet arguments, use -listallargs\n\n");
			}
			if (SampleCommandlines.Count > 0)
			{
				Log.Info("\n-------------------");
				Log.Info("SAMPLE COMMANDLINES");
				Log.Info("-------------------");
				foreach (KeyValuePair<string, string> SampleCommandline in SampleCommandlines)
				{
					Log.Info(SampleCommandline.Key);
					Log.Info("");
					Log.Info(SampleCommandline.Value);
					Log.Info("-------------------");
				}
			}

		}

		/// <summary>
		/// Optional function that is called on test completion and gives an opportunity to create a report
		/// </summary>
		/// <param name="Result"></param>
		/// <param name="Context"></param>
		/// <param name="Build"></param>
		public virtual void CreateReport(TestResult Result, UnrealTestContext Context, UnrealBuildSource Build, IEnumerable<UnrealRoleArtifacts> Artifacts, string ArtifactPath)
		{
		}

		/// <summary>
		/// Generate a Simple Test Report from the results of this test
		/// </summary>
		/// <param name="Result"></param>
		protected virtual HordeReport.SimpleTestReport CreateSimpleReportForHorde(TestResult Result)
		{
			HordeReport.SimpleTestReport HordeTestReport = new HordeReport.SimpleTestReport();
			HordeTestReport.ReportCreatedOn = DateTime.Now.ToString();
			HordeTestReport.TotalDurationSeconds = (float)(DateTime.Now - SessionStartTime).TotalSeconds;
			HordeTestReport.Description = Context.ToString();
			HordeTestReport.Status = Result.ToString();
			HordeTestReport.Errors.AddRange(GetErrorsAndAbnornalExits());
			HordeTestReport.Warnings.AddRange(GetWarnings());
			HordeTestReport.HasSucceeded = !(Result == TestResult.Failed || Result == TestResult.TimedOut || HordeTestReport.Errors.Count > 0);
			string HordeArtifactPath = string.IsNullOrEmpty(GetConfiguration().HordeArtifactPath) ? HordeReport.DefaultArtifactsDir : GetConfiguration().HordeArtifactPath;
			HordeTestReport.SetOutputArtifactPath(HordeArtifactPath);
			if (SessionArtifacts != null)
			{
				foreach (UnrealRoleArtifacts Artifact in SessionArtifacts)
				{
					string LogName = Path.GetFullPath(Artifact.LogPath).Replace(Path.GetFullPath(Path.Combine(ArtifactPath, "..")), "").TrimStart(Path.DirectorySeparatorChar);
					HordeTestReport.AttachArtifact(Artifact.LogPath, LogName);

					UnrealLogParser.LogSummary LogSummary = Artifact.LogSummary;
					if (LogSummary.Errors.Count() > 0)
					{
						HordeTestReport.Warnings.Add(
							string.Format(
								"Log Parsing: FatalErrors={0}, Ensures={1}, Errors={2}, Warnings={3}, Log={4}",
								(LogSummary.FatalError != null ? 1 : 0), LogSummary.Ensures.Count(), LogSummary.Errors.Count(), LogSummary.Warnings.Count(), LogName
							)
						);
					}
				}
			}
			return HordeTestReport;
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
				ExitCode = 0;
			}
			else if (LogSummary.HasTestExitCode)
			{
				if (LogSummary.TestExitCode == 0)
				{
					ExitReason = "Tests exited with code 0";
				}
				else
				{
					ExitReason = string.Format("Tests exited with error code {0}", LogSummary.TestExitCode);
				}

				// tests failed but the process didn't
				ExitCode = 0;
			}
			else if (LogSummary.EngineInitialized == false)
			{
				ExitReason = string.Format("Engine initialization failed");
				ExitCode = -1;
			}
			else if (LogSummary.RequestedExit)
			{
				ExitReason = string.Format("Exit was requested: {0}", LogSummary.RequestedExitReason);
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
						ExitReason = "Process has terminated prematurely! No exit code from Gauntlet controller.";
					}
				}
				else
				{
					// if all else fails, fall back to the exit code from the process. Not great.
					ExitCode = InArtifacts.AppInstance.ExitCode;
					if (ExitCode == 0)
					{
						ExitReason = "Process exited with code 0";
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

			MB.HorizontalLine();
			MB.H3(string.Format("Role: {0} ({1} {2})", InArtifacts.SessionRole.RoleType, InArtifacts.SessionRole.Platform, InArtifacts.SessionRole.Configuration));
		
			MB.Paragraph(string.Format("Result: {0} (Code={1})", ExitReason, ExitCode));

			MB.UnorderedList(new string[] {
				LogSummary.FatalError != null ? "Fatal Errors: 1" : null,
				LogSummary.Ensures.Count() > 0 ? string.Format("Ensures: {0}", LogSummary.Ensures.Count()) : null,
				LogSummary.Errors.Count() > 0 ? string.Format("Log Errors: {0}", LogSummary.Errors.Count()) : null,
				LogSummary.Warnings.Count() > 0 ? string.Format("Log Warnings: {0}", LogSummary.Warnings.Count()) : null,
			});

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

			// Show warnings if that option is set, or the process exited abnormally
			bool ShouldShowErrors = GetCachedConfiguration().ShowErrorsInSummary || (InArtifacts.AppInstance.WasKilled == false && InArtifacts.LogSummary.HasAbnormalExit);
			bool ShouldShowWarnings = GetCachedConfiguration().ShowWarningsInSummary || (InArtifacts.AppInstance.WasKilled == false && InArtifacts.LogSummary.HasAbnormalExit);

			if (ShouldShowErrors)
			{
				if (InArtifacts.LogSummary.Errors.Count() > 0)
				{
					IEnumerable<string> Errors = LogSummary.Errors.Distinct();

					string TrimStatement = "";

					if (Errors.Count() > MaxLogLines)
					{
						// too many errors. If there was an abnormal exit show the last ones as they may be relevant
						if (LogSummary.HasAbnormalExit)
						{
							Errors = Errors.Skip(Errors.Count() - MaxLogLines);
							TrimStatement = string.Format("(Last {0} of {1} errors)", MaxLogLines, LogSummary.Errors.Count());
						}
						else
						{
							Errors = Errors.Take(MaxLogLines);
							TrimStatement = string.Format("(First {0} of {1} errors)", MaxLogLines, LogSummary.Errors.Count());
						}
					}

					MB.H4("Errors");
					MB.UnorderedList(Errors);

					if (!string.IsNullOrEmpty(TrimStatement))
					{
						MB.Paragraph(TrimStatement);
					}
				}
			}

			if (ShouldShowWarnings)
			{
				if (InArtifacts.LogSummary.Warnings.Count() > 0)
				{
					IEnumerable<string> Warnings = LogSummary.Warnings.Distinct();

					string TrimStatement = "";

					if (Warnings.Count() > MaxLogLines)
					{
						// too many warnings. If there was an abnormal exit show the last ones as they may be relevant
						if (LogSummary.HasAbnormalExit)
						{
							Warnings = Warnings.Skip(Warnings.Count() - MaxLogLines);
							TrimStatement = string.Format("(Last {0} of {1} warnings)", MaxLogLines, LogSummary.Warnings.Count());
						}
						else
						{
							Warnings = Warnings.Take(MaxLogLines);
							TrimStatement = string.Format("(First {0} of {1} warnings)", MaxLogLines, LogSummary.Warnings.Count());
						}
					}

					MB.H4("Warnings");
					MB.UnorderedList(Warnings);

					if (!string.IsNullOrEmpty(TrimStatement))
					{
						MB.Paragraph(TrimStatement);
					}
				}
			}

			MB.H4("Artifacts");
			string[] ArtifactList = new string[]
			{
				string.Format("Log: {0}", InArtifacts.LogPath),
				string.Format("SavedDir: {0}", InArtifacts.ArtifactPath),
				string.Format("Commandline: {0}", InArtifacts.AppInstance.CommandLine),
			};
			MB.UnorderedList(ArtifactList);
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
		/// Return all artifacts that are exited abnormally. An abnormal exit is termed as a fatal error,
		/// crash, assert, or other exit that does not appear to have been caused by completion of a process
		/// </summary>
		/// <returns></returns>
		protected virtual IEnumerable<UnrealRoleArtifacts> GetArtifactsThatExitedAbnormally()
		{
			if (SessionArtifacts == null)
			{
				Log.Warning("SessionArtifacts was null, unable to check for failures");
				return Enumerable.Empty<UnrealRoleArtifacts>();
			}

			return SessionArtifacts.Where(A => A.AppInstance.WasKilled == false && A.LogSummary.HasAbnormalExit);
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

			// Put less suspect issues at the top since the user is likely going to stare at the last lines of the log and read up
			return FailureList.OrderBy(A =>
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

			MarkdownBuilder MB = new MarkdownBuilder();
			
			if (GetTestResult() != TestResult.Passed)
			{
				// If the test didn't pass then show a brief summary of any roles that had an abnormal exit, or failing that
				// are reported as having a failure. Tests should overload GetArtifactsWithFailures if necessary
				IEnumerable<UnrealRoleArtifacts> RolesCausingFailure = GetArtifactsThatExitedAbnormally();
				bool HadAbnormalExit = RolesCausingFailure.Any();

				if (!HadAbnormalExit)
				{
					RolesCausingFailure = GetArtifactsWithFailures();
				}

				if (RolesCausingFailure.Any())
				{
					MB.Paragraph(string.Format("{0} failed", this.Name));

					List<string> RoleItems = new List<string>();

					foreach (var Artifact in RolesCausingFailure)
					{
						string ProcessCause = "";
						int ExitCode = GetExitCodeAndReason(Artifact, out ProcessCause);
						MB.H3(Artifact.SessionRole.RoleType.ToString());

						if (Artifact.LogSummary.FatalError != null)
						{
							MB.Paragraph(Artifact.LogSummary.FatalError.Message);
						}

						MB.Paragraph(string.Format("\tResult: {0} (ExitCode {1})", ProcessCause, ExitCode));

						//RoleItems.Add(string.Format("{0}: {1}", Artifact.SessionRole.RoleType, ))
					}

					MB.Paragraph(string.Format("See Role {0} above for logs and any callstacks", RolesCausingFailure.First().SessionRole.ToString()));
				}
				else
				{
					MB.Paragraph(string.Format("{0} failed due to undiagnosed reasons", this.Name));
					MB.Paragraph("See above for logs and any callstacks");
				}
			}
			else
			{
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

				MB.UnorderedList(new string[] {
					string.Format("Context: {0}", Context.ToString()),
					FatalErrors > 0 ? string.Format("FatalErrors: {0}", FatalErrors) : null,
					Ensures > 0 ? string.Format("Ensures: {0}", Ensures) : null,
					Errors > 0 ? string.Format("Log Errors: {0}", Errors) : null,
					Warnings > 0 ? string.Format("Log Warnings: {0}", Warnings) : null,
					string.Format("Result: {0}", GetTestResult())
				});

				// Create a summary
				string WarningStatement = HasWarnings ? " With Warnings" : "";
				MB.H3(string.Format("{0} {1}{2}", Name, GetTestResult(), WarningStatement));
			}

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

			StringBuilder SB = new StringBuilder();

			// Get any artifacts with failures
			var FailureArtifacts = GetArtifactsWithFailures();

			// Any with warnings (ensures)
			var WarningArtifacts = SessionArtifacts.Where(A => A.LogSummary.Ensures.Count() > 0);

			// combine artifacts into order as Failures, Warnings, Other
			var AllArtifacts = FailureArtifacts.Union(WarningArtifacts);
			AllArtifacts = AllArtifacts.Union(SessionArtifacts);

			ReportBuilder.H1(string.Format("{0} Report", this.Name));
			ReportBuilder.HorizontalLine();

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

			// add Summary
			ReportBuilder.HorizontalLine();
			ReportBuilder.H2("Summary");
			ReportBuilder.Append(GetTestSummaryHeader());

			return ReportBuilder.ToString();
		}
	}
}