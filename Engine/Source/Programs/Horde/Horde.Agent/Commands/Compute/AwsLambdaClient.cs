// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Compute
{
	/// <summary>
	/// Class representing the HTTP response from next invocation request
	/// </summary>
	public class NextInvocationResponse
	{
		/// <summary>
		/// Lambda request ID
		/// </summary>
		public string requestId { get; }
		
		/// <summary>
		/// Name of the Lambda function being invoked
		/// </summary>
		public string invokedFunctionArn { get; }
		
		/// <summary>
		/// Deadline in milliseconds for completing the function invocation
		/// </summary>
		public long deadlineMs { get; }
		
		/// <summary>
		/// Data
		/// </summary>
		public ReadOnlyMemory<byte> data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="requestId"></param>
		/// <param name="invokedFunctionArn"></param>
		/// <param name="deadlineMs"></param>
		/// <param name="data"></param>
		public NextInvocationResponse(string requestId, string invokedFunctionArn, long deadlineMs, byte[] data)
		{
			this.requestId = requestId;
			this.invokedFunctionArn = invokedFunctionArn;
			this.deadlineMs = deadlineMs;
			this.data = data;
		}
	}

	/// <summary>
	/// Class representing an exception when communicating with the AWS Lambda Runtime API
	/// </summary>
	public class AwsLambdaClientException : Exception
	{
		/// <summary>
		/// Whether the exception is fatal and requires termination of the process (to adhere to Lambda specs)
		/// </summary>
		public bool isFatal { get; }
		
		/// <summary>
		/// HTTP status code in response from AWS Lambda API
		/// </summary>
		public HttpStatusCode? statusCode { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="message"></param>
		/// <param name="statusCode"></param>
		/// <param name="isFatal"></param>
		public AwsLambdaClientException(string message, bool isFatal = false, HttpStatusCode? statusCode = null) : base(message)
		{
			this.isFatal = isFatal;
			this.statusCode = statusCode;
		}
	}

	/// <summary>
	/// Client interfacing with the AWS Lambda runtime API
	/// </summary>
	class AwsLambdaClient
	{
		private const string EnvVarRuntimeApi = "AWS_LAMBDA_RUNTIME_API";
		private const string HeaderNameAwsRequestId = "Lambda-Runtime-Aws-Request-Id";
		private const string HeaderNameDeadlineMs = "Lambda-Runtime-Deadline-Ms";
		private const string HeaderNameInvokedFunctionArn = "Lambda-Runtime-Invoked-Function-Arn";
		private const string HeaderNameFuncErrorType = "Lambda-Runtime-Function-Error-Type";

		private readonly ILogger<AwsLambdaClient> _logger;
		private readonly string _hostPort;

		public AwsLambdaClient(string hostPort, ILogger<AwsLambdaClient> logger)
		{
			_hostPort = hostPort;
			_logger = logger;
		}

		public static AwsLambdaClient InitFromEnv(ILogger<AwsLambdaClient> logger)
		{
			string? runtimeApi = Environment.GetEnvironmentVariable(EnvVarRuntimeApi);
			if (runtimeApi == null)
			{
				throw new ArgumentException($"Env var {EnvVarRuntimeApi} is not set");
			}

			return new AwsLambdaClient(runtimeApi, logger);
		}
		
		private string GetNextInvocationUrl() => $"http://{_hostPort}/2018-06-01/runtime/invocation/next";
		private string GetInvocationResponseUrl(string awsRequestId) => $"http://{_hostPort}/2018-06-01/runtime/invocation/{awsRequestId}/response";
		private string GetInitErrorUrl() => $"http://{_hostPort}/runtime/init/error";
		private string GetInvocationErrorUrl(string awsRequestId) => $"http://{_hostPort}/2018-06-01/runtime/invocation/{awsRequestId}/error";

		private static HttpClient GetHttpClient()
		{
			return new HttpClient();
		}
		
		public async Task<NextInvocationResponse> GetNextInvocationAsync(CancellationToken cancellationToken)
		{
			using HttpClient client = GetHttpClient();
			client.Timeout = TimeSpan.FromHours(1); // Waiting for the next invocation can block for a while
			using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, GetNextInvocationUrl());

			HttpResponseMessage response = await client.SendAsync(request, cancellationToken);

			if (response.StatusCode != HttpStatusCode.OK)
			{
				throw new AwsLambdaClientException("Failed getting next invocation", false, response.StatusCode);
			}
			
			if (!response.Headers.Contains(HeaderNameAwsRequestId))
			{
				throw new AwsLambdaClientException($"Missing response header {HeaderNameAwsRequestId}");
			}
			
			if (!response.Headers.Contains(HeaderNameDeadlineMs))
			{
				throw new AwsLambdaClientException($"Missing response header {HeaderNameDeadlineMs}");
			}
			
			if (!response.Headers.Contains(HeaderNameInvokedFunctionArn))
			{
				throw new AwsLambdaClientException($"Missing response header {HeaderNameInvokedFunctionArn}");
			}

			string requestId = response.Headers.GetValues(HeaderNameAwsRequestId).First();
			long deadlineMs = Convert.ToInt64(response.Headers.GetValues(HeaderNameDeadlineMs).First());
			string invokedFunctionArn = response.Headers.GetValues(HeaderNameInvokedFunctionArn).First();

			byte[] data = await response.Content.ReadAsByteArrayAsync(cancellationToken);
			return new NextInvocationResponse(requestId, invokedFunctionArn, deadlineMs, data);
		}
		
		public async Task SendInvocationResponseAsync(string awsRequestId, ReadOnlyMemory<byte> data)
		{
			using HttpClient client = GetHttpClient();
			client.Timeout = TimeSpan.FromSeconds(30);
			using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, GetInvocationResponseUrl(awsRequestId));
			request.Content = new ByteArrayContent(data.ToArray());
			
			HttpResponseMessage response = await client.SendAsync(request);
			if (response.StatusCode != HttpStatusCode.Accepted)
			{
				_logger.LogError("Failed sending invocation response! RequestId={AwsRequestId} DataLen={DataLen}", awsRequestId, data.Length);
				throw new AwsLambdaClientException("Failed sending invocation response!");
			}
		}
		
		public Task SendInitErrorAsync(string errorType, string errorMessage, List<string>? stackTrace = null)
		{
			return SendErrorAsync(GetInitErrorUrl(), errorType, errorMessage, stackTrace);
		}
		
		public Task SendInvocationErrorAsync(string awsRequestId, string errorType, string errorMessage, List<string>? stackTrace = null)
		{
			return SendErrorAsync(GetInvocationErrorUrl(awsRequestId), errorType, errorMessage, stackTrace);
		}
		
		public static async Task SendErrorAsync(string url, string errorType, string errorMessage, List<string>? stackTrace)
		{
			using HttpClient client = new HttpClient();
			client.Timeout = TimeSpan.FromHours(1);
			using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, url);
			request.Headers.Add(HeaderNameFuncErrorType, errorType);
			string bodyStr = JsonSerializer.Serialize(new
			{
				errorMessage = errorMessage,
				errorType = errorType,
				stackTrace = stackTrace != null ? stackTrace.ToArray() : Array.Empty<string>()
			});
			request.Content = new StringContent(bodyStr);
			
			HttpResponseMessage response = await client.SendAsync(request);
			if (response.StatusCode != HttpStatusCode.Accepted)
			{
				bool isFatal = response.StatusCode == HttpStatusCode.InternalServerError;
				throw new AwsLambdaClientException($"Failed sending initialization error! Status code {response.StatusCode}", isFatal, response.StatusCode);
			}
		}
	}
}
