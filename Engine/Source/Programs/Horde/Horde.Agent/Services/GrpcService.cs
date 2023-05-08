// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Net.Security;
using Grpc.Net.Client;
using Horde.Agent.Utility;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Services
{
	/// <summary>
	/// Service which creates a configured Grpc channel
	/// </summary>
	class GrpcService
	{
		private readonly ServerProfile _serverProfile;
		private readonly ILogger _logger;
		private readonly ILoggerFactory _loggerFactory;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="settings"></param>
		/// <param name="logger"></param>
		/// <param name="loggerFactory"></param>
		public GrpcService(IOptions<AgentSettings> settings, ILogger<GrpcService> logger, ILoggerFactory loggerFactory)
		{
			_serverProfile = settings.Value.GetCurrentServerProfile();
			_logger = logger;
			_loggerFactory = loggerFactory;
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
#pragma warning disable CA2000 // Dispose objects before losing scope
			// HTTP handler is disposed by GrpcChannel below
			SocketsHttpHandler httpHandler = new()
			{
				KeepAlivePingDelay = TimeSpan.FromSeconds(50),
				SslOptions = new SslClientAuthenticationOptions
				{
					RemoteCertificateValidationCallback = (sender, cert, chain, errors) => CertificateHelper.CertificateValidationCallBack(_logger, sender, cert, chain, errors, _serverProfile)
				}
			};
#pragma warning restore CA2000 // Dispose objects before losing scope

			HttpClient httpClient = new (httpHandler, true);
			httpClient.DefaultRequestHeaders.Add("Accept", "application/json");
			if (bearerToken != null)
			{
				httpClient.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", bearerToken);
			}

			httpClient.Timeout = TimeSpan.FromSeconds(210); // Need to make sure this doesn't cancel any long running gRPC streaming calls (eg. session update)

			_logger.LogInformation("Connecting to rpc server {BaseUrl}", _serverProfile.Url);
			return GrpcChannel.ForAddress(_serverProfile.Url, new GrpcChannelOptions
			{
				// Required payloads coming from CAS service can be large
				MaxReceiveMessageSize = 1024 * 1024 * 1024, // 1 GB
				MaxSendMessageSize = 1024 * 1024 * 1024, // 1 GB
				LoggerFactory = _loggerFactory,
				HttpClient = httpClient,
				DisposeHttpClient = true
			});
		}
	}
}
