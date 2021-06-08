// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

namespace HordeAgent.Commands.Service
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Service", "Install", "Installs the agent as a service")]
	class InstallCommand : Command
	{
		/// <summary>
		/// Name of the service
		/// </summary>
		public const string ServiceName = "HordeAgent";

		/// <summary>
		/// Specifies the username for the service to run under
		/// </summary>
		[CommandLine("-UserName=")]
		public string? UserName = null;

		/// <summary>
		/// Password for the username
		/// </summary>
		[CommandLine("-Password=")]
		public string? Password = null;

		/// <summary>
		/// The server profile to use
		/// </summary>
		[CommandLine("-Server=")]
		public string? Server = null;
		
		/// <summary>
		/// Path to dotnet executable (dotnet.exe on Windows)
		/// When left empty, the value of "dotnet" will be used.
		/// </summary>
		[CommandLine("-DotNetExecutable=")]
		public string DotNetExecutable = "dotnet";

		/// <summary>
		/// Runs the service indefinitely
		/// </summary>
		/// <param name="Logger">Logger to use</param>
		/// <returns>Exit code</returns>
		public override Task<int> ExecuteAsync(ILogger Logger)
		{
			using (WindowsServiceManager ServiceManager = new WindowsServiceManager())
			{
				using (WindowsService Service = ServiceManager.Open(ServiceName))
				{
					if (Service.IsValid)
					{
						Logger.LogInformation("Stopping existing service...");
						Service.Stop();

						WindowsServiceStatus Status = Service.WaitForStatusChange(WindowsServiceStatus.Stopping, TimeSpan.FromSeconds(30.0));
						if (Status != WindowsServiceStatus.Stopped)
						{
							Logger.LogError("Unable to stop service (status = {Status})", Status);
							return Task.FromResult(1);
						}

						Logger.LogInformation("Deleting service");
						Service.Delete();
					}
				}

				Logger.LogInformation("Registering {ServiceName} service", ServiceName);

				StringBuilder CommandLine = new StringBuilder();
				CommandLine.AppendFormat("{0} \"{1}\" service run", DotNetExecutable, Assembly.GetEntryAssembly()!.Location);
				if(Server != null)
				{
					CommandLine.Append($" -server={Server}");
				}

				using (WindowsService Service = ServiceManager.Create(ServiceName, "Horde Agent", CommandLine.ToString(), UserName, Password))
				{
					Service.SetDescription("Allows this machine to participate in a Horde farm.");

					Logger.LogInformation("Starting...");
					Service.Start();

					WindowsServiceStatus Status = Service.WaitForStatusChange(WindowsServiceStatus.Starting, TimeSpan.FromSeconds(30.0));
					if (Status != WindowsServiceStatus.Running)
					{
						Logger.LogError("Unable to start service (status = {Status})", Status);
						return Task.FromResult(1);
					}

					Logger.LogInformation("Done.");
				}
			}
			return Task.FromResult(0);
		}
	}
}
