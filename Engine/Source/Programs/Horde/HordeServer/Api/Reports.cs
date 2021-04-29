// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace HordeServer.Api
{

	/// <summary>
	/// Represents one stream in one pool in one hour of telemetry
	/// </summary>
	public class UtilizationTelemetryStream
	{
		/// <summary>
		/// Stream Id
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Total time
		/// </summary>
		public double Time { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Stream">The stream</param>
		public UtilizationTelemetryStream(IStreamUtilizationTelemetry Stream)
		{
			this.StreamId = Stream.StreamId.ToString();
			this.Time = Stream.Time;
		}
	}

	/// <summary>
	/// Representation of an hour of time
	/// </summary>
	public class UtilizationTelemetryPool
	{
		/// <summary>
		/// Pool id
		/// </summary>
		public string PoolId { get; set; }

		/// <summary>
		/// Number of agents in this pool
		/// </summary>
		public int NumAgents { get; set; }

		/// <summary>
		/// Total time spent doing admin work
		/// </summary>
		public double AdminTime { get; set; }

		/// <summary>
		/// Total time agents in this pool were doing work for other pools
		/// </summary>
		public double OtherTime { get; set; }

		/// <summary>
		/// List of streams
		/// </summary>
		public List<UtilizationTelemetryStream> Streams { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Pool"></param>
		public UtilizationTelemetryPool(IPoolUtilizationTelemetry Pool)
		{
			this.PoolId = Pool.PoolId.ToString();
			this.NumAgents = Pool.NumAgents;
			this.AdminTime = Pool.AdminTime;
			this.OtherTime = Pool.OtherTime;

			this.Streams = Pool.Streams.ConvertAll(Stream => new UtilizationTelemetryStream(Stream));
		}
	}

	/// <summary>
	/// Response data for a utilization request
	/// </summary>
	public class UtilizationTelemetryResponse
	{
		/// <summary>
		/// Start hour
		/// </summary>
		public DateTimeOffset StartTime { get; set; }

		/// <summary>
		/// End hour
		/// </summary>
		public DateTimeOffset FinishTime { get; set; }

		/// <summary>
		/// List of pools
		/// </summary>
		public List<UtilizationTelemetryPool> Pools { get; set; }

		/// <summary>
		/// Total admin time
		/// </summary>
		public double AdminTime { get; set; }

		/// <summary>
		/// Total agents
		/// </summary>
		public int NumAgents { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public UtilizationTelemetryResponse(IUtilizationTelemetry Telemetry)
		{
			this.StartTime = Telemetry.StartTime;
			this.FinishTime = Telemetry.FinishTime;
			this.AdminTime = Telemetry.AdminTime;
			this.NumAgents = Telemetry.NumAgents;

			this.Pools = Telemetry.Pools.ConvertAll(Pool => new UtilizationTelemetryPool(Pool));
		}
	}

	
}
