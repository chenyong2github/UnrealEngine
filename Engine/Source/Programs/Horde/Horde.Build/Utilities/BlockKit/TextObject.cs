// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;

namespace Horde.Build.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Represents a BlockKit TextObject which is used to present markdown or plain text
	/// </summary>
	public class TextObject
	{
		/// <summary>
		/// Maximum length of a text object before truncation
		/// </summary>
		public const int MaxLength = 2048;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="text">Any text to initialize with</param>
		/// <param name="isMarkdown">If true, text is markdown, else text is considered plain text</param>
		/// <param name="emoji">If true, allows escaping of emojis with a colon</param>
		public TextObject(string text = "", bool isMarkdown = true, bool emoji = false)
		{
			Text = text;
			IsMarkdown = isMarkdown;
			// force emoji to false if markdown is true as this invalidates the schema
			Emoji = !IsMarkdown && emoji;
		}

		/// <summary>
		/// If true, text is markdown, else text is considered plain text
		/// </summary>
		public bool IsMarkdown { get; set; }

		/// <summary>
		/// If true, allows escaping of emojis with a colon
		/// </summary>
		public bool Emoji { get; set; }

		/// <summary>
		/// The plain text or markdown for this TextObject
		/// </summary>
		public string Text { get; set; }

		/// <summary>
		/// Writes the TextObject JSON to a <see cref="Utf8JsonWriter"/>
		/// </summary>
		/// <param name="writer"></param>
		public void Write(Utf8JsonWriter writer)
		{
			writer.WriteStartObject("text");
			writer.WriteString("type", IsMarkdown ? "mrkdwn" : "plain_text");

			string truncatedText = Text;
			if (truncatedText.Length > MaxLength)
			{
				truncatedText = truncatedText.Substring(0, MaxLength) + "...\n(message truncated)";
			}

			writer.WriteString("text", truncatedText);
			if (Emoji)
			{
				writer.WriteBoolean("emoji", Emoji);
			}
			writer.WriteEndObject();
		}
	}
}
