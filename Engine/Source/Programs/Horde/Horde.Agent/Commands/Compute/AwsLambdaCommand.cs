// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Amazon.SimpleSystemsManagement;
using Amazon.SimpleSystemsManagement.Model;
using EpicGames.Core;
using EpicGames.Horde.Auth;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using Google.Protobuf;
using Horde.Agent.Commands.Compute;
using Horde.Agent.Execution;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using Command = EpicGames.Core.Command;

namespace Horde.Agent.Commands
{
	/// <summary>
	/// Listens continuously for new Lambda invocations and executes them
	/// </summary>
	class AwsLambdaRunner
	{
		// TODO: Rename to AwsLambdaInvoker or Executor?
		
		private readonly AwsLambdaClient _lambdaClient;
		private readonly IStorageClient _storageClient;
		private readonly ILogger<AwsLambdaRunner> _logger;
		private readonly ComputeTaskExecutor _executor;

		public AwsLambdaRunner(AwsLambdaClient lambdaClient, IStorageClient storageClient, ILogger<AwsLambdaRunner> logger)
		{
			_lambdaClient = lambdaClient;
			_storageClient = storageClient;
			_logger = logger;
			_executor = new ComputeTaskExecutor(_storageClient, _logger);
		}

		public async Task<int> ListenForInvocationsAsync(CancellationToken cancellationToken)
		{
			while (!cancellationToken.IsCancellationRequested)
			{
				NextInvocationResponse? nextInvocationResponse = null;
				try
				{
					_logger.LogDebug("Waiting for next invocation...");

					// Block and wait for the next invocation (can be many minutes)
					nextInvocationResponse = await _lambdaClient.GetNextInvocationAsync(cancellationToken);

					AwsLambdaComputeTaskMessage computeTaskMessage;
					// Detect if protobuf is binary or text-encoded JSON. The JSON format is used during testing/debugging.
					bool isProtobufCompatibleJson = nextInvocationResponse.data.Length >= 1 && nextInvocationResponse.data.Span[0] == '{';
					if (isProtobufCompatibleJson)
					{
						computeTaskMessage = AwsLambdaComputeTaskMessage.Parser.ParseJson(Encoding.UTF8.GetString(nextInvocationResponse.data.Span));
					}
					else
					{
						computeTaskMessage = AwsLambdaComputeTaskMessage.Parser.ParseFrom(nextInvocationResponse.data.Span);
					}

					_logger.LogDebug("Invocation received. Lambda request ID {RequestId}", nextInvocationResponse.requestId);

					// Lease ID is not relevant when executing under AWS Lambda, but one is generated to satisfy the ComputeTaskExecutor.
					string leaseId = "aws-lambda-" + Guid.NewGuid();
					string workingDir = Path.GetTempPath();
					
					DirectoryReference leaseDir = DirectoryReference.Combine(new DirectoryReference(workingDir), "Compute", leaseId);
					DirectoryReference.CreateDirectory(leaseDir);

					ComputeTaskMessage task = computeTaskMessage.Task;
					const string msg = "clusterId={ClusterId}\n" +
					             "channelId={ChannelId}\n" +
					             "NamespaceId={NamespaceId}\n" +
					             "DispatchedMs={DispatchedMs}\n" +
					             "InputBucketId={InputBucketId}\n" +
					             "OutputBucketId={OutputBucketId}\n" +
					             "RequirementsHash={RequirementsHash}\n" +
					             "TaskRefId={TaskRefId}\n" +
					             "QueuedAt={QueuedAt}\n";
					_logger.LogDebug(msg, task.ClusterId, task.ChannelId, task.NamespaceId,
						task.DispatchedMs, task.InputBucketId, task.OutputBucketId, task.RequirementsHash,
						task.TaskRefId, task.QueuedAt);
					
					_logger.LogDebug("Lease dir {LeaseDir}", leaseDir.ToString());
					ComputeTaskResultMessage result;
					ComputeTaskExecutor executor = new ComputeTaskExecutor(_storageClient, _logger);
					try
					{
						result = await executor.ExecuteAsync(leaseId, computeTaskMessage.Task, leaseDir, cancellationToken);
					}
					finally
					{
						try
						{
							DirectoryReference.Delete(leaseDir, true);
						}
						catch
						{
							// Ignore any exceptions
						}
					}
					
					_logger.LogDebug("Execution completed. outcome={Outcome} resultRefId={ResultRefId} detail={Detail}", result.Outcome, result.ResultRefId, result.Detail);

					AwsLambdaComputeTaskResultMessage taskResultMessage = new AwsLambdaComputeTaskResultMessage { TaskResult = result };
					byte[] responseData;
					if (isProtobufCompatibleJson)
					{
						string jsonStr = JsonFormatter.Default.Format(taskResultMessage);
						responseData = Encoding.UTF8.GetBytes(jsonStr);
					}
					else
					{
						responseData = taskResultMessage.ToByteArray();
					}

					await _lambdaClient.SendInvocationResponseAsync(nextInvocationResponse.requestId, responseData);
				}
				catch (Exception e)
				{
					_logger.LogError(e, "Exception invoking Lambda function from request");
					
					if (nextInvocationResponse != null)
					{
						try
						{
							await _lambdaClient.SendInvocationErrorAsync(nextInvocationResponse.requestId, "general", e.Message);
						}
						catch (AwsLambdaClientException sendErrorException)
						{
							_logger.LogError(sendErrorException, "Bad response when sending invocation error. isFatal={IsFatal}", sendErrorException.isFatal);
							return 1;
						}
					}
					
					await Task.Delay(500, cancellationToken); // Cool down before trying another next invocation call
				}
			}

			return 0;
		}
	}
	
