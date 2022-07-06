// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Amazon.Extensions.NETCore.Setup;
using Amazon.S3;
using Amazon.S3.Model;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Storage.Backends
{
	/// <summary>
	/// Exception wrapper for S3 requests
	/// </summary>
	public sealed class AwsException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="message">Message for the exception</param>
		/// <param name="innerException">Inner exception data</param>
		public AwsException(string? message, Exception? innerException)
			: base(message, innerException)
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
		private readonly IAmazonS3 _client;

		/// <summary>
		/// Options for AWs
		/// </summary>
		private readonly IAwsStorageOptions _options;

		/// <summary>
		/// Semaphore for connecting to AWS
		/// </summary>
		private readonly SemaphoreSlim _semaphore;

		/// <summary>
		/// Logger interface
		/// </summary>
		private readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="awsOptions">AWS options</param>
		/// <param name="options">Storage options</param>
		/// <param name="logger">Logger interface</param>
		public AwsStorageBackend(AWSOptions awsOptions, IAwsStorageOptions options, ILogger<AwsStorageBackend> logger)
		{
			_client = awsOptions.CreateServiceClient<IAmazonS3>();
			_options = options;
			_semaphore = new SemaphoreSlim(16);
			_logger = logger;

			logger.LogInformation("Created AWS storage backend for bucket {BucketName} using credentials {Credentials} {CredentialsStr}", options.BucketName, awsOptions.Credentials.GetType(), awsOptions.Credentials.ToString());
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_client.Dispose();
			_semaphore.Dispose();
		}

		class WrappedResponseStream : Stream
		{
			readonly IDisposable _semaphore;
			readonly GetObjectResponse _response;
			readonly Stream _responseStream;

			public WrappedResponseStream(IDisposable semaphore, GetObjectResponse response)
			{
				_semaphore = semaphore;
				_response = response;
				_responseStream = response.ResponseStream;
			}

			public override bool CanRead => true;
			public override bool CanSeek => false;
			public override bool CanWrite => false;
			public override long Length => _responseStream.Length;
			public override long Position { get => _responseStream.Position; set => throw new NotSupportedException(); }

			public override void Flush() => _responseStream.Flush();

			public override int Read(byte[] buffer, int offset, int count) => _responseStream.Read(buffer, offset, count);
			public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) => _responseStream.ReadAsync(buffer, cancellationToken);
			public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken) => _responseStream.ReadAsync(buffer, offset, count, cancellationToken);

			public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
			public override void SetLength(long value) => throw new NotSupportedException();
			public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				_semaphore.Dispose();
				_response.Dispose();
				_responseStream.Dispose();
			}

			public override async ValueTask DisposeAsync()
			{
				await base.DisposeAsync();

				_semaphore.Dispose();
				_response.Dispose();
				await _responseStream.DisposeAsync();
			}
		}

		string GetFullPath(string path)
		{
			if (_options.BucketPath == null)
			{
				return path;
			}

			StringBuilder result = new StringBuilder();
			result.Append(_options.BucketPath);
			if (result.Length > 0 && result[^1] != '/')
			{
				result.Append('/');
			}
			result.Append(path);
			return result.ToString();
		}

		/// <inheritdoc/>
		public async Task<Stream?> ReadAsync(string path, CancellationToken cancellationToken)
		{
			string fullPath = GetFullPath(path);

			IDisposable? semaLock = null;
			GetObjectResponse? response = null;
			try
			{
				semaLock = await _semaphore.UseWaitAsync(cancellationToken);

				GetObjectRequest newGetRequest = new GetObjectRequest();
				newGetRequest.BucketName = _options.BucketName;
				newGetRequest.Key = fullPath;

				response = await _client.GetObjectAsync(newGetRequest, cancellationToken);

				return new WrappedResponseStream(semaLock, response);
			}
			catch (Exception ex)
			{
				_logger.LogWarning(ex, "Unable to read {Path} from S3", fullPath);

				semaLock?.Dispose();
				response?.Dispose();

				return null;
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(string path, Stream inputStream, CancellationToken cancellationToken)
		{
			TimeSpan[] retryTimes =
			{
				TimeSpan.FromSeconds(1.0),
				TimeSpan.FromSeconds(5.0),
				TimeSpan.FromSeconds(10.0),
			};

			string fullPath = GetFullPath(path);
			for (int attempt = 0; ; attempt++)
			{
				try
				{
					using IDisposable semaLock = await _semaphore.UseWaitAsync(cancellationToken);
					await WriteInternalAsync(fullPath, inputStream, cancellationToken);
					_logger.LogDebug("Written data to {Path}", path);
					break;
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Unable to write data to {Path} ({Attempt}/{AttemptCount})", fullPath, attempt + 1, retryTimes.Length + 1);
					if (attempt >= retryTimes.Length)
					{
						throw new AwsException($"Unable to write to bucket {_options.BucketName} path {fullPath}", ex);
					}
				}

				await Task.Delay(retryTimes[attempt]);
			}
		}

		/// <inheritdoc/>
		public async Task WriteInternalAsync(string fullPath, Stream stream, CancellationToken cancellationToken)
		{
			const int MinPartSize = 5 * 1024 * 1024;

			long streamLen = stream.Length;
			if (streamLen < MinPartSize)
			{
				// Read the data into memory. Errors with hash value not matching if we don't do this (?)
				byte[] buffer = new byte[streamLen];
				await ReadExactLengthAsync(stream, buffer, (int)streamLen, cancellationToken);

				// Upload it to S3
				using (MemoryStream inputStream = new MemoryStream(buffer))
				{
					PutObjectRequest uploadRequest = new PutObjectRequest();
					uploadRequest.BucketName = _options.BucketName;
					uploadRequest.Key = fullPath;
					uploadRequest.InputStream = inputStream;
					uploadRequest.Metadata.Add("bytes-written", streamLen.ToString(CultureInfo.InvariantCulture));
					await _client.PutObjectAsync(uploadRequest, cancellationToken);
				}
			}
			else
			{
				// Initiate a multi-part upload
				InitiateMultipartUploadRequest initiateRequest = new InitiateMultipartUploadRequest();
				initiateRequest.BucketName = _options.BucketName;
				initiateRequest.Key = fullPath;

				InitiateMultipartUploadResponse initiateResponse = await _client.InitiateMultipartUploadAsync(initiateRequest, cancellationToken);
				try
				{
					// Buffer for reading the data
					byte[] buffer = new byte[MinPartSize];

					// Upload all the parts
					List<PartETag> partTags = new List<PartETag>();
					for (long streamPos = 0; streamPos < streamLen;)
					{
						// Read the next chunk of data into the buffer
						int bufferLen = (int)Math.Min((long)MinPartSize, streamLen - streamPos);
						await ReadExactLengthAsync(stream, buffer, bufferLen, cancellationToken);
						streamPos += bufferLen;

						// Upload the part
						using (MemoryStream inputStream = new MemoryStream(buffer, 0, bufferLen))
						{
							UploadPartRequest partRequest = new UploadPartRequest();
							partRequest.BucketName = _options.BucketName;
							partRequest.Key = fullPath;
							partRequest.UploadId = initiateResponse.UploadId;
							partRequest.InputStream = inputStream;
							partRequest.PartSize = bufferLen;
							partRequest.PartNumber = partTags.Count + 1;
							partRequest.IsLastPart = (streamPos == streamLen);

							UploadPartResponse partResponse = await _client.UploadPartAsync(partRequest, cancellationToken);
							partTags.Add(new PartETag(partResponse.PartNumber, partResponse.ETag));
						}
					}

					// Mark the upload as complete
					CompleteMultipartUploadRequest completeRequest = new CompleteMultipartUploadRequest();
					completeRequest.BucketName = _options.BucketName;
					completeRequest.Key = fullPath;
					completeRequest.UploadId = initiateResponse.UploadId;
					completeRequest.PartETags = partTags;
					await _client.CompleteMultipartUploadAsync(completeRequest, cancellationToken);
				}
				catch
				{
					// Abort the upload
					AbortMultipartUploadRequest abortRequest = new AbortMultipartUploadRequest();
					abortRequest.BucketName = _options.BucketName;
					abortRequest.Key = fullPath;
					abortRequest.UploadId = initiateResponse.UploadId;
					await _client.AbortMultipartUploadAsync(abortRequest, cancellationToken);

					throw;
				}
			}
		}

		/// <summary>
		/// Reads data of an exact length into a stream
		/// </summary>
		/// <param name="stream">The stream to read from</param>
		/// <param name="buffer">The buffer to read into</param>
		/// <param name="length">Length of the data to read</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Async task</returns>
		static async Task ReadExactLengthAsync(System.IO.Stream stream, byte[] buffer, int length, CancellationToken cancellationToken)
		{
			int bufferPos = 0;
			while (bufferPos < length)
			{
				int bytesRead = await stream.ReadAsync(buffer, bufferPos, length - bufferPos, cancellationToken);
				if (bytesRead == 0)
				{
					throw new InvalidOperationException("Unexpected end of stream");
				}
				bufferPos += bytesRead;
			}
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(string path, CancellationToken cancellationToken)
		{
			DeleteObjectRequest newDeleteRequest = new DeleteObjectRequest();
			newDeleteRequest.BucketName = _options.BucketName;
			newDeleteRequest.Key = GetFullPath(path);
			await _client.DeleteObjectAsync(newDeleteRequest, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task<bool> ExistsAsync(string path, CancellationToken cancellationToken)
		{
			try
			{
				GetObjectMetadataRequest request = new GetObjectMetadataRequest();
				request.BucketName = _options.BucketName;
				request.Key = GetFullPath(path);
				await _client.GetObjectMetadataAsync(request, cancellationToken);
				return true;
			}
			catch (AmazonS3Exception ex) when (ex.StatusCode == System.Net.HttpStatusCode.NotFound)
			{
				return false;
			}
		}
	}
}
