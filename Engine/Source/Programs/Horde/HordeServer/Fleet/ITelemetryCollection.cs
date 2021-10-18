// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using PoolId = StringId<IPool>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Collection of utilization collection
	/// </summary>
	public interface ITelemetryCollection
	{
		/// <summary>
		/// Adds entries for the given utilization
		/// </summary>
		/// <param name="Telemetry">Telemetry data to add</param>
		/// <returns>Async task</returns>
		Task AddUtilizationTelemetryAsync(IUtilizationTelemetry Telemetry);

		/// <summary>
		/// Finds utilization data matching the given criteria
		/// </summary>
		/// <param name="StartTimeUtc">Start time to query utilization for</param>
		/// <param name="FinishTimeUtc">Finish time to query utilization for</param>
		/// <returns>The utilization data</returns>
		Task<List<IUtilizationTelemetry>> GetUtilizationTelemetryAsync(DateTime StartTimeUtc, DateTime FinishTimeUtc);

		/// <summary>
		/// Finds the latest utilization data
		/// </summary>
		/// <returns>The utilization data</returns>
		Task<IUtilizationTelemetry?> GetLatestUtilizationTelemetryAsync();
	}
}
