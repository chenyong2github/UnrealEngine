// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information about an agent software channel
	/// </summary>
	public class GetAgentSoftwareChannelResponse
	{
		/// <summary>
		/// Name of the channel
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Name of the user that last modified this channel
		/// </summary>
		public string? ModifiedBy { get; set; }

		/// <summary>
		/// The modified timestamp
		/// </summary>
		public DateTime ModifiedTime { get; set; }

		/// <summary>
		/// Version number of this software
		/// </summary>
		public string? Version { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Channel">The channel information</param>
		public GetAgentSoftwareChannelResponse(IAgentSoftwareChannel Channel)
		{
			this.Name = Channel.Name.ToString();
			this.ModifiedBy = Channel.ModifiedBy;
			this.ModifiedTime = Channel.ModifiedTime;
			this.Version = Channel.Version.ToString();
		}
	}
}
