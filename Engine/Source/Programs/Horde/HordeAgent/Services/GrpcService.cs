// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Net.Client;
using HordeAgent.Utility;
using Microsoft.Extensions.Http;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Polly;
using Polly.Extensions.Http;
using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using Build.Bazel.Remote.Execution.V2;
using Grpc.Core;
using HordeCommon.Rpc;

namespace HordeAgent.Services
{
	/// <summary>
	/// Service which creates a configured Grpc channel
	/// </summary>
	class GrpcService
	{
		/// <summary>
		/// Settings for the current server
		/// </summary>
		ServerProfile ServerProfile;

		/// <summary>
		/// Options for the agent
		/// </summary>
		IOptions<AgentSettings> Settings;

		/// <summary>
		/// Logger instance
		/// </summary>
		ILogger Logger;

		internal Func<ChannelBase, ContentAddressableStorage.ContentAddressableStorageClient> CasClientFactory;
		internal Func<ChannelBase, ActionCache.ActionCacheClient> ActionCacheClientFactory;
		internal Func<ChannelBase, ActionRpc.ActionRpcClient> ActionRpcClientFactory;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings"></param>
		/// <param name="Logger"></param>
		public GrpcService(IOptions<AgentSettings> Settings, ILogger<GrpcService> Logger)
		{
			this.Settings = Settings;
			this.ServerProfile = Settings.Value.GetCurrentServerProfile();
			this.Logger = Logger;
			
			CasClientFactory = Channel => new ContentAddressableStorage.ContentAddressableStorageClient(Channel);
			ActionCacheClientFactory = Channel => new ActionCache.ActionCacheClient(Channel);
			ActionRpcClientFactory = Channel => new ActionRpc.ActionRpcClient(Channel);
		}

		/// <summary>
		/// Create a GRPC channel with a default token
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel()
		{
			return CreateGrpcChannel(ServerProfile.Token);
		}
		
		/// <summary>
		/// Create a GRPC channel with the given bearer token
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel(string? BearerToken)
		{
			if (BearerToken == null)
			{
				return CreateGrpcChannel(ServerProfile.Url, null);
			}
			else
			{
				return CreateGrpcChannel(ServerProfile.Url, new AuthenticationHeaderValue("Bearer", BearerToken));
			}
		}

		/// <summary>
		/// Create a GRPC channel with the given auth header value
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel(string Address, AuthenticationHeaderValue? AuthHeaderValue)
		{
			HttpClientHandler CustomCertHandler = new HttpClientHandler();
			CustomCertHandler.ServerCertificateCustomValidationCallback += (Sender, Cert, Chain, Errors) => CertificateHelper.CertificateValidationCallBack(Logger, Sender, Cert, Chain, Errors, ServerProfile);

			TimeSpan[] RetryDelay = { TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(10) };
			IAsyncPolicy<HttpResponseMessage> Policy = HttpPolicyExtensions.HandleTransientHttpError().WaitAndRetryAsync(RetryDelay);
			PolicyHttpMessageHandler RetryHandler = new PolicyHttpMessageHandler(Policy);
			RetryHandler.InnerHandler = CustomCertHandler;

			HttpClient HttpClient = new HttpClient(RetryHandler);
			HttpClient.DefaultRequestHeaders.Add("Accept", "application/json");
			if (AuthHeaderValue != null)
			{
				HttpClient.DefaultRequestHeaders.Authorization = AuthHeaderValue;
			}

			HttpClient.Timeout = TimeSpan.FromSeconds(210); // Need to make sure this doesn't cancel any long running gRPC streaming calls (eg. session update)

			Logger.LogInformation("Connecting to rpc server {BaseUrl}", ServerProfile.Url);
			return GrpcChannel.ForAddress(Address, new GrpcChannelOptions
			{
				// Required payloads coming from CAS service can be large
				MaxReceiveMessageSize = 1024 * 1024 * 1024, // 1 GB
				MaxSendMessageSize = 1024 * 1024 * 1024, // 1 GB
				
				HttpClient = HttpClient,
				DisposeHttpClient = true
			});
		}

		public ContentAddressableStorage.ContentAddressableStorageClient CreateCasClient(ChannelBase Channel)
		{
			return CasClientFactory(Channel);
		}

		public ActionCache.ActionCacheClient CreateActionCacheClient(ChannelBase Channel)
		{
			return ActionCacheClientFactory(Channel);
		}

		public ActionRpc.ActionRpcClient CreateActionRpcClient(ChannelBase Channel)
		{
			return ActionRpcClientFactory(Channel);
		}
	}
}
