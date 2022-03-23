// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json;

namespace Horde.Build.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Wrapper for a BlockKit Divider block that is used to add a divider between two blocks.
	/// </summary>
	public class DividerBlock : BlockBase
	{
		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, JsonSerializerOptions options)
		{
			writer.WriteStartObject();
			writer.WriteString("type", "divider");
			writer.WriteEndObject();
		}
	}
}
