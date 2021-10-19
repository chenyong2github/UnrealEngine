// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeCommon;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using Serilog;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Google.Protobuf.WellKnownTypes;
using Google.Protobuf;

namespace HordeServer.Models
{
	using LeaseId = ObjectId<ILease>;
	using LogId = ObjectId<ILogFile>;
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Document describing a lease. This exists to permanently record a lease; the agent object tracks internal state of any active leases through AgentLease objects.
	/// </summary>
	public interface ILease
	{
		/// <summary>
		/// The unique id of this lease
		/// </summary>
		public LeaseId Id { get; }

		/// <summary>
		/// Name of this lease
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Unique id of the agent 
		/// </summary>
		public AgentId AgentId { get; }

		/// <summary>
		/// Unique id of the agent session
		/// </summary>
		public ObjectId SessionId { get; }

		/// <summary>
		/// The stream this lease belongs to
		/// </summary>
		public StreamId? StreamId { get; }

		/// <summary>
		/// Pool for the work being executed
		/// </summary>
		public PoolId? PoolId { get; }

		/// <summary>
		/// The log for this lease, if applicable
		/// </summary>
		public LogId? LogId { get; }

		/// <summary>
		/// Time at which this lease started
		/// </summary>
		public DateTime StartTime { get; }

		/// <summary>
		/// Time at which this lease completed
		/// </summary>
		public DateTime? FinishTime { get; }

		/// <summary>
		/// Payload for this lease. A packed Google.Protobuf.Any object.
		/// </summary>
		public ReadOnlyMemory<byte> Payload { get; }

		/// <summary>
		/// Outcome of the lease
		/// </summary>
		public LeaseOutcome Outcome { get; }

		/// <summary>
		/// Output from executing the lease
		/// </summary>
		public ReadOnlyMemory<byte> Output { get; }
	}

	/// <summary>
	/// Extension methods for leases
	/// </summary>
	public static class LeaseExtensions
	{
		/// <summary>
		/// Gets the task from a lease, encoded as an Any protobuf object
		/// </summary>
		/// <param name="Lease">The lease to query</param>
		/// <returns>The task definition encoded as a protobuf Any object</returns>
		public static Any GetTask(this ILease Lease)
		{
			return Any.Parser.ParseFrom(Lease.Payload.ToArray());
		}

		/// <summary>
		/// Gets a typed task object from a lease
		/// </summary>
		/// <typeparam name="T">Type of the protobuf message to return</typeparam>
		/// <param name="Lease">The lease to query</param>
		/// <returns>The task definition</returns>
		public static T GetTask<T>(this ILease Lease) where T : IMessage<T>, new()
		{
			return GetTask(Lease).Unpack<T>();
		}
	}
}
