// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

namespace Horde.Build.Configuration
{
	/// <summary>
	/// Server settings 
	/// </summary>
	public class GetServerSettingsResponse
	{
		/// <summary>
		/// MongoDB connection string
		/// </summary>
		public string? DatabaseConnectionString { get; set; }

		/// <summary>
		/// MongoDB database name
		/// </summary>
		public string DatabaseName { get; set; }

		/// <summary>
		/// The claim type for administrators
		/// </summary>
		public string AdminClaimType { get; set; }

		/// <summary>
		/// Value of the claim type for administrators
		/// </summary>
		public string AdminClaimValue { get; set; }

		/// <summary>
		/// Optional certificate to trust in order to access the database (eg. AWS public cert for TLS)
		/// </summary>
		public string? DatabasePublicCert { get; set; }

		/// <summary>
		/// Access the database in read-only mode (avoids creating indices or updating content)
		/// Useful for debugging a local instance of HordeServer against a production database.
		/// </summary>
		public bool DatabaseReadOnlyMode { get; set; }

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
		public string? JwtIssuer { get; set; }

		/// <summary>
		/// Secret key used to sign JWTs. This setting is typically only used for development. In prod, a unique secret key will be generated and stored in the DB for each unique server instance.
		/// </summary>
		public string? JwtSecret { get; set; }

		/// <summary>
		/// Length of time before JWT tokens expire, in hourse
		/// </summary>
		public int JwtExpiryTimeHours { get; set; }

		/// <summary>
		/// Disable authentication for debugging purposes
		/// </summary>
		public bool DisableAuth { get; set; }

		/// <summary>
		/// Whether to enable Cors, generally for development purposes
		/// </summary>
		public bool CorsEnabled { get; set; }

		/// <summary>
		/// Allowed Cors origin 
		/// </summary>
		public string CorsOrigin { get; set; }

		/// <summary>
		/// Whether to enable a schedule in test data (false by default for development builds)
		/// </summary>
		public bool EnableScheduleInTestData { get; set; }

		/// <summary>
		/// Interval between rebuilding the schedule queue with a DB query.
		/// </summary>
		public TimeSpan SchedulePollingInterval { get; set; }

		/// <summary>
		/// Interval between polling for new jobs
		/// </summary>
		public TimeSpan NoResourceBackOffTime { get; set; }

		/// <summary>
		/// Interval between attempting to assign agents to take on jobs
		/// </summary>
		public TimeSpan InitiateJobBackOffTime { get; set; }

		/// <summary>
		/// Interval between scheduling jobs when an unknown error occurs
		/// </summary>
		public TimeSpan UnknownErrorBackOffTime { get; set; }

		/// <summary>
		/// Config for connecting to Redis server(s).
		/// Setting it to null will disable Redis use and connection
		/// See format at https://stackexchange.github.io/StackExchange.Redis/Configuration.html
		/// </summary>
		public string? RedisConnectionConfig { get; set; }

		/// <summary>
		/// Type of write cache to use in log service
		/// Currently Supported: "InMemory" or "Redis"
		/// </summary>
		public string LogServiceWriteCacheType { get; set; }

		/// <summary>
		/// Provider Type
		/// Currently Supported: "S3" or "FileSystem"
		/// </summary>
		public StorageProviderType ExternalStorageProviderType { get; set; }

		/// <summary>
		/// Local log/artifact storage directory, if using type filesystem
		/// </summary>
		public string LocalLogsDir { get; set; } = null!;

		/// <summary>
		/// Local blob storage directory, if using type filesystem
		/// </summary>
		public string LocalBlobsDir { get; set; } = null!;

		/// <summary>
		/// Local artifact storage directory, if using type filesystem
		/// </summary>
		public string LocalArtifactsDir { get; set; } = null!;

		/// <summary>
		/// S3 bucket region for logfile storage
		/// </summary>
		public string S3BucketRegion { get; set; } = null!;

		/// <summary>
		/// Arn to assume for s3.  "Basic", "AssumeRole", "AssumeRoleWebIdentity" only
		/// </summary>
		public string? S3CredentialType { get; set; }

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
		public bool LogJsonToStdOut { get; set; }

		/// <summary>
		/// Which fleet manager service to use
		/// </summary>
		public FleetManagerType FleetManager { get; set; }

		/// <summary>
		/// Whether to run scheduled jobs. Not wanted for development.
		/// </summary>
		public bool DisableSchedules { get; set; }

		/// <summary>
		/// Timezone for evaluating schedules
		/// </summary>
		public string? ScheduleTimeZone { get; set; }

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
		/// The p4 bridge service username
		/// </summary>
		public string? P4BridgeServiceUsername { get; set; }

		/// <summary>
		/// The p4 bridge service password
		/// </summary>
		public string? P4BridgeServicePassword { get; set; }

		/// <summary>
		/// Whether the p4 bridge service account can impersonate other users
		/// </summary>
		public bool P4BridgeCanImpersonate { get; set; }

		/// <summary>
		/// Set the minimum size of the global thread pool
		/// This value has been found in need of tweaking to avoid timeouts with the Redis client during bursts
		/// of traffic. Default is 16 for .NET Core CLR. The correct value is dependent on the traffic the Horde Server
		/// is receiving. For Epic's internal deployment, this is set to 40.
		/// </summary>
		public int? GlobalThreadPoolMinSize { get; set; }

		/// <summary>
		/// The number of live server setting updates 
		/// </summary>
		public int NumServerUpdates { get; set; }

		/// <summary>
		/// Path to the root config file
		/// </summary>
		public string? GlobalConfigPath { get; set; }

