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
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Text;
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
	/// Options for AWS
	/// </summary>
	public interface IAwsStorageOptions
	{
		/// <summary>
		/// Name of the bucket to use
		/// </summary>
		public string? BucketName { get; }

		/// <summary>
		/// Base path within the bucket 
		/// </summary>
		public string? BucketPath { get; }
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
		/// Options for AWs
		/// </summary>
		private IAwsStorageOptions Options;

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
		/// <param name="AwsOptions">AWS options</param>
		/// <param name="Options">Storage options</param>
		/// <param name="Logger">Logger interface</param>
		public AwsStorageBackend(AWSOptions AwsOptions, IAwsStorageOptions Options, ILogger<AwsStorageBackend> Logger)
		{
			this.Client = AwsOptions.CreateServiceClient<IAmazonS3>();
			this.Options = Options;
			this.Semaphore = new SemaphoreSlim(16);
			this.Logger = Logger;

			Logger.LogInformation("Created AWS storage backend for bucket {BucketName} using credentials {Credentials} {CredentialsStr}", Options.BucketName, AwsOptions.Credentials.GetType(), AwsOptions.Credentials.ToString());
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

		string GetFullPath(string Path)
		{
			if (Options.BucketPath == null)
			{
				return Path;
			}

			StringBuilder Result = new StringBuilder();
			Result.Append(Options.BucketPath);
			if (Result.Length > 0 && Result[Result.Length - 1] != '/')
			{
				Result.Append('/');
			}
			Result.Append(Path);
			return Result.ToString();
		}

		/// <inheritdoc/>
		public async Task<Stream?> ReadAsync(string Path)
		{
			string FullPath = GetFullPath(Path);

			IDisposable? Lock = null;
			GetObjectResponse? Response = null;
			try
			{
				Lock = await Semaphore.UseWaitAsync();

				GetObjectRequest NewGetRequest = new GetObjectRequest();
				NewGetRequest.BucketName = Options.BucketName;
				NewGetRequest.Key = FullPath;

				Response = await Client.GetObjectAsync(NewGetRequest);

				return new WrappedResponseStream(Lock, Response);
			}
			catch (Exception Ex)
			{
				Logger.LogWarning(Ex, "Unable to read {Path} from S3", FullPath);

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

			string FullPath = GetFullPath(Path);
			for (int Attempt = 0; ; Attempt++)
			{
				try
				{
					using IDisposable Lock = await Semaphore.UseWaitAsync();
					await WriteInternalAsync(FullPath, InputStream);
					Logger.LogDebug("Written data to {Path}", Path);
					break;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unable to write data to {Path} ({Attempt}/{AttemptCount})", FullPath, Attempt + 1, RetryTimes.Length + 1);
					if (Attempt >= RetryTimes.Length)
					{
						throw new AwsException($"Unable to write to bucket {Options.BucketName} path {FullPath}", Ex);
					}
				}

				await Task.Delay(RetryTimes[Attempt]);
			}
		}

		/// <inheritdoc/>
		public async Task WriteInternalAsync(string FullPath, Stream Stream)
		{
			const int MinPartSize = 5 * 1024 * 1024;

			long StreamLen = Stream.Length;
			if (StreamLen < MinPartSize)
			{
				// Read the data into memory. Errors with hash value not matching if we don't do this (?)
				byte[] Buffer = new byte[StreamLen];
				await ReadExactLengthAsync(Stream, Buffer, (int)StreamLen);

				// Upload it to S3
				using (MemoryStream InputStream = new MemoryStream(Buffer))
				{
					PutObjectRequest UploadRequest = new PutObjectRequest();
					UploadRequest.BucketName = Options.BucketName;
					UploadRequest.Key = FullPath;
					UploadRequest.InputStream = InputStream;
					UploadRequest.Metadata.Add("bytes-written", StreamLen.ToString(CultureInfo.InvariantCulture));
					await Client.PutObjectAsync(UploadRequest);
				}
			}
			else
			{
				// Initiate a multi-part upload
				InitiateMultipartUploadRequest InitiateRequest = new InitiateMultipartUploadRequest();
				InitiateRequest.BucketName = Options.BucketName;
				InitiateRequest.Key = FullPath;

				InitiateMultipartUploadResponse InitiateResponse = await Client.InitiateMultipartUploadAsync(InitiateRequest);
				try
				{
					// Buffer for reading the data
					byte[] Buffer = new byte[MinPartSize];

					// Upload all the parts
					List<PartETag> PartTags = new List<PartETag>();
					for (long StreamPos = 0; StreamPos < StreamLen;)
					{
						// Read the next chunk of data into the buffer
						int BufferLen = (int)Math.Min((long)MinPartSize, StreamLen - StreamPos);
						await ReadExactLengthAsync(Stream, Buffer, BufferLen);
						StreamPos += BufferLen;

						// Upload the part
						using (MemoryStream InputStream = new MemoryStream(Buffer, 0, BufferLen))
						{
							UploadPartRequest PartRequest = new UploadPartRequest();
							PartRequest.BucketName = Options.BucketName;
							PartRequest.Key = FullPath;
							PartRequest.UploadId = InitiateResponse.UploadId;
							PartRequest.InputStream = InputStream;
							PartRequest.PartSize = BufferLen;
							PartRequest.PartNumber = PartTags.Count + 1;
							PartRequest.IsLastPart = (StreamPos == StreamLen);

							UploadPartResponse PartResponse = await Client.UploadPartAsync(PartRequest);
							PartTags.Add(new PartETag(PartResponse.PartNumber, PartResponse.ETag));
						}
					}

					// Mark the upload as complete
					CompleteMultipartUploadRequest CompleteRequest = new CompleteMultipartUploadRequest();
					CompleteRequest.BucketName = Options.BucketName;
					CompleteRequest.Key = FullPath;
					CompleteRequest.UploadId = InitiateResponse.UploadId;
					CompleteRequest.PartETags = PartTags;
					await Client.CompleteMultipartUploadAsync(CompleteRequest);
				}
				catch
				{
					// Abort the upload
					AbortMultipartUploadRequest AbortRequest = new AbortMultipartUploadRequest();
					AbortRequest.BucketName = Options.BucketName;
					AbortRequest.Key = FullPath;
					AbortRequest.UploadId = InitiateResponse.UploadId;
					await Client.AbortMultipartUploadAsync(AbortRequest);

					throw;
				}
			}
		}

		/// <summary>
		/// Reads data of an exact length into a stream
		/// </summary>
		/// <param name="Stream">The stream to read from</param>
		/// <param name="Buffer">The buffer to read into</param>
		/// <param name="Length">Length of the data to read</param>
		/// <returns>Async task</returns>
		static async Task ReadExactLengthAsync(System.IO.Stream Stream, byte[] Buffer, int Length)
		{
			int BufferPos = 0;
			while (BufferPos < Length)
			{
				int BytesRead = await Stream.ReadAsync(Buffer, BufferPos, Length - BufferPos);
				if (BytesRead == 0)
				{
					throw new InvalidOperationException("Unexpected end of stream");
				}
				BufferPos += BytesRead;
			}
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(string Path)
		{
			DeleteObjectRequest NewDeleteRequest = new DeleteObjectRequest();
			NewDeleteRequest.BucketName = Options.BucketName;
			NewDeleteRequest.Key = GetFullPath(Path);
			await Client.DeleteObjectAsync(NewDeleteRequest);
		}

		/// <inheritdoc/>
		public async Task<bool> ExistsAsync(string Path)
		{
			try
			{
				GetObjectMetadataRequest Request = new GetObjectMetadataRequest();
				Request.BucketName = Options.BucketName;
				Request.Key = GetFullPath(Path);
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
