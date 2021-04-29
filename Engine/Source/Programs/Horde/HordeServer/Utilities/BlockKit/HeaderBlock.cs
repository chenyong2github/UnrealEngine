// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Represents a BlockKit Section block
	/// </summary>
	public class HeaderBlock : BlockBase
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">Any text to initiale the Section with</param>
		/// <param name="IsMarkdown">If true, text is markdown, else text is considered plain text</param>
		/// <param name="Emoji">If true, allows escaping of emojis with colons.</param>
		public HeaderBlock(string Text = "", bool IsMarkdown = true, bool Emoji = false)
		{
			this.Text = new TextObject(Text, IsMarkdown, Emoji);
		}

		/// <summary>
		/// The text of the Section block
		/// </summary>
		public TextObject Text { get; }

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options)
		{
			Writer.WriteStartObject();
			Writer.WriteString("type", "header");
			Text.Write(Writer);
			Writer.WriteEndObject();
		}
	}
}
