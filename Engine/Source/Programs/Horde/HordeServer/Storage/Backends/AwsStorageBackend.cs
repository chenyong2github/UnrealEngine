// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Extensions.NETCore.Setup;
using Amazon.Runtime;
using Amazon.S3;
using Amazon.S3.Model;
using HordeServer.Utilities;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Storage.Backends
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
	public sealed class AwsStorageBackend : IStorageBackend, IDisposable
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
		/// <param name="AwsOptions">The AWS options</param>
		/// <param name="ServerSettings">Options for configuring AWS</param>
		/// <param name="Logger">Logger interface</param>
		public AwsStorageBackend(AWSOptions AwsOptions, IOptions<ServerSettings> ServerSettings, ILogger<AwsStorageBackend> Logger)
		{
			ServerSettings OptionsValue = ServerSettings.Value;
			this.Client = AwsOptions.CreateServiceClient<IAmazonS3>();
			this.BucketName = OptionsValue.S3LogBucketName;
			this.Semaphore = new SemaphoreSlim(16);
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Client.Dispose();
			Semaphore.Dispose();
		}

		class WrappedResponseStream : Stream
		{
			IDisposable Semaphore;
			GetObjectResponse Response;
			Stream ResponseStream;

			public WrappedResponseStream(IDisposable Semaphore, GetObjectResponse Response)
			{
				this.Semaphore = Semaphore;
				this.Response = Response;
				this.ResponseStream = Response.ResponseStream;
			}

			public override bool CanRead => true;
			public override bool CanSeek => false;
			public override bool CanWrite => false;
			public override long Length => ResponseStream.Length;
			public override long Position { get => ResponseStream.Position; set => throw new NotSupportedException(); }

			public override void Flush() => ResponseStream.Flush();

			public override int Read(byte[] buffer, int offset, int count) => ResponseStream.Read(buffer, offset, count);
			public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) => ResponseStream.ReadAsync(buffer, cancellationToken);
			public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken) => ResponseStream.ReadAsync(buffer, offset, count, cancellationToken);

			public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
			public override void SetLength(long value) => throw new NotSupportedException();
			public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				Semaphore.Dispose();
				Response.Dispose();
				ResponseStream.Dispose();
			}

			public override async ValueTask DisposeAsync()
			{
				await base.DisposeAsync();

				Semaphore.Dispose();
				Response.Dispose();
				await ResponseStream.DisposeAsync();
			}
		}

		/// <inheritdoc/>
		public async Task<Stream?> ReadAsync(string Path)
		{
			IDisposable? Lock = null;
			GetObjectResponse? Response = null;
			try
			{
				Lock = await Semaphore.UseWaitAsync();

				GetObjectRequest NewGetRequest = new GetObjectRequest();
				NewGetRequest.BucketName = BucketName;
				NewGetRequest.Key = Path;

				Response = await Client.GetObjectAsync(NewGetRequest);

				return new WrappedResponseStream(Lock, Response);
			}
			catch (Exception Ex)
			{
				Logger.LogWarning(Ex, "Unable to read {Path} from S3", Path);

				Lock?.Dispose();
				Response?.Dispose();

				return null;
			}
		}

		/// <inheritdoc/>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "<Pending>")]
		public async Task WriteAsync(string Path, Stream InputStream)
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
					await WriteInternalAsync(Path, InputStream);
					Logger.LogDebug("Written data to {Path}", Path);
					break;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to write data to {Path} ({Attempt}/{AttemptCount})", Path, Attempt + 1, RetryTimes.Length + 1);
					if (Attempt >= RetryTimes.Length)
					{
						throw new AwsException($"Unable to write to bucket {BucketName} path {Path}", Ex);
					}
				}

				await Task.Delay(RetryTimes[Attempt]);
			}
		}

		/// <inheritdoc/>
		public async Task WriteInternalAsync(string Path, Stream InputStream)
		{
			using (await Semaphore.UseWaitAsync())
			{
				PutObjectRequest NewUploadRequest = new PutObjectRequest();
				NewUploadRequest.BucketName = BucketName;
				NewUploadRequest.Key = Path;
				NewUploadRequest.InputStream = InputStream;
				NewUploadRequest.Metadata.Add("bytes-written", InputStream.Length.ToString(CultureInfo.InvariantCulture));
				await Client.PutObjectAsync(NewUploadRequest);
			}
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(string Path)
		{
			DeleteObjectRequest NewDeleteRequest = new DeleteObjectRequest();
			NewDeleteRequest.BucketName = BucketName;
			NewDeleteRequest.Key = Path;
			await Client.DeleteObjectAsync(NewDeleteRequest);
		}

		/// <inheritdoc/>
		public async Task<bool> ExistsAsync(string Path)
		{
			try
			{
				GetObjectMetadataRequest Request = new GetObjectMetadataRequest();
				Request.BucketName = BucketName;
				Request.Key = Path;
				await Client.GetObjectMetadataAsync(Request);
				return true;
			}
			catch (AmazonS3Exception Ex) when (Ex.StatusCode == System.Net.HttpStatusCode.NotFound)
			{
				return false;
			}
		}
	}
}
