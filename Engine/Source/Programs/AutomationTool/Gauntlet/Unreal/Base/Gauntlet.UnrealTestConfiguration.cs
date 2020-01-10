// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using AutomationTool;
using UnrealBuildTool;
using System.Linq;

namespace Gauntlet
{

    public enum EWindowMode
    {
        /// <summary>
		/// The window is in true fullscreen mode.
        /// </summary>
        Fullscreen,
        /// <summary>
		/// CURRENTLY UNSUPPORTED. Using this value will enable -fullscreen for now. The window has no border and takes up the entire area of the screen.
        /// </summary>
        WindowedFullscreen,
        /// <summary>
		/// The window has a border and may not take up the entire screen area.
        /// </summary>
        Windowed,
        /// <summary>
		/// The total number of supported window modes.
        /// </summary>
        NumWindowModes
    };

    /// <summary>
    /// Generic intent enum for the base of where we would like to copy a file to.
    /// Interpreted properly in TargetDeviceX.cs.
    /// </summary>
    public enum EIntendedBaseCopyDirectory
    {
        Build,
		Binaries,
        Config,
        Content,
        Demos,
        Profiling,
        Saved
    }

	/// <summary>
	/// Delegate for role device configuration
	/// </summary>
	public delegate void ConfigureDeviceHandler(ITargetDevice Device);

	/// <summary>
	/// This class represents a process-role in a test and defines the type, command line,
	/// and controllers that are needed.
	/// 
	/// TODO - can this be removed and UnrealSessionRole used directly?
	/// 
	/// </summary>
	public class UnrealTestRole
	{
		/// <summary>
		/// Constructor. This intentionally takes only a type as it's expected that code creating roles should do so via
		/// the configuration class and take care to append properties.
		/// </summary>
		/// <param name="InType"></param>
		public UnrealTestRole(UnrealTargetRole InType, UnrealTargetPlatform? InPlatformOverride)
		{
			Type = InType;
            PlatformOverride = InPlatformOverride;
			CommandLine = string.Empty;
			MapOverride = string.Empty;
			ExplicitClientCommandLine = string.Empty;
			Controllers = new List<string>();
            FilesToCopy = new List<UnrealFileToCopy>();
			AdditionalArtifactDirectories = new List<EIntendedBaseCopyDirectory>();
            RoleType = ERoleModifier.None;
		}

        public ERoleModifier RoleType { get; set; }
		
		/// <summary>
		/// Type of process this role represents
		/// </summary>
		public UnrealTargetRole Type { get; protected set; }

		/// <summary>
		/// Override for what platform this role is on
		/// </summary>
		public UnrealTargetPlatform? PlatformOverride { get; protected set; }

		/// <summary>
		/// Command line or this role
		/// </summary>
		public string CommandLine { get; set; }

		/// <summary>
		/// Controllers for this role
		/// </summary>
		public List<string> Controllers { get; set; }

		/// <summary>
		/// Explicit command line for this role. If this is set no other
		/// options from above or configs will be applied!
		/// </summary>
		public string ExplicitClientCommandLine { get; set; }

        public List<UnrealFileToCopy> FilesToCopy { get; set; }

		/// <summary>
		/// Additional directories to 
		/// </summary>
		public List<EIntendedBaseCopyDirectory> AdditionalArtifactDirectories { get; set; }

		/// <summary>
		/// A map value passed in per server in case a test needs multiple servers on different maps.
		/// </summary>
		public string MapOverride { get; set; }

		/// <summary>
		/// Role device configuration 
		/// </summary>
		public ConfigureDeviceHandler ConfigureDevice;

	}

	/// <summary>
	/// Collection of parameters that control how heartbeats coming from the native gauntlet controller for this role should be handled.
	/// To make best use of this, your GauntletTestController should regularly call MarkHeartbeatActive().
	/// Set bExpectHeartbeats to true to enable killing the App Instance when expected heartbeats are not detected.
	/// </summary>
	public class UnrealHeartbeatOptions
	{
		/// <summary>
		/// The amount of time between regular heartbeats. This value is passed along through the command line.
		/// </summary>
		public float HeartbeatPeriod;

		/// <summary>
		/// Set to true to allow the App Instance to be killed when expected heartbeats are not detected. If left false, heartbeat timeouts will not result in any action or timeouts.
		/// </summary>
		public bool bExpectHeartbeats;
		
		/// <summary>
		/// The max amount of time allowed before the first "active" heartbeat is detected
		/// </summary>
		public float TimeoutBeforeFirstActiveHeartbeat;

		/// <summary>
		/// The max amount of time allowed between "active" heartbeats
		/// </summary>
		public float TimeoutBetweenActiveHeartbeats;

		/// <summary>
		/// The max amount of time allowed between any heartbeats, active or not
		/// </summary>
		public float TimeoutBetweenAnyHeartbeats;

