// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;

using HordeServer.Models;
using HordeServer.Collections;
using System;
using HordeServer.Utilities;
using System.Threading;

using Microsoft.Extensions.Logging;
using HordeCommon;
using MongoDB.Bson;
using System.Collections.Generic;
using System.Security.Claims;
using System.Diagnostics.CodeAnalysis;
using HordeServer.Notifications;
using System.Linq;
using HordeServer.Api;
using Microsoft.Extensions.Hosting;

namespace HordeServer.Services
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Notice service
	/// </summary>
	public sealed class NoticeService 
	{
		/// <summary>
		/// Log output writer
		/// </summary>
		ILogger<NoticeService> Logger;

		/// <summary>
		/// Notice collection
		/// </summary>
		INoticeCollection Notices;

		/// <summary>
		/// Device service constructor
		/// </summary>
		public NoticeService(INoticeCollection Notices, ILogger<NoticeService> Logger)
		{
			this.Notices = Notices;
            this.Logger = Logger;			
		}

		/// <summary>
		/// Add a notice
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="UserId"></param>
		/// <param name="StartTime"></param>
		/// <param name="FinishTime"></param>
		/// <returns></returns>
		public Task<INotice?> AddNoticeAsync(string Message, UserId? UserId, DateTime? StartTime, DateTime? FinishTime)
		{
			return this.Notices.AddNoticeAsync(Message, UserId, StartTime, FinishTime);
		}

		/// <summary>
		/// Update an existing notice
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Message"></param>
		/// <param name="StartTime"></param>
		/// <param name="FinishTime"></param>
		/// <returns></returns>
		public Task<bool> UpdateNoticeAsync(ObjectId Id, string? Message, DateTime? StartTime, DateTime? FinishTime)
		{
			return this.Notices.UpdateNoticeAsync(Id, Message, StartTime, FinishTime);
		}


		/// <summary>
		/// Get a notice by id
		/// </summary>
		/// <param name="NoticeId"></param>
		/// <returns></returns>
		public Task<INotice?> GetNoticeAsync(ObjectId NoticeId)
		{
			return this.Notices.GetNoticeAsync(NoticeId);
		}

		/// <summary>
		/// Get all notices
		/// </summary>
		/// <returns></returns>
		public Task<List<INotice>> GetNoticesAsync()
		{
			return this.Notices.GetNoticesAsync();
		}

		/// <summary>
		/// Remove an existing notice
		/// </summary>
		/// <param name="Id"></param>
		/// <returns></returns>
		public Task<bool> RemoveNoticeAsync(ObjectId Id)
		{
			return this.Notices.RemoveNoticeAsync(Id);
		}

	}
}