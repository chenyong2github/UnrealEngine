// Copyright Epic Games, Inc. All Rights Reserved.

namespace Horde.Build.Api
{
	/// <summary>
	/// Setting information required by dashboard
	/// </summary>
	public class GetDashboardConfigResponse
	{
		/// <summary>
		/// The name of the external issue service
		/// </summary>
		public string? ExternalIssueServiceName { get; set; }

		/// <summary>
		/// The url of the external issue service
		/// </summary>
		public string? ExternalIssueServiceUrl { get; set; }

		/// <summary>
		/// The url of the perforce swarm installation
		/// </summary>
		public string? PerforceSwarmUrl { get; set; }

		/// <summary>
		/// Response constructor
		/// </summary>
		public GetDashboardConfigResponse()
		{
		}

	}
}

