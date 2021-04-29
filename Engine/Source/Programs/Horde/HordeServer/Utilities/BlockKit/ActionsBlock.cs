// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;

namespace HordeServer.Utilities.Slack.BlockKit
{
	/// <summary>
	/// Wrapper for a BlockKit Actions block that contains interactive elements.
	/// </summary>
	public class ActionsBlock : BlockBase
	{
		/// <summary>
		/// A collection of interactive elements.
		/// </summary>
		public List<ActionElement> Elements { get; } = new List<ActionElement>();

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options)
		{
			Writer.WriteStartObject();
			Writer.WriteString("type", "actions");
			Writer.WriteStartArray("elements");
			foreach (ActionElement Element in Elements)
			{
				Element.Write(Writer, Options);
			}
			Writer.WriteEndArray();
			Writer.WriteEndObject();
		}

		/// <summary>
		/// Helper method to add a new <see cref="ActionButton"/> to the block.
		/// </summary>
		/// <param name="Text">The button text.</param>
		/// <param name="URL">The URL to navigate to when clicked, if any.</param>
		/// <param name="Value">
		/// The value, if any, that should be passed in the interaction payload if the button is clicked.
		/// </param>
		/// <param name="ActionId">An optional identifier for handling interaction logic.</param>
		/// <param name="Style">An option button style to apply to the button.</param>
		public void AddButton(string Text, Uri? URL = null, string? Value = null, string? ActionId = null, ActionButton.ButtonStyle Style = ActionButton.ButtonStyle.Default)
		{
			Elements.Add(new ActionButton(Text, URL, Value, ActionId, Style));
		}
	}

	/// <summary>
	/// A base class for interaction Action elements to derive from.
	/// </summary>
	public abstract class ActionElement
	{
		/// <summary>
		/// Writes the Json for an Action block element to the given <see cref="Utf8JsonWriter"/>.
		/// </summary>
		/// <param name="Writer"></param>
		/// <param name="Options"></param>
		public abstract void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options);
	}

	/// <summary>
	/// An button element that can be added to an <see cref="ActionsBlock"/>.
	/// </summary>
	public class ActionButton : ActionElement
	{
		/// <summary>
		/// Determines the visual color scheme to use for the button.
		/// </summary>
		public enum ButtonStyle
		{
			/// <summary>
			/// The default color scheme.
			/// </summary>
			Default,

			/// <summary>
			/// Used for affirmation/confirmation actions.
			/// </summary>
			Primary,

			/// <summary>
			/// Used when an action is destructive and cannot be undone.
			/// </summary>
			Danger,
		}

		/// <summary>
		/// Construct a new Button action element.
		/// </summary>
		/// <param name="Text">The button text.</param>
		/// <param name="URL">The URL to navigate to when clicked, if any.</param>
		/// <param name="Value">
		/// The value, if any, that should be passed in the interaction payload if the button is clicked.
		/// </param>
		/// <param name="ActionId">An optional identifier for handling interaction logic.</param>
		/// <param name="Style">An option button style to apply to the button.</param>
		public ActionButton(string Text, Uri? URL = null, string? Value = null, string? ActionId = null, ButtonStyle Style = ButtonStyle.Default)
		{
			this.Text = new TextObject(Text, IsMarkdown: false);
			this.URL = URL;
			this.Value = Value;
			this.ActionId = ActionId;
			this.Style = Style;
		}

		/// <summary>
		/// The button text.
		/// </summary>
		public TextObject Text { get; } = new TextObject();

		/// <summary>
		/// The URL to navigate to when clicked, if any.
		/// </summary>
		public Uri? URL { get; set; }

		/// <summary>
		/// The value, if any, that should be passed in the interaction payload if the button is clicked.
		/// </summary>
		public string? Value { get; set; }

		/// <summary>
		/// An optional identifier for handling interaction logic.
		/// </summary>
		public string? ActionId { get; set; }

		/// <summary>
		/// Determines how the button appears in the message.
		/// </summary>
		public ButtonStyle Style { get; set; }

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter Writer, JsonSerializerOptions Options)
		{
			Writer.WriteStartObject();
			Writer.WriteString("type", "button");
			Text.Write(Writer);
			if (URL != null)
			{
				Writer.WriteString("url", URL.AbsoluteUri);
			}
			if (!string.IsNullOrWhiteSpace(Value))
			{
				Writer.WriteString("value", Value);
			}
			if (!string.IsNullOrWhiteSpace(ActionId))
			{
				Writer.WriteString("action_id", ActionId);
			}
			if (Style != ButtonStyle.Default)
			{
				Writer.WriteString("style", Style.ToString().ToLower(CultureInfo.CurrentCulture));
			}

			Writer.WriteEndObject();
		}
	}
}
