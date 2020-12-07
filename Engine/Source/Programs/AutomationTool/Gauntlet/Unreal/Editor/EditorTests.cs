using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Gauntlet;

namespace UnrealEditor
{
	/// <summary>
	/// Default set of options for testing Editor. Options that tests can configure
	/// should be public, external command-line driven options should be protected/private
	/// </summary>
	public class EditorTestConfig : UnrealTestConfiguration
	{
		/// <summary>
		/// Filter or groups of tests to apply
		/// </summary>
		[AutoParam]
		public string TestFilter = "Editor";

		/// <summary>
		/// Force some specific plugins to load, comma delimited (at time of writing)
		/// </summary>
		[AutoParam]
		public string EnablePlugins = string.Empty;

		/// <summary>
		/// The file to trace profile data to
		/// </summary>
		[AutoParam]
		public string TraceFile = string.Empty;

		/// <summary>
		/// Control for interpretation of log warnings as test failures
		/// </summary>
		[AutoParam]
		public string TreatLogWarningsAsTestErrors = "true";

		/// <summary>
		/// Control for interpretation of log errors as test failures
		/// </summary>
		[AutoParam]
		public string TreatLogErrorsAsTestErrors = "true";

		/// <summary>
		/// Applies these options to the provided app config
		/// </summary>
		/// <param name="AppConfig"></param>
		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

			if (AppConfig.ProcessType.IsEditor())
			{
				AppConfig.CommandLineParams.Add("ExecCmds", string.Format("\"Automation RunTests {0}; Quit;\"", TestFilter));
			}

			if (EnablePlugins != string.Empty)
			{
				AppConfig.CommandLineParams.Add(string.Format("EnablePlugins={0}", EnablePlugins));
			}

			if (TraceFile != string.Empty)
			{
				AppConfig.CommandLineParams.Add(string.Format("tracefile={0}", TraceFile));
				AppConfig.CommandLineParams.Add("tracefiletrunc"); // replace existing
				AppConfig.CommandLineParams.Add("trace=cpu");
				AppConfig.CommandLineParams.Add("statnamedevents");
			}

			if (TreatLogWarningsAsTestErrors != "true" || TreatLogWarningsAsTestErrors != "1")
			{
				AppConfig.CommandLineParams.Add("ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:bTreatLogWarningsAsTestErrors=False");
			}

			if (TreatLogErrorsAsTestErrors != "true" || TreatLogErrorsAsTestErrors != "1")
			{
				AppConfig.CommandLineParams.Add("ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:bTreatLogErrorsAsTestErrors=False");
			}
		}
	}

	public class EditorTests : UnrealTestNode<EditorTestConfig>
	{
		public EditorTests(UnrealTestContext InContext) : base(InContext)
		{
		}

		public override EditorTestConfig GetConfiguration()
		{
			// just need a single client
			EditorTestConfig Config = base.GetConfiguration();
			Config.RequireRole(UnrealTargetRole.Editor);	
			return Config;
		}

		public override ITestReport CreateReport(TestResult Result)
		{
			ITestReport Report = base.CreateReport(Result);

			if (Report != null && CachedConfig.TraceFile != string.Empty)
			{
				string TraceFilePath = System.IO.Path.GetFullPath( CachedConfig.TraceFile );
				Log.Info("Attaching trace {0} to report", TraceFilePath);
				Report.AttachArtifact(TraceFilePath);
			}

			return Report;
		}
	}}