		public UnrealHeartbeatOptions(float InHeartbeatPeriod = 30f, bool bShouldExpectHeartbeats = false, float InTimeoutBeforeFirstActiveHeartbeat = 0f, float InTimeoutBetweenActiveHeartbeats = 0f, float InTimeoutBetweenAnyHeartbeats = 90f)
		{
			HeartbeatPeriod = InHeartbeatPeriod;
			bExpectHeartbeats = bShouldExpectHeartbeats;
			TimeoutBeforeFirstActiveHeartbeat = InTimeoutBeforeFirstActiveHeartbeat;
			TimeoutBetweenActiveHeartbeats = InTimeoutBetweenActiveHeartbeats;
			TimeoutBetweenAnyHeartbeats = InTimeoutBetweenAnyHeartbeats;
		}

	}

	/// <summary>
	///	TestConfiguration describes the setup that is required for a specific test. 
	///	
	/// Protected parameters are generally test-wide options that are read from the command line and which tests cannot
	/// control.
	/// 
	/// Public parameters are options that individual tests can configure as appropriate.
	/// 
	///	Each test can (and should) supply its own configuration by overriding TestNode.GetConfiguration. At a minimum a 
	///	test must add one or more roles and the command line or controller necessary to execute the tests.
	///	
	/// Inherited classes should implement ApplyToConfig to apply the options they expose, and should ball the base class
	/// implementation.
	///
	/// </summary>
	public class UnrealTestConfiguration : IConfigOption<UnrealAppConfig>
	{

		// Protected options that are driven from the command line

		/// <summary>
		/// How often to grab a screenshot
		/// </summary>
		/// 
		[AutoParam(0)]
		protected int ScreenshotPeriod { get; set; }

		/// <summary>
		/// Use a nullrhi for tests
		/// </summary>
		/// 
		[AutoParam(false)]
		protected bool Nullrhi { get; set; }

        // Public options that tests can configure

        /// <summary>
        /// If true, explicitly do not set the default resolution of 1280x720 or the window mode. Most tests should not do this.
        /// </summary>
        /// 
        [AutoParam(false)]
        public bool IgnoreDefaultResolutionAndWindowMode { get; set; }

        /// <summary>
        /// The width resolution. Default resolution is 1280x720.
        /// </summary>
        /// 
        [AutoParam(1280)]
        public int ResX { get; set; }

        /// <summary>
        /// The height resolution. Default resolution is 1280x720.
        /// </summary>
        /// 
        [AutoParam(720)]
        public int ResY { get; set; }

		/// <summary>
		/// Set to Windowed mode (same as -WindowMode=Windowed);
		/// </summary>
		/// 
		[AutoParam(false)]
		protected bool Windowed { get; set; }

		/// <summary>
		/// Which window mode to use for the PC or Mac client. Only Windowed and Fullscreen are fully supported.
		/// </summary>
		/// 
		[AutoParam(EWindowMode.Windowed)]
		public EWindowMode WindowMode { get; set; }

		/// <summary>
		/// Do not specify the unattended flag
		/// </summary>
		/// 
		[AutoParam(false)]
		public bool Attended { get; set; }

		/// <summary>
		/// Maximum duration in seconds that this test is expected to run for. Defaults to 600.
		/// </summary>
		[AutoParam(600.0f)]
		public float MaxDuration { get; set; }

		/// <summary>
		/// Whether ensures are considered a failure
		/// </summary>
		[AutoParam(false)]
		public bool FailOnEnsures { get; set; }

		/// <summary>
		/// Whether warnings are shown in the summary
		/// </summary>
		[AutoParam(false)]
		public bool ShowWarningsInSummary { get; set; }

		/// <summary>
		/// Whether warnings are shown in the summary
		/// </summary>
		[AutoParam(false)]
		public bool ShowErrorsInSummary { get; set; }

		/// <summary>
		/// Whether the test expects all roles to exit
		/// </summary>
		public bool AllRolesExit { get; set; }

		/// <summary>
		/// The collection of options which define heartbeat behavior
		/// </summary>
		public UnrealHeartbeatOptions HeartbeatOptions { get; set; }

		/// <summary>
		/// Prevents heartbeats timeouts from being checked so that tests will not fail from missed heartbeats
		/// </summary>
		[AutoParam(false)]
		public bool DisableHeartbeatTimeout { get; set; }

		// Member variables 

		/// <summary>
		/// A map of role types to test roles
		/// </summary>
		public Dictionary<UnrealTargetRole, List<UnrealTestRole>> RequiredRoles { get; private set; }

		/// <summary>
		/// Base constructor
		/// </summary>
		public UnrealTestConfiguration()
		{
			MaxDuration = 600;	// 10m

			// create the role structure
			RequiredRoles = new Dictionary<UnrealTargetRole, List<UnrealTestRole>>();

			HeartbeatOptions = new UnrealHeartbeatOptions();
		}

		/// <summary>
		/// Set this test to use dummy, renderless clients.
		/// </summary>
		/// <param name="quantity">Number of dummy clients to spawn.</param>
		/// <param name="AdditionalCommandLine"></param>
		public void AddDummyClients(int Quantity, string AdditionalCommandLine = "")
		{
			IEnumerable<UnrealTestRole> DummyClientRole = RequireRoles(UnrealTargetRole.Client, UnrealTargetPlatform.Win64, Quantity, ERoleModifier.Dummy);
			foreach (UnrealTestRole DummyClient in DummyClientRole)
			{
				DummyClient.CommandLine += " -nullrhi " + AdditionalCommandLine;
			}
		}