		/// <summary>
		/// Path to the user config file
		/// </summary>
		public string? UserServerSettingsPath { get; set; }

		/// <summary>
		/// Lazily computed timezone value
		/// </summary>
		public TimeZoneInfo TimeZoneInfo { get; set; } = null!;

		/// <summary>
		/// Constructor
		/// </summary>		
		public GetServerSettingsResponse(ServerSettings settings, string? globalConfigPath = null)
		{
			GlobalConfigPath = globalConfigPath;

			if (GlobalConfigPath == null)
			{
				GlobalConfigPath = settings.ConfigPath;
			}

			NumServerUpdates = 0;

			UserServerSettingsPath = Program.UserConfigFile.ToString();
			DatabaseConnectionString = settings.DatabaseConnectionString;
			DatabaseName = settings.DatabaseName;
			DatabasePublicCert = settings.DatabasePublicCert;
			DatabaseReadOnlyMode = settings.DatabaseReadOnlyMode;

			AdminClaimType = settings.AdminClaimType;
			AdminClaimValue = settings.AdminClaimValue;

			ServerPrivateCert = settings.ServerPrivateCert;
			OidcAuthority = settings.OidcAuthority;
			OidcClientId = settings.OidcClientId;
			OidcSigninRedirect = settings.OidcSigninRedirect;

			JwtIssuer = settings.JwtIssuer;
			JwtSecret = settings.JwtSecret;
			JwtExpiryTimeHours = settings.JwtExpiryTimeHours;

			CorsEnabled = settings.CorsEnabled;
			CorsOrigin = settings.CorsOrigin;
			EnableScheduleInTestData = settings.EnableScheduleInTestData;
			SchedulePollingInterval = settings.SchedulePollingInterval;

			NoResourceBackOffTime = settings.NoResourceBackOffTime;
			InitiateJobBackOffTime = settings.InitiateJobBackOffTime;
			UnknownErrorBackOffTime = settings.UnknownErrorBackOffTime;

			RedisConnectionConfig = settings.RedisConnectionConfig;
			LogServiceWriteCacheType = settings.LogServiceWriteCacheType;
			ExternalStorageProviderType = settings.ExternalStorageProviderType;
			LocalLogsDir = settings.LocalLogsDir;
			LocalBlobsDir = settings.LocalBlobsDir;
			LocalArtifactsDir = settings.LocalArtifactsDir;

			S3BucketRegion = settings.S3BucketRegion;
			S3CredentialType = settings.S3CredentialType;
			S3ClientKeyId = settings.S3ClientKeyId;
			S3ClientSecret = settings.S3ClientSecret;
			S3AssumeArn = settings.S3AssumeArn;
			S3LogBucketName = settings.S3LogBucketName;
			S3ArtifactBucketName = settings.S3ArtifactBucketName;

			LogRelayServer = settings.LogRelayServer;
			LogRelayBearerToken = settings.LogRelayBearerToken;
			LogJsonToStdOut = settings.LogJsonToStdOut;

			FleetManager = settings.FleetManager;
			DisableSchedules = settings.DisableSchedules;
			ScheduleTimeZone = settings.ScheduleTimeZone;

			SlackToken = settings.SlackToken;
			SlackSocketToken = settings.SlackSocketToken;

			SlackToken = settings.SlackToken;
			SlackToken = settings.SlackToken;
			UpdateStreamsNotificationChannel = settings.UpdateStreamsNotificationChannel;

			SmtpServer = settings.SmtpServer;
			EmailSenderAddress = settings.EmailSenderAddress;
			EmailSenderName = settings.EmailSenderName;

			UpdateStreamsNotificationChannel = settings.UpdateStreamsNotificationChannel;
			UpdateStreamsNotificationChannel = settings.UpdateStreamsNotificationChannel;
			UpdateStreamsNotificationChannel = settings.UpdateStreamsNotificationChannel;

			P4BridgeServiceUsername = settings.P4BridgeServiceUsername;
			P4BridgeServicePassword = settings.P4BridgeServicePassword;
			P4BridgeCanImpersonate = settings.P4BridgeCanImpersonate;

			GlobalThreadPoolMinSize = settings.GlobalThreadPoolMinSize;
			TimeZoneInfo = settings.TimeZoneInfo;
		}
	}

	/// <summary>
	/// Parameters to update global settings
	/// </summary>
	public class UpdateServerSettingsRequest
	{
		/// <summary>
		/// Delta settings to update
		/// </summary>
		public Dictionary<string, object>? Settings { get; set; }
	}

	/// <summary>
	/// Response for server updates
	/// </summary>
	public class ServerUpdateResponse
	{
		/// <summary>
		/// List of any error messages when updating
		/// </summary>
		public List<string> Errors { get; set; } = new List<string>();

		/// <summary>
		/// Whether a server restart is required
		/// </summary>
		public bool RestartRequired { get; set; } = false;
	}

	/// <summary>
	/// Parameters to update global settings
	/// </summary>
	public class UpdateGlobalConfigRequest
	{
		/// <summary>
		/// Delta updates for global config
		/// </summary>
		public string? GlobalsJson { get; set; }

		/// <summary>
		/// Projects json, filename => json string
		/// </summary>
		public Dictionary<string, string>? ProjectsJson { get; set; }

		/// <summary>
		/// Base64 encoded project logo
		/// </summary>
		public string? ProjectLogo { get; set; }

		/// <summary>
		/// Streams json, filename => json string
		/// </summary>
		public Dictionary<string, string>? StreamsJson { get; set; }
		
		/// <summary>
		/// Default Pool Name
		/// </summary>
		public string? DefaultPoolName {get; set;}		
	}
}

