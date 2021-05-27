// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text.Json;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Represents a BlockKit TextObject which is used to present markdown or plain text
	/// </summary>
	public class TextObject
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Text">Any text to initialize with</param>
		/// <param name="IsMarkdown">If true, text is markdown, else text is considered plain text</param>
		/// <param name="Emoji">If true, allows escaping of emojis with a colon</param>
		public TextObject(string Text = "", bool IsMarkdown = true, bool Emoji = false)
		{
			this.Text = Text;
			this.IsMarkdown = IsMarkdown;
			// force emoji to false if markdown is true as this invalidates the schema
			this.Emoji = this.IsMarkdown ? false : Emoji;
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
		/// <param name="Writer"></param>
		public void Write(Utf8JsonWriter Writer)
		{
			Writer.WriteStartObject("text");
			Writer.WriteString("type", IsMarkdown ? "mrkdwn" : "plain_text");

			const int MaxLength = 2048;

			string TruncatedText = Text;
			if (TruncatedText.Length > MaxLength)
			{
				TruncatedText = TruncatedText.Substring(0, MaxLength) + "...\n(message truncated)";
			}

			Writer.WriteString("text", TruncatedText);
			if (Emoji)
			{
				Writer.WriteBoolean("emoji", Emoji);
			}
			Writer.WriteEndObject();
		}
	}
}
