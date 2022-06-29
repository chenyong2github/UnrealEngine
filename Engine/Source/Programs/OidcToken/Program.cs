// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System.Linq;
using System.Reflection;
using EpicGames.OIDC;

namespace OidcToken
{
	class Program
	{
		static async Task Main(string[] args)
		{
			if (args.Any(s => s.Equals("--help") || s.Equals("-help")) || args.Length == 0)
			{
				// print help
				Console.WriteLine("Usage: OidcToken --Service <serviceName> [options]");
				Console.WriteLine("Service is a required parameter to indicate which OIDC service you intend to connect to. The connection details of the service is configured in appsettings.json");
				Console.WriteLine();
				Console.WriteLine("Options: ");
				Console.WriteLine(" --Mode [Query/GetToken] - Switch mode to allow you to preview operation without triggering user interaction (result can be used to determine if user interaction is required)");
				Console.WriteLine(" --OutFile <path> - Path to create json file of result");
				Console.WriteLine(" --ResultToConsole [true/false] - If true the resulting json file is output to stdout (and logs are not created)");
				Console.WriteLine(" --Unattended [true/false] - If true we assume no user is present and thus can not rely on their input");
				Console.WriteLine(" --Zen [true/false] - If true the resulting refresh token is posted to Zens token endpoints");
				return;
			}

			string ueRoot = LocateRoot();
			string optionalConfigPath = Path.Combine(ueRoot, "Engine", "Restricted", "NotForLicensees", "Build", "OidcToken", "appsettings.json");

			ConfigurationBuilder configBuilder = new();
			configBuilder.SetBasePath(AppContext.BaseDirectory)
				.AddJsonFile("appsettings.json", false, false)
				.AddJsonFile(
					optionalConfigPath,
					true)
				.AddCommandLine(args);

			IConfiguration config = configBuilder.Build();

			await Host.CreateDefaultBuilder(args)
				.ConfigureAppConfiguration(builder =>
				{
					builder.AddConfiguration(config);
				})
				.ConfigureLogging(loggingBuilder =>
				{
					loggingBuilder.ClearProviders();

					TokenServiceOptions options = new();
					IConfigurationRoot configRoot = configBuilder.Build();
					configRoot.Bind(options);
					if (!options.ResultToConsole)
					{
						loggingBuilder.AddConsole();
					}
				})
				.ConfigureServices(
				(content, services) =>
				{
					IConfiguration configuration = content.Configuration;
					services.AddOptions<TokenServiceOptions>().Bind(configuration).ValidateDataAnnotations();
					services.AddOptions<OidcTokenOptions>().Bind(configuration.GetSection("OidcToken"))
						.ValidateDataAnnotations();

					services.AddSingleton<OidcTokenManager>();
					services.AddTransient<ITokenStore>(TokenStoreFactory.CreateTokenStore);

					services.AddHostedService<TokenService>();
				})
				.RunConsoleAsync();
		}

		static string LocateRoot()
		{
			string? exeLocation = Assembly.GetEntryAssembly()?.Location;

			if (string.IsNullOrEmpty(exeLocation))
			{
				exeLocation = Environment.CurrentDirectory;
			}

			FileInfo exeInfo = new(exeLocation);
			DirectoryInfo? di = exeInfo.Directory;
			DirectoryInfo? root = null;
			while (di != null)
			{
				if (di.EnumerateFiles("GenerateProjectFiles.bat").Any())
				{
					root = di;
					break;
				}

				di = di.Parent!;
			}

			if (root == null)
			{
				throw new Exception($"Unable to find unreal root under: {exeInfo.Directory!.FullName}");
			}
			
			return root.FullName;
		}
	}
}