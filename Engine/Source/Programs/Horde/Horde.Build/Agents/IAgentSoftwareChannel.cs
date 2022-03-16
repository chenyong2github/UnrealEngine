// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Api;
using Horde.Build.Collections;
using Horde.Build.Services;
using Horde.Build.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace Horde.Build.Models
{
	using AgentSoftwareVersion = StringId<IAgentSoftwareCollection>;
	using AgentSoftwareChannelName = StringId<AgentSoftwareChannels>;

	/// <summary>
	/// A software channel
	/// </summary>
	public interface IAgentSoftwareChannel
	{
		/// <summary>
		/// The channel id
		/// </summary>
		public AgentSoftwareChannelName Name { get; set; }

		/// <summary>
		/// Name of the user that made the last modification
		/// </summary>
		public string? ModifiedBy { get; set; }

		/// <summary>
		/// Last modification time
		/// </summary>
		public DateTime ModifiedTime { get; set; }

		/// <summary>
		/// The software version number
		/// </summary>
		public string Version { get; set; }
	}
}
