// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Converter for overriding default Json serialization/deserialization for BlockKit blocks.
	/// </summary>
	public class BlockBaseConverter : JsonConverter<BlockBase>
	{
		/// <summary>
		/// Reads a BlockKit block Json blob and turns it into the appropriate BlockKit block object.
		/// </summary>
		/// <returns>A BlockKit block object of the appropriate type.</returns>
		public override BlockBase Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => throw new NotImplementedException();

		/// <summary>
		/// Writes the Json for a BlockKit block to the given <see cref="Utf8JsonWriter"/>.
		/// </summary>
		public override void Write(Utf8JsonWriter Writer, BlockBase Value, JsonSerializerOptions Options)
		{
			Value.Write(Writer, Options);
		}
	}

	/// <summary>
	/// A base class for all BlockKit blocks to derive from.
	/// </summary>
	[JsonConverter(typeof(BlockBaseConverter))]
	public abstract class BlockBase
	{
		/// <summary>
		/// Write a BlockKit block to a <see cref="Utf8JsonWriter"/>
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Options"></param>
		public abstract void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options);
	}
}
