// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading.Tasks;
using Horde.Build.Acls;
using Horde.Build.Api;
using Horde.Build.Collections;
using Horde.Build.Models;
using Horde.Build.Services;
using Horde.Build.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using TimeZoneConverter;

namespace Horde.Build.Controllers
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
		readonly NoticeService _noticeService;

		/// <summary>
		/// The acl service singleton
		/// </summary>
		readonly AclService _aclService;

		/// <summary>
		/// Reference to the globals singleton
		/// </summary>
		readonly ISingletonDocument<Globals> _globals;

		/// <summary>
		/// Reference to the user collection
		/// </summary>
		private readonly IUserCollection _userCollection;

		/// <summary>
		/// Server settings
		/// </summary>
		readonly IOptionsMonitor<ServerSettings> _settings;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="settings">The server settings</param>
		/// <param name="noticeService">The notice service singleton</param>
		/// <param name="aclService">The acl service singleton</param>
		/// <param name="userCollection">The user collection singleton</param>
		/// <param name="globals">The global singleton</param>
		public NoticesController(IOptionsMonitor<ServerSettings> settings, NoticeService noticeService, AclService aclService, IUserCollection userCollection, ISingletonDocument<Globals> globals)
		{
			_settings = settings;
			_noticeService = noticeService;
			_aclService = aclService;
			_userCollection = userCollection;
			_globals = globals;
		}

		/// <summary>
		/// Add a status message
		/// </summary>
		/// <param name="request"></param>
		/// <returns></returns>
		[HttpPost("/api/v1/notices")]
		public async Task<ActionResult> AddNoticeAsync(CreateNoticeRequest request)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			INotice? notice = await _noticeService.AddNoticeAsync(request.Message, User.GetUserId(), request.StartTime, request.FinishTime);

			return notice == null ? NotFound() : Ok();

		}

		/// <summary>
		/// Update a status message
		/// </summary>
		/// <param name="request"></param>
		/// <returns></returns>
		[HttpPut("/api/v1/notices")]
		public async Task<ActionResult> UpdateNoticeAsync(UpdateNoticeRequest request)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			return await _noticeService.UpdateNoticeAsync(new ObjectId(request.Id), request.Message, request.StartTime, request.FinishTime) ? Ok() : NotFound();
		}

		/// <summary>
		/// Remove a manually added status message
		/// </summary>
		/// <returns></returns>
		[HttpDelete("/api/v1/notices/{id}")]
		public async Task<ActionResult> DeleteNoticeAsync(string id)
		{
			if (!await _aclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			await _noticeService.RemoveNoticeAsync(new ObjectId(id));
			
			return Ok();
		}

		/// <summary>
		/// Gets the current status messages
		/// </summary>
		/// <returns>The status messages</returns>
		[HttpGet("/api/v1/notices")]
		public async Task<List<GetNoticeResponse>> GetNoticesAsync()
		{			
			string? scheduleTimeZone = _settings.CurrentValue.ScheduleTimeZone;
			TimeZoneInfo timeZone = (scheduleTimeZone == null) ? TimeZoneInfo.Local : TZConvert.GetTimeZoneInfo(scheduleTimeZone);
			
			Globals globalsValue = await _globals.GetAsync();
			List<GetNoticeResponse> messages = new List<GetNoticeResponse>();

			// Add system downtime notices
			for (int idx = 0; idx < globalsValue.ScheduledDowntime.Count; idx++)
			{
				ScheduledDowntime downtime = globalsValue.ScheduledDowntime[idx];

				// @todo: handle this, though likely will have a custom message
				if (downtime.Frequency == ScheduledDowntimeFrequency.Once)
				{
					continue;
				}

				DateTimeOffset now = TimeZoneInfo.ConvertTime(DateTimeOffset.Now, _settings.CurrentValue.TimeZoneInfo);
				
				// @todo: this time and formatting should be on client
				if (downtime.IsActive(now))
				{
					(DateTimeOffset startTime, DateTimeOffset finishTime) = downtime.GetNext(now);
					string finishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:t}", TimeZoneInfo.ConvertTime(finishTime, timeZone));
					messages.Add(new GetNoticeResponse() { StartTime = startTime.UtcDateTime, FinishTime = finishTime.UtcDateTime, Message = $"Horde is currently in scheduled downtime. Jobs will resume execution at {finishTimeString} {timeZone.Id}." });
				}
				else 
				{
					// add a week to get the actual next scheduled time
					if (downtime.Frequency == ScheduledDowntimeFrequency.Weekly)
					{
						now += TimeSpan.FromDays(7);
					}

					(DateTimeOffset startTime, DateTimeOffset finishTime) = downtime.GetNext(now);
					string startTimeString = String.Format(CultureInfo.CurrentCulture, "{0:g}", startTime);
					string finishTimeString = String.Format(CultureInfo.CurrentCulture, "{0:g}", finishTime);
					messages.Add(new GetNoticeResponse() { StartTime = startTime.UtcDateTime, FinishTime = finishTime.UtcDateTime, Message = $"Downtime is scheduled from {startTimeString} to {finishTimeString} {timeZone.Id}. No jobs will run during this time." });
				}
			}

			// Add user notices
			List<INotice> notices = await _noticeService.GetNoticesAsync();

			for (int i = 0; i < notices.Count; i++)
			{
				INotice notice = notices[i];
				GetThinUserInfoResponse? userInfo = null;

				if (notice.UserId != null)
				{
					userInfo = new GetThinUserInfoResponse(await _userCollection.GetCachedUserAsync(notice.UserId));
				}								

				messages.Add(new GetNoticeResponse(notice, userInfo));
			}

			return messages;
		}
	}
}
