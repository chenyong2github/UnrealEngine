// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Service
{
	/// <summary>
	/// Uninstalls the service
	/// </summary>
	[Command("Service", "Uninstall", "Uninstalls the service")]
	class UninstallCommand : Command
	{
		/// <summary>
		/// Name of the service
		/// </summary>
		public const string ServiceName = InstallCommand.ServiceName;

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
					else
					{
						Logger.LogInformation("Unable to find service {ServiceName}", ServiceName);
					}
				}
			}
			return Task.FromResult(0);
		}
	}
}
