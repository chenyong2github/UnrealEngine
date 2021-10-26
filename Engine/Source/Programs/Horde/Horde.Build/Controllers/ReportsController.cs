// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using EpicGames.Core;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/reports endpoint, used for the reports pages
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public sealed class ReportsController : ControllerBase
	{
		/// <summary>
		/// the Telemetry collection singleton
		/// </summary>
		ITelemetryCollection TelemetryCollection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TelemetryCollection">The telemetry collection</param>
		public ReportsController(ITelemetryCollection TelemetryCollection)
		{
			this.TelemetryCollection = TelemetryCollection;
		}

		/// <summary>
		/// Gets a collection of utilization data including an endpoint and a range
		/// </summary>
		[HttpGet]
		[Route("/api/v1/reports/utilization/{EndDate}")]
		public async Task<ActionResult<List<UtilizationTelemetryResponse>>> GetStreamUtilizationData(DateTime EndDate, [FromQuery(Name = "Range")] int Range, [FromQuery(Name = "TzOffset")] int? TzOffset)
		{
			// Logic here is a bit messy. The client is always passing in a date at midnight
			// If user passes in 12/1/2020 into the date with range of 1, the range should be 12/1/2020:00:00:00 to 12/1/2020:23:59:59
			// Range 2 should be 11/30/2020:00:00:00 to 12/1/2020:23:59:59
			int Offset = TzOffset ?? 0;
			DateTimeOffset EndDateOffset = new DateTimeOffset(EndDate, TimeSpan.FromHours(Offset)).Add(new TimeSpan(23, 59, 59));
			DateTimeOffset StartDateOffset = EndDate.Subtract(new TimeSpan(Range - 1, 0, 0, 0));

			List<IUtilizationTelemetry> Telemetry = await TelemetryCollection.GetUtilizationTelemetryAsync(StartDateOffset.UtcDateTime, EndDateOffset.UtcDateTime);

			return Telemetry.ConvertAll(Telemetry => new UtilizationTelemetryResponse(Telemetry));
		}
	}
}
