// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;

namespace HordeServer.Models
{
	/// <summary>
	/// Information about an agent session.
	/// </summary>
	public interface ISession
	{
		/// <summary>
		/// Unique id for this session
		/// </summary>
		public ObjectId Id { get; }

		/// <summary>
		/// The agent id
		/// </summary>
		public AgentId AgentId { get; }

		/// <summary>
		/// Start time for this session
		/// </summary>
		public DateTime StartTime { get; }

		/// <summary>
		/// Finishing time for this session
		/// </summary>
		public DateTime? FinishTime { get; }

		/// <summary>
		/// Capabilities of this agent at the time the session started
		/// </summary>
		public AgentCapabilities? Capabilities { get; }

		/// <summary>
		/// Version of the agent software
		/// </summary>
		public AgentSoftwareVersion? Version { get; }
	}
}
