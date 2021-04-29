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

using PoolId = HordeServer.Utilities.StringId<HordeServer.Models.IPool>;
using StreamId = HordeServer.Utilities.StringId<HordeServer.Models.IStream>;

namespace HordeServer.Models
{
	/// <summary>
	/// Document describing a lease. This exists to permanently record a lease; the agent object tracks internal state of any active leases through AgentLease objects.
	/// </summary>
	public interface ILease
	{
		/// <summary>
		/// The unique id of this lease
		/// </summary>
		public ObjectId Id { get; }

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
		public ObjectId? LogId { get; }

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
	}
}
