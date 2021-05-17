// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Services;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;
using TimeZoneConverter;

namespace HordeServer
{
	/// <summary>
	/// Types of storage to use for log data
	/// </summary>
	public enum StorageProviderType
	{
		/// <summary>
		/// AWS S3
		/// </summary>
		S3,

		/// <summary>
		/// Local filesystem
		/// </summary>
		FileSystem,

		/// <summary>
		/// In-memory only (for testing)
		/// </summary>
		Transient,

		/// <summary>
		/// Relay to another server (useful for testing against prod)
		/// </summary>
		Relay,
	};

	/// <summary>
	/// Specifies the service to use for controlling the size of the fleet
	/// </summary>
	public enum FleetManagerType
	{
		/// <summary>
		/// Default (empty) instance
		/// </summary>
		None,

		/// <summary>
		/// Use AWS EC2 instances
		/// </summary>
		Aws,
	}

	
	/// <summary>
	/// Settings for a remote execution instance
	///
	/// Instances are a concept from Google's remote execution API that Horde implements.
	/// They are used for separating different type of executions into groups.
	/// </summary>
	public class RemoteExecInstanceSettings
	{
		/// <summary>
		/// gRPC URL for the content-addressable storage to be used for this instance
		/// </summary>
		[Required]
		public Uri? CasUrl { get; set; }
		
		/// <summary>
		/// gRPC URL for the action cache to be used for this instance
		/// </summary>
		[Required]
		public Uri? ActionCacheUrl { get; set; }
		
		/// <summary>
		/// Service account token for accessing the gRPC URLs
		/// </summary>
		[Required]
		public string? ServiceAccountToken { get; set; }
	}
	
	/// <summary>
	/// Settings for remote execution
	/// </summary>
	public class RemoteExecSettings
	{
		/// <summary>
		/// Mapping instance names to instance settings
		/// </summary>
		public Dictionary<string, RemoteExecInstanceSettings> Instances { get; set; } = new Dictionary<string, RemoteExecInstanceSettings>();
	}

	/// <summary>
	/// Global settings for the application
	/// </summary>
	public class ServerSettings
	{
		/// <summary>
		/// MongoDB connection string
		/// </summary>
		[Required]
		public string DatabaseConnectionString { get; set; } = null!;

		/// <summary>
		/// MongoDB database name
		/// </summary>
		[Required]
		public string DatabaseName { get; set; } = null!;

		/// <summary>
		/// The claim type for administrators
		/// </summary>
		public string AdminClaimType { get; set; } = HordeClaimTypes.InternalRole;

		/// <summary>
		/// Value of the claim type for administrators
		/// </summary>
		public string AdminClaimValue { get; set; } = "admin";

		/// <summary>
		/// Optional certificate to trust in order to access the database (eg. AWS public cert for TLS)
		/// </summary>
		public string? DatabasePublicCert { get; set; }

		/// <summary>
		/// Access the database in read-only mode (avoids creating indices or updating content)
		/// Useful for debugging a local instance of HordeServer against a production database.
		/// </summary>
		public bool DatabaseReadOnlyMode { get; set; } = false;

		/// <summary>
		/// Optional PFX certificate to use for encryting agent SSL traffic. This can be a self-signed certificate, as long as it's trusted by agents.
		/// </summary>
		public string? ServerPrivateCert { get; set; }

		/// <summary>
		/// Issuer for tokens from the auth provider
		/// </summary>
		public string? OidcAuthority { get; set; }
		
		/// <summary>
		/// Client id for the OIDC authority
		/// </summary>
		public string? OidcClientId { get; set; }

		/// <summary>
		/// Optional redirect url provided to OIDC login
		/// </summary>
		public string? OidcSigninRedirect { get; set; }		

		/// <summary>
		/// Name of the issuer in bearer tokens from the server
		/// </summary>
		public string? JwtIssuer { get; set; } = null!;

		/// <summary>
		/// Secret key used to sign JWTs. This setting is typically only used for development. In prod, a unique secret key will be generated and stored in the DB for each unique server instance.
		/// </summary>
		public string? JwtSecret { get; set; } = null!;

