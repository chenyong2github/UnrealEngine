// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
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
using System.Threading.Tasks;
using TimeZoneConverter;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/notices endpoint
	/// </summary>
	[ApiController]
	[Authorize]
	[Route("[controller]")]
	public class NoticesController : ControllerBase
	{
		/// <summary>
		/// 
		/// </summary>
		NoticeService NoticeService;

		/// <summary>
		/// The acl service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// Reference to the globals singleton
		/// </summary>
		ISingletonDocument<Globals> Globals;

		/// <summary>
		/// Reference to the user collection
		/// </summary>
		private readonly IUserCollection UserCollection;

		/// <summary>
		/// Server settings
		/// </summary>
		IOptionsMonitor<ServerSettings> Settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Settings">The server settings</param>
		/// <param name="NoticeService">The notice service singleton</param>
		/// <param name="AclService">The acl service singleton</param>
		/// <param name="UserCollection">The user collection singleton</param>
		/// <param name="Globals">The global singleton</param>
		public NoticesController(IOptionsMonitor<ServerSettings> Settings, NoticeService NoticeService, AclService AclService, IUserCollection UserCollection, ISingletonDocument<Globals> Globals)
		{
			this.Settings = Settings;
			this.NoticeService = NoticeService;
			this.AclService = AclService;
			this.UserCollection = UserCollection;
			this.Globals = Globals;
		}

		/// <summary>
		/// Add a status message
		/// </summary>
		/// <param name="Request"></param>
		/// <returns></returns>
		[HttpPost("/api/v1/notices")]
		public async Task<ActionResult> AddNoticeAsync(CreateNoticeRequest Request)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			INotice? Notice = await NoticeService.AddNoticeAsync(Request.Message, User.GetUserId(), Request.StartTime, Request.FinishTime);

			return Notice == null ? NotFound() : Ok();

		}

		/// <summary>
		/// Update a status message
		/// </summary>
		/// <param name="Request"></param>
		/// <returns></returns>
		[HttpPut("/api/v1/notices")]
		public async Task<ActionResult> UpdateNoticeAsync(UpdateNoticeRequest Request)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			return await NoticeService.UpdateNoticeAsync(new ObjectId(Request.Id), Request.Message, Request.StartTime, Request.FinishTime) ? Ok() : NotFound();
		}


		/// <summary>
		/// Remove a manually added status message
		/// </summary>
		/// <returns></returns>
		[HttpDelete("/api/v1/notices/{Id}")]
		public async Task<ActionResult> DeleteNoticeAsync(string Id)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			await NoticeService.RemoveNoticeAsync(new ObjectId(Id));
			
			return Ok();
		}

		/// <summary>
		/// Gets the current status messages
		/// </summary>
		/// <returns>The status messages</returns>
		[HttpGet("/api/v1/notices")]
		public async Task<List<GetNoticeResponse>> GetNoticesAsync()
		{			
			string? ScheduleTimeZone = Settings.CurrentValue.ScheduleTimeZone;
			TimeZoneInfo TimeZone = (ScheduleTimeZone == null) ? TimeZoneInfo.Local : TZConvert.GetTimeZoneInfo(ScheduleTimeZone);
			
			Globals GlobalsValue = await Globals.GetAsync();
			List<GetNoticeResponse> Messages = new List<GetNoticeResponse>();

			// Add system downtime notices
			for (int Idx = 0; Idx < GlobalsValue.ScheduledDowntime.Count; Idx++)
			{
				ScheduledDowntime Downtime = GlobalsValue.ScheduledDowntime[Idx];

				// @todo: handle this, though likely will have a custom message
				if (Downtime.Frequency == ScheduledDowntimeFrequency.Once)
				{
					continue;
				}

				DateTimeOffset Now = TimeZoneInfo.ConvertTime(DateTimeOffset.Now, Settings.CurrentValue.TimeZoneInfo);
				
				// @todo: this time and formatting should be on client
				if (Downtime.IsActive(Now))
				{
					(DateTimeOffset StartTime, DateTimeOffset FinishTime) = Downtime.GetNext(Now);
					string FinishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:t}", TimeZoneInfo.ConvertTime(FinishTime, TimeZone));
					Messages.Add(new GetNoticeResponse() { StartTime = StartTime.UtcDateTime, FinishTime = FinishTime.UtcDateTime, Message = $"Horde is currently in scheduled downtime. Jobs will resume execution at {FinishTimeString} {TimeZone.Id}." });
				}
				else 
				{
					// add a week to get the actual next scheduled time
					if (Downtime.Frequency == ScheduledDowntimeFrequency.Weekly)
					{
						Now += TimeSpan.FromDays(7);
					}

					(DateTimeOffset StartTime, DateTimeOffset FinishTime) = Downtime.GetNext(Now);
					string StartTimeString = String.Format(CultureInfo.CurrentCulture, "{0:g}", StartTime);
					string FinishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:g}", FinishTime);
					Messages.Add(new GetNoticeResponse() { StartTime = StartTime.UtcDateTime, FinishTime = FinishTime.UtcDateTime, Message = $"Downtime is scheduled from {StartTimeString} to {FinishTimeString} {TimeZone.Id}. No jobs will run during this time." });
				}
			}

			// Add user notices
			List<INotice> Notices = await NoticeService.GetNoticesAsync();

			for (int i = 0; i < Notices.Count; i++)
			{
				INotice Notice = Notices[i];
				GetThinUserInfoResponse? UserInfo = null;

				if (Notice.UserId != null)
				{
					UserInfo = new GetThinUserInfoResponse(await UserCollection.GetCachedUserAsync(Notice.UserId));
				}								

				Messages.Add(new GetNoticeResponse(Notice, UserInfo));
			}

			return Messages;
		}
	}
}
