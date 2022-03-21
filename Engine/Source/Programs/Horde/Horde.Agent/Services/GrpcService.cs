// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Net.Client;
using Horde.Agent.Utility;
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
using Grpc.Core;
using HordeCommon.Rpc;

namespace Horde.Agent.Services
{
	/// <summary>
	/// Service which creates a configured Grpc channel
	/// </summary>
	class GrpcService
	{
		private readonly ServerProfile _serverProfile;
		private readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="settings"></param>
		/// <param name="logger"></param>
		public GrpcService(IOptions<AgentSettings> settings, ILogger<GrpcService> logger)
		{
			_serverProfile = settings.Value.GetCurrentServerProfile();
			_logger = logger;
		}

		/// <summary>
		/// Create a GRPC channel with a default token
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel()
		{
			return CreateGrpcChannel(_serverProfile.Token);
		}
		
		/// <summary>
		/// Create a GRPC channel with the given bearer token
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel(string? bearerToken)
		{
			if (bearerToken == null)
			{
				return CreateGrpcChannel(_serverProfile.Url, null);
			}
			else
			{
				return CreateGrpcChannel(_serverProfile.Url, new AuthenticationHeaderValue("Bearer", bearerToken));
			}
		}

		/// <summary>
		/// Create a GRPC channel with the given auth header value
		/// </summary>
		/// <returns>New grpc channel</returns>
		public GrpcChannel CreateGrpcChannel(string address, AuthenticationHeaderValue? authHeaderValue)
		{
			HttpClientHandler customCertHandler = new HttpClientHandler();
			customCertHandler.ServerCertificateCustomValidationCallback += (sender, cert, chain, errors) => CertificateHelper.CertificateValidationCallBack(_logger, sender, cert, chain, errors, _serverProfile);

			TimeSpan[] retryDelay = { TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(10) };
			IAsyncPolicy<HttpResponseMessage> policy = HttpPolicyExtensions.HandleTransientHttpError().WaitAndRetryAsync(retryDelay);
			PolicyHttpMessageHandler retryHandler = new PolicyHttpMessageHandler(policy);
			retryHandler.InnerHandler = customCertHandler;

			HttpClient httpClient = new HttpClient(retryHandler);
			httpClient.DefaultRequestHeaders.Add("Accept", "application/json");
			if (authHeaderValue != null)
			{
				httpClient.DefaultRequestHeaders.Authorization = authHeaderValue;
			}

			httpClient.Timeout = TimeSpan.FromSeconds(210); // Need to make sure this doesn't cancel any long running gRPC streaming calls (eg. session update)

			_logger.LogInformation("Connecting to rpc server {BaseUrl}", _serverProfile.Url);
			return GrpcChannel.ForAddress(address, new GrpcChannelOptions
			{
				// Required payloads coming from CAS service can be large
				MaxReceiveMessageSize = 1024 * 1024 * 1024, // 1 GB
				MaxSendMessageSize = 1024 * 1024 * 1024, // 1 GB
				
				HttpClient = httpClient,
				DisposeHttpClient = true
			});
		}
	}
}
