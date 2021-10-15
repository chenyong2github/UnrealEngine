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

namespace HordeServer.Collections
{
	/// <summary>
	/// Message in an audit log
	/// </summary>
	/// <typeparam name="TTargetId">Type of entity that the log is for</typeparam>
	public interface IAuditLogMessage<TTargetId>
	{
		/// <summary>
		/// Unique id for the entity
		/// </summary>
		public TTargetId TargetId { get; }

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
	/// Concrete implementation of <see cref="IAuditLogMessage{T}"/> used to add new entries
	/// </summary>
	/// <typeparam name="TTargetId"></typeparam>
	public class NewAuditLogMessage<TTargetId> : IAuditLogMessage<TTargetId>
	{
		/// <inheritdoc/>
		public TTargetId TargetId { get; set; }

		/// <inheritdoc/>
		public DateTime TimeUtc { get; set; }

		/// <inheritdoc/>
		public LogLevel Level { get; }

		/// <inheritdoc/>
		public string Data { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public NewAuditLogMessage(TTargetId EntityId, DateTime TimeUtc, LogLevel Level, string Data)
		{
			this.TargetId = EntityId;
			this.TimeUtc = TimeUtc;
			this.Level = Level;
			this.Data = Data;
		}
	}

	/// <summary>
	/// Interface for a collection of log messages for a particular document type
	/// </summary>
	public interface IAuditLog<TTargetId>
	{
		/// <summary>
		/// Adds a message with the given properties
		/// </summary>
		/// <param name="NewMessage">The message to add</param>
		Task AddAsync(NewAuditLogMessage<TTargetId> NewMessage);

		/// <summary>
		/// Finds messages matching certain criteria
		/// </summary>
		/// <param name="Id">Identifier for the </param>
		/// <param name="MinTime"></param>
		/// <param name="MaxTime"></param>
		/// <param name="Index"></param>
		/// <param name="Count"></param>
		/// <returns></returns>
		IAsyncEnumerable<IAuditLogMessage<TTargetId>> FindAsync(TTargetId Id, DateTime? MinTime = null, DateTime? MaxTime = null, int? Index = null, int? Count = null);

		/// <summary>
		/// Deletes messages between a given time range for a particular object
		/// </summary>
		/// <param name="Id">Id of the object</param>
		/// <param name="MinTime">Minimum time to remove</param>
		/// <param name="MaxTime">Maximum time to remove</param>
		/// <returns>Async task</returns>
		Task<long> DeleteAsync(TTargetId Id, DateTime? MinTime = null, DateTime? MaxTime = null);
	}

	/// <summary>
	/// Factory for instantiating audit log instances
	/// </summary>
	/// <typeparam name="TTargetId"></typeparam>
	public interface IAuditLogFactory<TTargetId>
	{
		/// <summary>
		/// Create a new audit log instance, with the given database name
		/// </summary>
		/// <param name="Name"></param>
		/// <returns></returns>
		IAuditLog<TTargetId> Create(string Name);
	}

	/// <summary>
	/// Extension methods for AuditLog instances
	/// </summary>
	public static class AuditLogExtensions
	{
		/// <summary>
		/// Write a message to an audit log
		/// </summary>
		public static Task WriteAsync<TTargetId>(this IAuditLog<TTargetId> AuditLog, TTargetId Id, LogLevel Level, string Format, params object[] Args)
		{
			return WriteAsync(AuditLog, Id, DateTime.UtcNow, Level, new EventId(0), Format, Args);
		}

		/// <summary>
		/// Write a message to an audit log
		/// </summary>
		public static Task WriteAsync<TTargetId>(this IAuditLog<TTargetId> AuditLog, TTargetId Id, LogLevel Level, EventId EventId, string Format, params object[] Args)
		{
			return WriteAsync(AuditLog, Id, DateTime.UtcNow, Level, EventId, Format, Args);
		}

		/// <summary>
		/// Write a message to an audit log
		/// </summary>
		public static Task WriteAsync<TTargetId>(this IAuditLog<TTargetId> AuditLog, TTargetId Id, DateTime TimeUtc, LogLevel Level, EventId EventId, string Format, params object[] Args)
		{
			ArrayBufferWriter<byte> Buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter Writer = new Utf8JsonWriter(Buffer))
			{
				Dictionary<string, object> Properties = new Dictionary<string, object>();
				MessageTemplate.ParsePropertyValues(Format, Args, Properties);
				string Text = MessageTemplate.Render(Format, Properties);
				MessageTemplate.SerializeJson(Writer, TimeUtc, Level, EventId, Format, Properties);
			}

			NewAuditLogMessage<TTargetId> NewMessage = new NewAuditLogMessage<TTargetId>(Id, TimeUtc, Level, Encoding.UTF8.GetString(Buffer.WrittenSpan));
			return AuditLog.AddAsync(NewMessage);
		}
	}
}
