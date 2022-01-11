// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// A collection of user specified notices
	/// </summary>
	public interface INoticeCollection
	{
		/// <summary>
		/// Add a notice to the collection
		/// </summary>
		/// <param name="Message"></param>
		/// <param name="UserId"></param>
		/// <param name="StartTime"></param>
		/// <param name="FinishTime"></param>
		/// <returns></returns>
		Task<INotice?> AddNoticeAsync(string Message, UserId? UserId, DateTime? StartTime, DateTime? FinishTime);

		/// <summary>
		/// Update an existing notice
		/// </summary>
		/// <param name="Id"></param>
		/// <param name="Message"></param>
		/// <param name="StartTime"></param>
		/// <param name="FinishTime"></param>
		/// <returns></returns>
		Task<bool> UpdateNoticeAsync(ObjectId Id, string? Message, DateTime? StartTime, DateTime? FinishTime);

		/// <summary>
		/// Get a notice by id
		/// </summary>
		/// <param name="NoticeId"></param>
		/// <returns></returns>
		Task<INotice?> GetNoticeAsync(ObjectId NoticeId);

		/// <summary>
		/// Get all notices
		/// </summary>
		/// <returns></returns>
		Task<List<INotice>> GetNoticesAsync();

		/// <summary>
		/// Remove a notice
		/// </summary>
		/// <param name="Id"></param>
		/// <returns></returns>
		Task<bool> RemoveNoticeAsync(ObjectId Id);


	}

}