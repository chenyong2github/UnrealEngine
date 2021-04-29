// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Wrapper for a BlockKit Divider block that is used to add a divider between two blocks.
	/// </summary>
	public class DividerBlock : BlockBase
	{
		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options)
		{
			Writer.WriteStartObject();
			Writer.WriteString("type", "divider");
			Writer.WriteEndObject();
		}
	}
}
