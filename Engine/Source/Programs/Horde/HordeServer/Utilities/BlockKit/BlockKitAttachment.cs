// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Converter for overriding default Json serialization/deserialization for BlockKitAttachment objects.
	/// </summary>
	public class BlockKitAttachmentConverter : JsonConverter<BlockKitAttachment>
	{
		/// <summary>
		/// Reads a BlockKitAttachment Json blob and turns it into the appropriate BlockKitAttachment object.
		/// </summary>
		/// <returns>A BlockKitAttachment object of the appropriate type.</returns>
		public override BlockKitAttachment Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => throw new NotImplementedException();

		/// <summary>
		/// Writes the Json for a BlockKitAttachment to the given <see cref="Utf8JsonWriter"/>.
		/// </summary>
		public override void Write(Utf8JsonWriter Writer, BlockKitAttachment Value, JsonSerializerOptions Options)
		{
			Value.Write(Writer, Options);
		}
	}

	/// <summary>
	/// Static helpers for consistent color usage for <see cref="BlockKitMessage"/> messages.
	/// </summary>
	public static class BlockKitAttachmentColors
	{
		/// <summary>
		/// The color to use for error messages.
		/// </summary>
		public static Color Error => Color.FromArgb(236, 76, 71);

		/// <summary>
		/// The color to use for warning messages.
		/// </summary>
		public static Color Warning => Color.FromArgb(247, 209, 84);

		/// <summary>
		/// The color to use for success messages.
		/// </summary>
		public static Color Success => Color.FromArgb(77, 181, 7);
	}

	/// <summary>
	/// An attachment to a block kit message that is indented with a vertical, colored line on the left.
	/// Should only be used for secondary information if possible.
	/// </summary>
	[JsonConverter(typeof(BlockKitAttachmentConverter))]
	public class BlockKitAttachment
	{
		/// <summary>
		/// Used for the "toast" notifications sent by Slack. If not set, and the Text property of the
		/// base message isn't set, the notification will have a message saying it has no preview.
		/// </summary>
		public string? FallbackText { get; set; }

		/// <summary>
		/// The color of the line down the left side of the attachment.
		/// </summary>
		public Color Color { get; set; }

		/// <summary>
		/// A collection of BlockKit blocks the attachment is composed from.
		/// </summary>
		public List<BlockBase> Blocks { get; } = new List<BlockBase>();

		/// <summary>
		/// Write a BlockKit block to a <see cref="Utf8JsonWriter"/>
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Options"></param>
		public void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options)
		{
			Writer.WriteStartObject();
			if (FallbackText != null)
			{
				Writer.WriteString("fallback", FallbackText);
			}
			Writer.WriteString("color", $"#{Color.ToArgb() & 0xFFFFFF:x6}");
			Writer.WriteStartArray("blocks");
			foreach (BlockBase Block in Blocks)
			{
				Block.Write(Writer, Options);
			}
			Writer.WriteEndArray();
			Writer.WriteEndObject();
		}
	}
}
