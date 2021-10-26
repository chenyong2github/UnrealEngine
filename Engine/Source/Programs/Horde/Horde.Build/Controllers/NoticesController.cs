// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using TimeZoneConverter;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/status endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class NoticesController : ControllerBase
	{
		/// <summary>
		/// Timezone to use for display notices
		/// </summary>
		TimeZoneInfo TimeZone;

		/// <summary>
		/// The globals singleton
		/// </summary>
		ISingletonDocument<Globals> Globals;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings">The server settings</param>
		/// <param name="Globals">The globals singleton</param>
		public NoticesController(IOptions<ServerSettings> Settings, ISingletonDocument<Globals> Globals)
		{
			string? ScheduleTimeZone = Settings.Value.ScheduleTimeZone;
			this.TimeZone = (ScheduleTimeZone == null) ? TimeZoneInfo.Local : TZConvert.GetTimeZoneInfo(ScheduleTimeZone);

			this.Globals = Globals;
		}

		/// <summary>
		/// Update a status message
		/// </summary>
		/// <param name="Status"></param>
		/// <returns></returns>
		[HttpPost("/api/v1/notices")]
		public async Task<ActionResult> AddNoticeAsync(Notice Status)
		{
			Status.Id = ObjectId.GenerateNewId().ToString();
			await Globals.UpdateAsync(x => x.Notices.Add(Status));
			return Ok();
		}

		/// <summary>
		/// Remove a manually added status message
		/// </summary>
		/// <returns></returns>
		[HttpDelete("/api/v1/notices/{Id}")]
		public async Task<ActionResult> DeleteNoticeAsync(string Id)
		{
			await Globals.UpdateAsync(x => x.Notices.RemoveAll(x => x.Id == Id));
			return Ok();
		}

		/// <summary>
		/// Gets the current status messages
		/// </summary>
		/// <returns>The status messages</returns>
		[HttpGet("/api/v1/notices")]
		public async Task<List<Notice>> GetNoticesAsync()
		{
			Globals Value = await Globals.GetAsync();

			List<Notice> Messages = new List<Notice>(Value.Notices);

			if(Value.ScheduledDowntime.Count > 0)
			{
				DateTimeOffset Now = DateTimeOffset.Now;

				(DateTimeOffset StartTime, DateTimeOffset FinishTime) = Value.ScheduledDowntime[0].GetNext(Now);
				for (int Idx = 1; Idx < Value.ScheduledDowntime.Count; Idx++)
				{
					(DateTimeOffset NextStartTime, DateTimeOffset NextFinishTime) = Value.ScheduledDowntime[Idx].GetNext(Now);
					if (NextFinishTime < StartTime)
					{
						StartTime = NextStartTime;
					}
					if (NextStartTime < FinishTime && NextFinishTime > FinishTime)
					{
						FinishTime = NextFinishTime;
					}
				}

				if (StartTime < Now)
				{
					string FinishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:t}", TimeZoneInfo.ConvertTime(FinishTime, TimeZone));
					Messages.Add(new Notice { StartTime = StartTime.UtcDateTime, FinishTime = FinishTime.UtcDateTime, Message = $"Horde is currently in scheduled downtime. Jobs will resume execution at {FinishTimeString} {TimeZone.Id}." });
				}
				else if(StartTime < Now + TimeSpan.FromHours(12.0))
				{
					string StartTimeString = String.Format(CultureInfo.CurrentCulture, "{0:t}", StartTime);
					string FinishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:t}", FinishTime);
					Messages.Add(new Notice { StartTime = StartTime.UtcDateTime, FinishTime = FinishTime.UtcDateTime, Message = $"Downtime is scheduled from {StartTimeString} to {FinishTimeString} {TimeZone.Id}. No jobs will run during this time." });
				}
			}

			return Messages;
		}
	}
}
