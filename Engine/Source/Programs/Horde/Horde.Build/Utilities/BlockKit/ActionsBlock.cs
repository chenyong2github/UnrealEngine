// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;

namespace Horde.Build.Utilities.Slack.BlockKit
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
		public override void Write(Utf8JsonWriter writer, JsonSerializerOptions options)
		{
			writer.WriteStartObject();
			writer.WriteString("type", "actions");
			writer.WriteStartArray("elements");
			foreach (ActionElement element in Elements)
			{
				element.Write(writer, options);
			}
			writer.WriteEndArray();
			writer.WriteEndObject();
		}

		/// <summary>
		/// Helper method to add a new <see cref="ActionButton"/> to the block.
		/// </summary>
		/// <param name="text">The button text.</param>
		/// <param name="url">The URL to navigate to when clicked, if any.</param>
		/// <param name="value">
		/// The value, if any, that should be passed in the interaction payload if the button is clicked.
		/// </param>
		/// <param name="actionId">An optional identifier for handling interaction logic.</param>
		/// <param name="style">An option button style to apply to the button.</param>
		public void AddButton(string text, Uri? url = null, string? value = null, string? actionId = null, ActionButton.ButtonStyle style = ActionButton.ButtonStyle.Default)
		{
			Elements.Add(new ActionButton(text, url, value, actionId, style));
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
		/// <param name="writer"></param>
		/// <param name="options"></param>
		public abstract void Write(Utf8JsonWriter writer, JsonSerializerOptions options);
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
		/// <param name="text">The button text.</param>
		/// <param name="url">The URL to navigate to when clicked, if any.</param>
		/// <param name="value">
		/// The value, if any, that should be passed in the interaction payload if the button is clicked.
		/// </param>
		/// <param name="actionId">An optional identifier for handling interaction logic.</param>
		/// <param name="style">An option button style to apply to the button.</param>
		public ActionButton(string text, Uri? url = null, string? value = null, string? actionId = null, ButtonStyle style = ButtonStyle.Default)
		{
			Text = new TextObject(text, isMarkdown: false);
			Url = url;
			Value = value;
			ActionId = actionId;
			Style = style;
		}

		/// <summary>
		/// The button text.
		/// </summary>
		public TextObject Text { get; } = new TextObject();

		/// <summary>
		/// The URL to navigate to when clicked, if any.
		/// </summary>
		public Uri? Url { get; set; }

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
		public override void Write(Utf8JsonWriter writer, JsonSerializerOptions options)
		{
			writer.WriteStartObject();
			writer.WriteString("type", "button");
			Text.Write(writer);
			if (Url != null)
			{
				writer.WriteString("url", Url.AbsoluteUri);
			}
			if (!String.IsNullOrWhiteSpace(Value))
			{
				writer.WriteString("value", Value);
			}
			if (!String.IsNullOrWhiteSpace(ActionId))
			{
				writer.WriteString("action_id", ActionId);
			}
			if (Style != ButtonStyle.Default)
			{
				writer.WriteString("style", Style.ToString().ToLower(CultureInfo.CurrentCulture));
			}

			writer.WriteEndObject();
		}
	}
}