	/// <summary>
	/// Run agent inside an AWS Lambda function, wrapping the execution of an arbitrary task
	/// </summary>
	[Command("Compute", "AwsLambda", "Listen for AWS Lambda invocations")]
	class AwsLambdaCommand : Command
	{
//		private const string EnvVarHordeStorageUrl = "UE_HORDE_STORAGE_URL";
//		private const string EnvVarHordeStorageOAuthUrl = "UE_HORDE_STORAGE_OAUTH_URL";
//		private const string EnvVarHordeStorageOAuthGrantType = "UE_HORDE_STORAGE_OAUTH_GRANT_TYPE";
//		private const string EnvVarHordeStorageOAuthClientId = "UE_HORDE_STORAGE_OAUTH_CLIENT_ID";
		private const string EnvVarHordeStorageOAuthClientSecretArn = "UE_HORDE_STORAGE_OAUTH_CLIENT_SECRET_ARN";
//		private const string EnvVarHordeStorageOAuthScope = "UE_HORDE_STORAGE_OAUTH_SCOPE";
		
		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			LogLevel logLevel;
			// TODO: Add tracing

			string logLevelStr = Environment.GetEnvironmentVariable("UE_HORDE_LOG_LEVEL") ?? "debug";

			if (Enum.TryParse(logLevelStr, true, out LogLevel logEventLevel))
			{
				logLevel = logEventLevel;
			}
			else
			{
				logger.LogError("Unable to parse log level: {LogLevelStr}", logLevelStr);
				return 1;
			}

			using ILoggerFactory loggerFactory = LoggerFactory.Create(builder => builder.AddConsole().SetMinimumLevel(logLevel));
			ILogger<AwsLambdaClient> lambdaClientLogger = loggerFactory.CreateLogger<AwsLambdaClient>();
			AwsLambdaClient lambdaClient = AwsLambdaClient.InitFromEnv(lambdaClientLogger);
			AwsLambdaRunner lambdaRunner;
			using CancellationTokenSource cts = new CancellationTokenSource();
			
			try
			{
				logger.LogInformation("Initializing AWS Lambda executor...");

				string oAuthClientSecret = await GetOAuthClientSecretAsync(cts.Token);
				HttpServiceClientOptions clientOptions = CreateHttpServiceClientOptionsFromEnv(oAuthClientSecret);
				using HttpClient httpClient = new HttpClient();
				OAuthHandlerFactory oAuthHandlerFactory = new OAuthHandlerFactory(httpClient);
				OAuthHandler<HttpStorageClient> oAuthHandler = oAuthHandlerFactory.Create<HttpStorageClient>(clientOptions);
				using HttpClient client = new HttpClient(oAuthHandler);
				client.BaseAddress = new Uri(clientOptions.Url);
				IStorageClient storageClientManual = new HttpStorageClient(client);

				AppDomain.CurrentDomain.ProcessExit += (s, e) => 
				{
					logger.LogInformation("ProcessExit triggered. Shutting down...");
					cts.Cancel();
				};

				ILogger<AwsLambdaRunner> lambdaRunnerLogger = loggerFactory.CreateLogger<AwsLambdaRunner>();
				lambdaRunner = new AwsLambdaRunner(lambdaClient, storageClientManual, lambdaRunnerLogger);
			}
			catch (Exception e)
			{
				logger.LogError(e, "Error initializing");
				try
				{
					await lambdaClient.SendInitErrorAsync("general", e.Message);
				}
				catch (AwsLambdaClientException initException)
				{
					logger.LogError(initException, "Bad response when sending init error. isFatal={IsFatal}", initException.isFatal);
				}
				
				return 1;
			}
			
			return await lambdaRunner.ListenForInvocationsAsync(cts.Token);
		}

		private static async Task<string> GetOAuthClientSecretAsync(CancellationToken cancellationToken)
		{
			string oAuthClientSecret = GetEnvVar(EnvVarHordeStorageOAuthClientSecretArn);
			using AmazonSimpleSystemsManagementClient ssmClient = new AmazonSimpleSystemsManagementClient();
			GetParameterRequest request = new GetParameterRequest { Name = oAuthClientSecret, WithDecryption = true };
			GetParameterResponse response = await ssmClient.GetParameterAsync(request, cancellationToken);
			return response.Parameter.Value;
		}

		private static HttpServiceClientOptions CreateHttpServiceClientOptionsFromEnv(string clientSecret)
		{
			return new HttpServiceClientOptions
			{
				Url = GetEnvVar("UE_HORDE_STORAGE_URL"),
				AuthUrl = GetEnvVar("UE_HORDE_STORAGE_OAUTH_URL"),
				GrantType = GetEnvVar("UE_HORDE_STORAGE_OAUTH_GRANT_TYPE"),
				ClientId = GetEnvVar("UE_HORDE_STORAGE_OAUTH_CLIENT_ID"),
				ClientSecret = clientSecret,
				Scope = GetEnvVar("UE_HORDE_STORAGE_OAUTH_SCOPE"),
			};
		}

		private static string GetEnvVar(string name)
		{
			string? value = Environment.GetEnvironmentVariable(name);
			if (value == null)
			{
				throw new ArgumentException($"Missing env var {name}");
			}
			return value;
		}
	}
}
