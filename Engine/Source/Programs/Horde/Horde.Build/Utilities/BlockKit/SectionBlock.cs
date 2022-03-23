// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text.Json;

namespace Horde.Build.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Represents a BlockKit Section block
	/// </summary>
	public class SectionBlock : BlockBase
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="text">Any text to initiale the Section with</param>
		/// <param name="isMarkdown">If true, text is markdown, else text is considered plain text</param>
		/// <param name="emoji">If true, allows escaping of emoji with colon.</param>
		public SectionBlock(string text = "", bool isMarkdown = true, bool emoji = false)
		{
			Text = new TextObject(text, isMarkdown, emoji);
		}

		/// <summary>
		/// The text of the Section block
		/// </summary>
		public TextObject Text { get; }

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, JsonSerializerOptions options)
		{
			writer.WriteStartObject();
			writer.WriteString("type", "section");
			Text.Write(writer);
			writer.WriteEndObject();
		}
	}
}