		/// <summary>
		/// Length of time before JWT tokens expire, in hourse
		/// </summary>
		public int JwtExpiryTimeHours { get; set; } = 4;

		/// <summary>
		/// Disable authentication for debugging purposes
		/// </summary>
		public bool DisableAuth { get; set; }

		/// <summary>
		/// Whether to enable Cors, generally for development purposes
		/// </summary>
		public bool CorsEnabled { get; set; } = false;

		/// <summary>
		/// Allowed Cors origin 
		/// </summary>
		public string CorsOrigin { get; set; } = null!;

		/// <summary>
		/// Whether to enable a schedule in test data (false by default for development builds)
		/// </summary>
		public bool EnableScheduleInTestData { get; set; }

		/// <summary>
		/// Interval between rebuilding the schedule queue with a DB query.
		/// </summary>
		public TimeSpan SchedulePollingInterval { get; set; } = TimeSpan.FromSeconds(60.0);

		/// <summary>
		/// Interval between polling for new jobs
		/// </summary>
		public TimeSpan NoResourceBackOffTime { get; set; } = TimeSpan.FromSeconds(30.0);

		/// <summary>
		/// Interval between attempting to assign agents to take on jobs
		/// </summary>
		public TimeSpan InitiateJobBackOffTime { get; set; } = TimeSpan.FromSeconds(180.0);

		/// <summary>
		/// Interval between scheduling jobs when an unknown error occurs
		/// </summary>
		public TimeSpan UnknownErrorBackOffTime { get; set; } = TimeSpan.FromSeconds(120.0);

		/// <summary>
		/// Config for connecting to Redis server(s).
		/// Setting it to null will disable Redis use and connection
		/// See format at https://stackexchange.github.io/StackExchange.Redis/Configuration.html
		/// </summary>
		[Required]
		public string RedisConnectionConfig { get; set; } = null!;
		
		/// <summary>
		/// Type of write cache to use in log service
		/// Currently Supported: "InMemory" or "Redis"
		/// </summary>
		public string LogServiceWriteCacheType { get; set; } = "InMemory";

		/// <summary>
		/// Provider Type
		/// Currently Supported: "S3" or "FileSystem"
		/// </summary>
		public StorageProviderType ExternalStorageProviderType { get; set; } = StorageProviderType.FileSystem;

		/// <summary>
		/// local log file storage directory, if using type filesystem
		/// </summary>
		public string LocalStorageDir { get; set; } = "D:\\HordeLogs";

		/// <summary>
		/// S3 bucket region for logfile storage
		/// </summary>
		public string S3BucketRegion { get; set; } = null!;

		/// <summary>
		/// Arn to assume for s3.  "Basic", "AssumeRole", "AssumeRoleWebIdentity" only
		/// </summary>
		public string S3CredentialType { get; set; } = null!;

		/// <summary>
		/// S3 Client username (used in Basic auth type only)
		/// </summary>
		public string S3ClientKeyId { get; set; } = null!;

		/// <summary>
		/// S3 client password (used in Basic auth type only)
		/// </summary>
		public string S3ClientSecret { get; set; } = null!;

		/// <summary>
		/// Arn to assume for s3
		/// </summary>
		public string S3AssumeArn { get; set; } = null!;

		/// <summary>
		/// S3 log bucket name
		/// </summary>
		public string S3LogBucketName { get; set; } = null!;

		/// <summary>
		/// S3 artifact bucket name
		/// </summary>
		public string S3ArtifactBucketName { get; set; } = null!;

		/// <summary>
		/// When using a relay storage provider, specifies the remote server to use
		/// </summary>
		public string? LogRelayServer { get; set; }

		/// <summary>
		/// Authentication token for using a relay server
		/// </summary>
		public string? LogRelayBearerToken { get; set; }

		/// <summary>
		/// Whether to log json to stdout
		/// </summary>
		public bool LogJsonToStdOut { get; set; } = false;

		/// <summary>
		/// Which fleet manager service to use
		/// </summary>
		public FleetManagerType FleetManager { get; set; } = FleetManagerType.None;

		/// <summary>
		/// Whether to run scheduled jobs. Not wanted for development.
		/// </summary>
		public bool DisableSchedules { get; set; } = true;

