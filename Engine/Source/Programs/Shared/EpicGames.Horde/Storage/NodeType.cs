// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Information about a type within a bundle
	/// </summary>
	/// <param name="Guid">Nominal identifier for the type</param>
	/// <param name="Version">Version number for the serializer</param>
	public record struct NodeType(Guid Guid, int Version)
	{
		/// <inheritdoc/>
		public override string ToString() => $"{Guid}#{Version}";
	}
}
