using Amazon.Extensions.NETCore.Setup;
using Amazon.Runtime;
using Amazon.S3;
using Amazon.S3.Model;
using EpicGames.Core;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Storage.Impl
{
	/// <summary>
	/// Exception wrapper for S3 requests
	/// </summary>
	[SuppressMessage("Design", "CA1032:Implement standard exception constructors", Justification = "<Pending>")]
	public sealed class AwsException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message">Message for the exception</param>
		/// <param name="InnerException">Inner exception data</param>
		public AwsException(string? Message, Exception? InnerException)
			: base(Message, InnerException)
		{
		}
	}

	/// <summary>
	/// FileStorage implementation using an s3 bucket
	/// </summary>
	public sealed class AwsStorageBackend : IStorageBackend
	{
		/// <summary>
		/// S3 Client
		/// </summary>
		private readonly IAmazonS3 Client;

		/// <summary>
		/// S3 bucket name
		/// </summary>
		private readonly string BucketName;

		/// <summary>
		/// Semaphore for connecting to AWS
		/// </summary>
		private SemaphoreSlim Semaphore;

		/// <summary>
		/// Logger interface
		/// </summary>
		private ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings">Current Horde Settings</param>
		/// <param name="Configuration">Configuration</param>
		/// <param name="Logger">Logger interface</param>
		public AwsStorageBackend(IOptionsMonitor<ServerSettings> Settings, IConfiguration Configuration, ILogger<AwsStorageBackend> Logger)
		{
			ServerSettings CurrentSettings = Settings.CurrentValue;
			AWSOptions Options = Configuration.GetAWSOptions();
			Options.Credentials = GetCredentials(CurrentSettings);

			this.Client = Options.CreateServiceClient<IAmazonS3>();
			this.BucketName = CurrentSettings.S3LogBucketName;
			this.Semaphore = new SemaphoreSlim(16);
			this.Logger = Logger;
		}

		enum AWSCredentialsType
		{
			Basic,
			AssumeRole,
			AssumeRoleWebIdentity
		}

		/// <summary>
		/// Gets auth credentials based on type
		/// </summary>
		/// <param name="Settings">Horde settings</param>
		/// <returns></returns>
		static AWSCredentials GetCredentials(ServerSettings Settings)
		{
			AWSCredentialsType AuthType;
			if (!Enum.TryParse(Settings.S3CredentialType, true, out AuthType))
			{
				throw new Exception("Could not determine auth type from appsettings. Should be Basic, AssumeRole, or AssumeRoleWebIdentity!");
			}

			switch (AuthType)
			{
				case AWSCredentialsType.Basic:
					return new BasicAWSCredentials(Settings.S3ClientKeyId, Settings.S3ClientSecret);
				case AWSCredentialsType.AssumeRole:
					return new AssumeRoleAWSCredentials(FallbackCredentialsFactory.GetCredentials(), Settings.S3AssumeArn, "Horde");
				case AWSCredentialsType.AssumeRoleWebIdentity:
					return AssumeRoleWithWebIdentityCredentials.FromEnvironmentVariables();
				default:
					throw new Exception($"Invalid authentication type: {AuthType}");
			}
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
			Semaphore.Dispose();
		}

		/// <inheritdoc/>
		public Task<bool> TouchAsync(string Path)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public async Task<ReadOnlyMemory<byte>?> ReadAsync(string Path)
		{
			using (await Semaphore.UseWaitAsync())
			{
				GetObjectRequest NewGetRequest = new GetObjectRequest();
				NewGetRequest.BucketName = BucketName;
				NewGetRequest.Key = Path;

				try
				{
					using (GetObjectResponse GetResponse = await this.Client.GetObjectAsync(NewGetRequest))
					{
						using (Stream ResponseStream = GetResponse.ResponseStream)
						{
							using (MemoryStream OutputStream = new MemoryStream())
							{
								await ResponseStream.CopyToAsync(OutputStream);
								return OutputStream.ToArray();
							}
						}
					}
				}
				catch(Exception Ex)
				{
					throw new AwsException($"Unable to read from bucket {BucketName} path {Path}", Ex);
				}
			}
		}

		/// <inheritdoc/>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		public async Task WriteAsync(string Path, ReadOnlyMemory<byte> Data)
		{
			TimeSpan[] RetryTimes =
			{
				TimeSpan.FromSeconds(1.0),
				TimeSpan.FromSeconds(5.0),
				TimeSpan.FromSeconds(10.0),
			};

			for (int Attempt = 0; ; Attempt++)
			{
				try
				{
					await WriteInternalAsync(Path, Data);
					Logger.LogDebug("Written data to {Path}", Path);
					break;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to write data to {Path} ({Attempt}/{AttemptCount})", Path, Attempt + 1, RetryTimes.Length + 1);
					if(Attempt >= RetryTimes.Length)
					{
						throw new AwsException($"Unable to write to bucket {BucketName} path {Path}", Ex);
					}
				}

				await Task.Delay(RetryTimes[Attempt]);
			}
		}

		/// <inheritdoc/>
		public async Task WriteInternalAsync(string Path, ReadOnlyMemory<byte> Data)
		{
			using (await Semaphore.UseWaitAsync())
			{
				using (ReadOnlyMemoryStream InputStream = new ReadOnlyMemoryStream(Data)) 
				{
					PutObjectRequest NewUploadRequest = new PutObjectRequest();
					NewUploadRequest.BucketName = BucketName;
					NewUploadRequest.Key = Path;
					NewUploadRequest.InputStream = InputStream;
					NewUploadRequest.Metadata.Add("bytes-written", Data.Length.ToString(CultureInfo.InvariantCulture));
					await Client.PutObjectAsync(NewUploadRequest);
				}
			}
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			throw new NotImplementedException();
		}
	}
}