		/// <summary>
		/// Adds one role of the specified type to this test. With inherited tests this could
		/// return an existing role so care should be added to append commandlines, controllers etc
		/// </summary>
		/// <param name="Role"></param>
		/// <returns></returns>
		public UnrealTestRole RequireRole(UnrealTargetRole InRole)
		{
			return RequireRoles(InRole, 1).First();
		}

		public UnrealTestRole RequireRole(UnrealTargetRole InRole, UnrealTargetPlatform PlatformOverride)
		{
			return RequireRoles(InRole, PlatformOverride, 1).First();
		}

		/// <summary>
		/// Adds 'Count' of the specified roles to this test
		/// </summary>
		/// <param name="Role"></param>
		/// <param name="Count"></param>
		/// <returns></returns>
		public IEnumerable<UnrealTestRole> RequireRoles(UnrealTargetRole InRole, int Count)
		{
			return RequireRoles(InRole, null, Count);
		}

		public IEnumerable<UnrealTestRole> RequireRoles(UnrealTargetRole InRole, UnrealTargetPlatform? PlatformOverride, int Count, ERoleModifier roleType = ERoleModifier.None)
		{
			if (RequiredRoles.ContainsKey(InRole) == false)
			{
				RequiredRoles[InRole] = new List<UnrealTestRole>();
			}

			List<UnrealTestRole> RoleList = new List<UnrealTestRole>();

			RequiredRoles[InRole].ForEach((R) => { if (R.PlatformOverride == PlatformOverride) RoleList.Add(R); });

			for (int i = RoleList.Count; i < Count; i++)
			{
				UnrealTestRole NewRole = new UnrealTestRole(InRole, PlatformOverride);
				NewRole.RoleType = roleType;
				RoleList.Add(NewRole);
				RequiredRoles[InRole].Add(NewRole);
			}

			return RoleList;
		}

		/// <summary>
		/// Returns the number of roles of the specified type that exist for this test
		/// </summary>
		/// <param name="Role"></param>
		/// <returns></returns>
		public int RoleCount(UnrealTargetRole Role)
		{
			int Roles = 0;

			if (RequiredRoles.ContainsKey(Role))
			{
				Roles = RequiredRoles[Role].Count;
			}

			return Roles;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="AppConfig"></param>
		public void ApplyToConfig(UnrealAppConfig AppConfig)
		{
			throw new AutomationException("Unreal tests should use ApplyToConfig(Config, Role, OtherRoles)");
		}

		/// <summary>
		/// Apply our options to the provided app config
		/// </summary>
		/// <param name="AppConfig"></param>
		/// <returns></returns>
		public virtual void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			if (AppConfig.ProcessType.IsClient())
			{
				if (Nullrhi)
				{
					AppConfig.CommandLine += " -nullrhi";
				}
				else
				{
					if (AppConfig.Platform == UnrealTargetPlatform.Win64 || AppConfig.Platform == UnrealTargetPlatform.Mac)
					{
						if(!IgnoreDefaultResolutionAndWindowMode)
						{
							if (Globals.Params.ToString().Contains("-resx") == false)
							{
								AppConfig.CommandLine += String.Format(" -ResX={0} -ResY={1}", ResX, ResY);
							}
							if (WindowMode == EWindowMode.Windowed || Windowed)
							{
								AppConfig.CommandLine += " -windowed";
							}
							else if (WindowMode == EWindowMode.Fullscreen)
							{
								AppConfig.CommandLine += " -fullscreen";
							}
							else if (WindowMode == EWindowMode.WindowedFullscreen) // Proper -windowedfullscreen flag does not exist and some platforms treat both modes as the same.
							{
								AppConfig.CommandLine += " -fullscreen";
							}
							else
							{
								Log.Warning("Test config uses an unsupported WindowMode: {0}! WindowMode not set.", Enum.GetName(typeof(EWindowMode), WindowMode));
							}
						}
					}

					if (ScreenshotPeriod > 0 && Nullrhi == false)
					{
						AppConfig.CommandLine += string.Format(" -gauntlet.screenshotperiod={0}", ScreenshotPeriod);
					}
				}
			}

			// use -log on servers so we get a window..
			if (AppConfig.ProcessType.IsServer())
			{
				AppConfig.CommandLine += " -log";
			}

			if (Attended == false)
			{
				AppConfig.CommandLine += " -unattended";
			}

			AppConfig.CommandLine += " -stdout -AllowStdOutLogVerbosity";

			float HeartbeatPeriod = Globals.Params.ParseValue("HeartbeatPeriod", HeartbeatOptions.HeartbeatPeriod);
			if (HeartbeatPeriod > 0)
			{
				AppConfig.CommandLine += string.Format(" -gauntlet.heartbeatperiod={0}", HeartbeatPeriod);
			}
		}
	}

}