		/// <summary>
		/// Timezone for evaluating schedules
		/// </summary>
		public string? ScheduleTimeZone { get; set; }

		/// <summary>
		/// Address of the Perforce bridge
		/// </summary>
		public string? PerforceBridge { get; set; }

		/// <summary>
		/// Token for interacting with Slack
		/// </summary>
		public string? SlackToken { get; set; }

		/// <summary>
		/// Token for opening a socket to slack
		/// </summary>
		public string? SlackSocketToken { get; set; }

		/// <summary>
		/// Channel to send stream notification update failures to
		/// </summary>
		public string? UpdateStreamsNotificationChannel { get; set; }

		/// <summary>
		/// URI to the SmtpServer to use for sending email notifications
		/// </summary>
		public string? SmtpServer { get; set; }

		/// <summary>
		/// The email address to send email notifications from
		/// </summary>
		public string? EmailSenderAddress { get; set; }

		/// <summary>
		/// The name for the sender when sending email notifications
		/// </summary>
		public string? EmailSenderName { get; set; }

		/// <summary>
		/// The URl to use for generating links back to the dashboard.
		/// </summary>
		public Uri DashboardUrl { get; set; } = new Uri("https://localhost:3000");
		
		/// <summary>
		/// The URL to Helix Swarm server
		/// </summary>
		public Uri? HelixSwarmServerUrl { get; set; }
		
		/// <summary>
		/// Username to use when communicating with Helix Swarm
		/// </summary>
		public string? HelixSwarmUsername { get; set; }
		
		/// <summary>
		/// Password to use when communicating with Helix Swarm
		/// </summary>
		public string? HelixSwarmPassword { get; set; }
		
		/// <summary>
		/// Projects in Helix Swarm to monitor, including the state of reviews in each project
		/// Multiple projects can be specified, separated by comma.
		/// </summary>
		public string? HelixSwarmProjects { get; set; }

		/// <summary>
		/// The p4 bridge server
		/// </summary>
		public string? P4BridgeServer { get; set; }

		/// <summary>
		/// The p4 bridge service username
		/// </summary>
		public string? P4BridgeServiceUsername { get; set; }

		/// <summary>
		/// The p4 bridge service password
		/// </summary>
		public string? P4BridgeServicePassword { get; set; }

		/// <summary>
		/// Set the minimum size of the global thread pool
		/// This value has been found in need of tweaking to avoid timeouts with the Redis client during bursts
		/// of traffic. Default is 16 for .NET Core CLR. The correct value is dependent on the traffic the Horde Server
		/// is receiving. For Epic's internal deployment, this is set to 40.
		/// </summary>
		public int? GlobalThreadPoolMinSize { get; set; }

		/// <summary>
		/// Path to the root config file
		/// </summary>
		public string? ConfigPath { get; set; }

		/// <summary>
		/// Settings for remote execution
		/// </summary>
		public RemoteExecSettings RemoteExecSettings { get; set; } = new RemoteExecSettings();
		
		/// <summary>
		/// Lazily computed timezone value
		/// </summary>
		public TimeZoneInfo TimeZoneInfo
		{
			get
			{
				if (CachedTimeZoneInfo == null)
				{
					CachedTimeZoneInfo = (ScheduleTimeZone == null) ? TimeZoneInfo.Local : TZConvert.GetTimeZoneInfo(ScheduleTimeZone);
				}
				return CachedTimeZoneInfo;
			}
		}

		private TimeZoneInfo? CachedTimeZoneInfo;

		/// <summary>
		/// Check if Helix Swarm is enabled
		/// </summary>
		/// <returns>True if enabled</returns>
		public bool IsSwarmEnabled()
		{
			return HelixSwarmServerUrl != null;
		}
		
		/// <summary>
		/// Parse the comma-separated string of Swarm projects into an actual array
		/// </summary>
		/// <returns>Array of Swarm projects</returns>
		public string[] GetSwarmProjects()
		{
			return HelixSwarmProjects != null ? HelixSwarmProjects.Split(",") : Array.Empty<string>();
		}
	}
}
