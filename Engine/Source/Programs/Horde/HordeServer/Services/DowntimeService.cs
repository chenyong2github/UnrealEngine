// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Runtime.Internal.Util;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Interface for a service which keeps track of whether we're during downtime
	/// </summary>
	public interface IDowntimeService
	{
		/// <summary>
		/// Returns true if downtime is currently active
		/// </summary>
		public bool IsDowntimeActive
		{
			get;
		}
	}

	/// <summary>
	/// Service which manages the downtime schedule
	/// </summary>
	public class DowntimeService : TickedBackgroundService, IDowntimeService
	{
		/// <summary>
		/// Whether the server is currently in downtime
		/// </summary>
		public bool IsDowntimeActive
		{
			get;
			private set;
		}

		DatabaseService DatabaseService;
		IOptionsMonitor<ServerSettings> Settings;
		ILogger<DowntimeService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		/// <param name="Settings">The server settings</param>
		/// <param name="Logger">Logger instance</param>
		public DowntimeService(DatabaseService DatabaseService, IOptionsMonitor<ServerSettings> Settings, ILogger<DowntimeService> Logger)
			: base(TimeSpan.FromMinutes(1.0), Logger)
		{
			this.DatabaseService = DatabaseService;
			this.Settings = Settings;
			this.Logger = Logger;

			// Ensure the initial value to be correct
			TickAsync(CancellationToken.None).Wait();
		}

		/// <summary>
		/// Periodically called tick function
		/// </summary>
		/// <param name="StoppingToken">Token indicating that the service should stop</param>
		/// <returns>Async task</returns>
		protected override async Task TickAsync(CancellationToken StoppingToken)
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();

			DateTimeOffset Now = TimeZoneInfo.ConvertTime(DateTimeOffset.Now, Settings.CurrentValue.TimeZoneInfo);
			bool bIsActive = Globals.ScheduledDowntime.Any(x => x.IsActive(Now));

			DateTimeOffset? Next = null;
			foreach (ScheduledDowntime Schedule in Globals.ScheduledDowntime)
			{
				DateTimeOffset Start = Schedule.GetNext(Now).StartTime;
				if(Next == null || Start < Next)
				{
					Next = Start;
				}
			}

			if (Next != null)
			{
				Logger.LogInformation("Server time: {Time}. Downtime: {Downtime}. Next: {Next}.", Now, bIsActive, Next.Value);
			}

			if (bIsActive != IsDowntimeActive)
			{
				IsDowntimeActive = bIsActive;
				if (IsDowntimeActive)
				{
					Logger.LogInformation("Entering downtime");
				}
				else
				{
					Logger.LogInformation("Leaving downtime");
				}
			}
		}
	}
}
