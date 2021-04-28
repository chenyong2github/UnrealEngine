// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using Gauntlet;
using Gauntlet.UnrealTest;


namespace UnrealEditor
{
	/// <summary>
	/// Default set of options for testing Editor. Options that tests can configure
	/// should be public, external command-line driven options should be protected/private
	/// </summary>
	public class EditorTestConfig : EngineTestConfig
	{
		/// <summary>
		/// Filter or groups of tests to apply
		/// </summary>
		public override bool UseEditor { get; set; } = true;

		/// <summary>
		/// Use Simple Horde Report instead of Unreal Automated Tests
		/// </summary>
		public override bool SimpleHordeReport { get; set; } = true;

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
		/// Modify the game instance lost timeout interval
		/// </summary>
		[AutoParam]
		public string GameInstanceLostTimerSeconds = string.Empty;

		/// <summary>
		/// Disable distribution of shader builds (but use worker processes still)
		/// </summary>
		[AutoParam]
		public bool NoShaderDistrib = false;

		/// <summary>
		/// Applies these options to the provided app config
		/// </summary>
		/// <param name="AppConfig"></param>
		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

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

			if (TreatLogWarningsAsTestErrors.ToLower() == "false" || TreatLogWarningsAsTestErrors == "0")
			{
				AppConfig.CommandLineParams.Add("ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:bTreatLogWarningsAsTestErrors=false");
			}

			if (TreatLogErrorsAsTestErrors.ToLower() == "false" || TreatLogErrorsAsTestErrors == "0")
			{
				AppConfig.CommandLineParams.Add("ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:bTreatLogErrorsAsTestErrors=false");
			}

			if (GameInstanceLostTimerSeconds != string.Empty)
			{
				AppConfig.CommandLineParams.Add(string.Format("ini:Engine:[/Script/AutomationController.AutomationControllerSettings]:GameInstanceLostTimerSeconds={0}", GameInstanceLostTimerSeconds));
			}

			if (NoShaderDistrib)
			{
				AppConfig.CommandLineParams.Add("-noxgeshadercompile");
			}
		}
	}

	public class EditorTests : EngineTestBase<EditorTestConfig>
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
	}
}
