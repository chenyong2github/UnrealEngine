// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	/// <summary>
	/// This class contains settings for a site-specific deployment of UGS. Epic's internal implementation uses a static constructor in a NotForLicensees folder to initialize these values.
	/// </summary>
	static partial class DeploymentSettings
	{
#if WITH_TELEMETRY
		/// <summary>
		/// Delegate used to create a telemetry sink
		/// </summary>
		/// <param name="UserName">The default Perforce user name</param>
		/// <param name="SessionId">Unique identifier for this session</param>
		/// <param name="Log">Log writer</param>
		/// <returns>New telemetry sink instance</returns>
		public delegate ITelemetrySink CreateTelemetrySinkDelegate(string UserName, string SessionId, TextWriter Log);
#endif

		/// <summary>
		/// SQL connection string used to connect to the database for telemetry and review data. The 'Program' class is a partial class, to allow an
		/// opportunistically included C# source file in NotForLicensees/ProgramSettings.cs to override this value in a static constructor.
		/// </summary>
		public static readonly string ApiUrl = null;

		/// <summary>
		/// Servers to connect to for issue details by default
		/// </summary>
		public static readonly List<string> DefaultIssueApiUrls = new List<string>();

		/// <summary>
		/// The issue api to use for URL handler events
		/// </summary>
		public static readonly string UrlHandleIssueApi = null;

		/// <summary>
		/// Specifies the depot path to sync down the stable version of UGS from, without a trailing slash (eg. //depot/UnrealGameSync/bin). This is a site-specific setting. 
		/// The UnrealGameSync executable should be located at Release/UnrealGameSync.exe under this path, with any dependent DLLs.
		/// </summary>
		public static readonly string DefaultDepotPath = null;

		/// <summary>
		/// Depot path to sync additional tools from
		/// </summary>
		public static readonly string ToolsDepotPath = null;

		/// <summary>
		/// URL to send info about NET Core versions to.
		/// </summary>
		public static readonly string NetCoreTelemetryUrl = null;

#if WITH_TELEMETRY
		/// <summary>
		/// Delegate used to create a new telemetry sink
		/// </summary>
		public static readonly CreateTelemetrySinkDelegate CreateTelemetrySink = (UserName, SessionId, Log) => new NullTelemetrySink();
#endif

#if !UGS_LAUNCHER
		/// <summary>
		/// Delegate to allow validating a project being opened
		/// </summary>
		/// <param name="Task">The detected settings for the project</param>
		/// <param name="Log">The logger</param>
		/// <param name="Error">Receives an error on failure</param>
		/// <returns></returns>
		public delegate bool DetectProjectSettingsEvent(DetectProjectSettingsTask Task, TextWriter Log, out string Error);

		/// <summary>
		/// Called to validate the project settings
		/// </summary>
		public static DetectProjectSettingsEvent OnDetectProjectSettings = null;
#endif
	}
}
