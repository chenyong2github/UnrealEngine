// Copyright Epic Games, Inc. All Rights Reserved.

using System.ComponentModel;
using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Serialization;
using Horde.Build.Utilities;

namespace Horde.Build.Agents.Software
{
	/// <summary>
	/// Identifier for a pool
	/// </summary>
	/// <param name="Id">Id to construct from</param>
	[JsonSchemaString]
	[TypeConverter(typeof(StringIdTypeConverter<AgentSoftwareChannelName, AgentSoftwareChannelNameConverter>))]
	[StringIdConverter(typeof(AgentSoftwareChannelNameConverter))]
	[CbConverter(typeof(StringIdCbConverter<AgentSoftwareChannelName, AgentSoftwareChannelNameConverter>))]
	public record struct AgentSoftwareChannelName(StringId Id)
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public AgentSoftwareChannelName(string id) : this(new StringId(id))
		{
		}

		/// <inheritdoc cref="StringId.IsEmpty"/>
		public bool IsEmpty => Id.IsEmpty;

		/// <inheritdoc/>
		public override string ToString() => Id.ToString();
	}

	/// <summary>
	/// Converter to and from <see cref="StringId"/> instances.
	/// </summary>
	class AgentSoftwareChannelNameConverter : StringIdConverter<AgentSoftwareChannelName>
	{
		/// <inheritdoc/>
		public override AgentSoftwareChannelName FromStringId(StringId id) => new AgentSoftwareChannelName(id);

		/// <inheritdoc/>
		public override StringId ToStringId(AgentSoftwareChannelName value) => value.Id;
	}
}
