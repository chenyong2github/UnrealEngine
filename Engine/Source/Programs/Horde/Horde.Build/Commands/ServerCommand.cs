// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Server.Kestrel.Core;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Serilog;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
using System.Threading.Tasks;

namespace Horde.Build.Commands
{
	using ILogger = Microsoft.Extensions.Logging.ILogger;

	[Command("server", "Runs the Horde Build server (default)")]
	class ServerCommand : Command
	{
		ServerSettings HordeSettings;
		IConfiguration Config;
		string[] Args = Array.Empty<string>();

		public ServerCommand(ServerSettings Settings, IConfiguration Config)
		{
			this.HordeSettings = Settings;
			this.Config = Config;
		}

		public override void Configure(CommandLineArguments Arguments, ILogger Logger)
		{
			base.Configure(Arguments, Logger);
			this.Args = Arguments.GetRawArray();
		}

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			using (X509Certificate2? GrpcCertificate = ReadGrpcCertificate(HordeSettings))
			{
				List<IHost> Hosts = new List<IHost>();
				Hosts.Add(CreateHostBuilderWithCert(Args, Config, HordeSettings, GrpcCertificate).Build());
#if WITH_HORDE_STORAGE
				IHostBuilder StorageHostBuilder = Horde.Storage.Program.CreateHostBuilder(Args);
				StorageHostBuilder.ConfigureWebHostDefaults(Builder =>
				{
					Builder.ConfigureKestrel(Options =>
					{
						Options.ListenAnyIP(57000);
						Options.ListenAnyIP(57001, Configure => Configure.UseHttps());
					});
				});
				Hosts.Add(StorageHostBuilder.Build());
#endif
				await Task.WhenAll(Hosts.Select(x => x.RunAsync()));
				return 0;
			}
		}

		// Used by WebApplicationFactory in controller tests. Uses reflection to call this exact function signature.
		public static IHostBuilder CreateHostBuilder(string[] Args)
		{
			ServerSettings HordeSettings = new ServerSettings();
			return CreateHostBuilderWithCert(Args, new ConfigurationBuilder().Build(), HordeSettings, null);
		}

		static IHostBuilder CreateHostBuilderWithCert(string[] Args, IConfiguration Config, ServerSettings ServerSettings, X509Certificate2? SslCert)
		{
			return Host.CreateDefaultBuilder(Args)
				.UseSerilog()
				.ConfigureAppConfiguration(Builder => Builder.AddConfiguration(Config))
				.ConfigureWebHostDefaults(WebBuilder =>
				{
					WebBuilder.ConfigureKestrel(Options =>
					{
						Options.Limits.MaxRequestBodySize = 100 * 1024 * 1024;

						if (ServerSettings.HttpPort != 0)
						{
							Options.ListenAnyIP(ServerSettings.HttpPort, Configure => { Configure.Protocols = HttpProtocols.Http1AndHttp2; });
						}

						if (ServerSettings.HttpsPort != 0)
						{
							Options.ListenAnyIP(ServerSettings.HttpsPort, Configure => { if (SslCert != null) { Configure.UseHttps(SslCert); } });
						}

						// To serve HTTP/2 with gRPC *without* TLS enabled, a separate port for HTTP/2 must be used.
						// This is useful when having a load balancer in front that terminates TLS.
						if (ServerSettings.Http2Port != 0)
						{
							Options.ListenAnyIP(ServerSettings.Http2Port, Configure => { Configure.Protocols = HttpProtocols.Http2; });
						}
					});
					WebBuilder.UseStartup<Startup>();
				});
		}

		/// <summary>
		/// Gets the certificate to use for Grpc endpoints
		/// </summary>
		/// <returns>Custom certificate to use for Grpc endpoints, or null for the default.</returns>
		public static X509Certificate2? ReadGrpcCertificate(ServerSettings HordeSettings)
		{
			string Base64Prefix = "base64:";

			if (HordeSettings.ServerPrivateCert == null)
			{
				return null;
			}
			else if (HordeSettings.ServerPrivateCert.StartsWith(Base64Prefix, StringComparison.Ordinal))
			{
				byte[] CertData = Convert.FromBase64String(HordeSettings.ServerPrivateCert.Replace(Base64Prefix, "", StringComparison.Ordinal));
				return new X509Certificate2(CertData);
			}
			else
			{
				FileReference? ServerPrivateCert = null;

				if (!Path.IsPathRooted(HordeSettings.ServerPrivateCert))
				{
					ServerPrivateCert = FileReference.Combine(Program.AppDir, HordeSettings.ServerPrivateCert);
				}
				else
				{
					ServerPrivateCert = new FileReference(HordeSettings.ServerPrivateCert);
				}

				return new X509Certificate2(FileReference.ReadAllBytes(ServerPrivateCert));
			}
		}
	}
}
