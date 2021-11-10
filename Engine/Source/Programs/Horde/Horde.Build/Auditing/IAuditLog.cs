// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf.WellKnownTypes;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeCommon.Rpc.Tasks;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using EpicGames.Core;
using System.Buffers;
using System.Text.Json;
using System.Text;
using System.IO.Pipelines;

namespace HordeServer.Collections
{
	/// <summary>
	/// Message from an audit log
	/// </summary>
	public interface IAuditLogMessage
	{
		/// <summary>
		/// Timestamp for the event
		/// </summary>
		public DateTime TimeUtc { get; }

		/// <summary>
		/// Severity of the message
		/// </summary>
		public LogLevel Level { get; }

		/// <summary>
		/// The message payload. Should be an encoded JSON object, with format/properties fields.
		/// </summary>
		public string Data { get; }
	}

	/// <summary>
	/// Channel for a particular entity
	/// </summary>
	public interface IAuditLogChannel : ILogger
	{
		/// <summary>
		/// Finds messages matching certain criteria
		/// </summary>
		/// <param name="MinTime"></param>
		/// <param name="MaxTime"></param>
		/// <param name="Index"></param>
		/// <param name="Count"></param>
		/// <returns></returns>
		IAsyncEnumerable<IAuditLogMessage> FindAsync(DateTime? MinTime = null, DateTime? MaxTime = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Deletes messages between a given time range for a particular object
		/// </summary>
		/// <param name="MinTime">Minimum time to remove</param>
		/// <param name="MaxTime">Maximum time to remove</param>
		/// <returns>Async task</returns>
		Task<long> DeleteAsync(DateTime? MinTime = null, DateTime? MaxTime = null);
	}

	/// <summary>
	/// Channel for a particular entity
	/// </summary>
	public interface IAuditLogChannel<TSubject> : IAuditLogChannel
	{
		/// <summary>
		/// Identifier for the subject of this channel
		/// </summary>
		TSubject Subject { get; }
	}

	/// <summary>
	/// Message in an audit log
	/// </summary>
	/// <typeparam name="TTargetId">Type of entity that the log is for</typeparam>
	public interface IAuditLogMessage<TTargetId> : IAuditLogMessage
	{
		/// <summary>
		/// Unique id for the entity
		/// </summary>
		public TTargetId Subject { get; }
	}

	/// <summary>
	/// Interface for a collection of log messages for a particular document type
	/// </summary>
	public interface IAuditLog<TSubject>
	{
		/// <summary>
		/// Get the channel for a particular subject
		/// </summary>
		/// <param name="Subject"></param>
		/// <returns></returns>
		IAuditLogChannel<TSubject> this[TSubject Subject] { get; }
	}

	/// <summary>
	/// Factory for instantiating audit log instances
	/// </summary>
	/// <typeparam name="TSubject"></typeparam>
	public interface IAuditLogFactory<TSubject>
	{
		/// <summary>
		/// Create a new audit log instance, with the given database name
		/// </summary>
		/// <param name="CollectionName"></param>
		/// <param name="SubjectProperty"></param>
		/// <returns></returns>
		IAuditLog<TSubject> Create(string CollectionName, string SubjectProperty);
	}

	/// <summary>
	/// Extension methods for audit log channels
	/// </summary>
	static class AuditLogExtensions
	{
		/// <summary>
		/// Retrieve historical information about a specific agent
		/// </summary>
		/// <param name="Channel">Channel to query</param>
		/// <param name="BodyWriter">Writer for Json data</param>
		/// <param name="MinTime">Minimum time for records to return</param>
		/// <param name="MaxTime">Maximum time for records to return</param>
		/// <param name="Index">Offset of the first result</param>
		/// <param name="Count">Number of records to return</param>
		/// <returns>Information about the requested agent</returns>
		public static async Task FindAsync<T>(this IAuditLogChannel<T> Channel, PipeWriter BodyWriter, DateTime? MinTime = null, DateTime? MaxTime = null, int Index = 0, int Count = 50)
		{
			string Prefix = "{\n\t\"entries\":\n\t[";
			await BodyWriter.WriteAsync(Encoding.UTF8.GetBytes(Prefix));

			string Separator = "";
			await foreach (IAuditLogMessage<T> Message in Channel.FindAsync(MinTime, MaxTime, Index, Count))
			{
				string Line = $"{Separator}\n\t\t{Message.Data}";
				await BodyWriter.WriteAsync(Encoding.UTF8.GetBytes(Line));
				Separator = ",";
			}

			string Suffix = "\n\t]\n}";
			await BodyWriter.WriteAsync(Encoding.UTF8.GetBytes(Suffix));
		}
	}
}
