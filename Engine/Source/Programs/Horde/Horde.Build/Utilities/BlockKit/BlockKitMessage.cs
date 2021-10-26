// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace HordeServer.Utilities.Slack.BlockKit
{
    /// <summary>
    /// An entire BlockKit message to be presented in Slack.
    /// </summary>
    public class BlockKitMessage
    {
		/// <summary>
		/// The channel ID or user ID to deliver the notification to.
		/// </summary>
		[JsonPropertyName("channel")]
		public string? Channel { get; set; }

		/// <summary>
		/// Timestamp of the message to update
		/// </summary>
		[JsonPropertyName("ts")]
		public string? Ts { get; set; }

		/// <summary>
		/// If no blocks are provided, this text will be displayed. This is also what is used on the pop up notification.
		/// </summary>
		[JsonPropertyName("text")]
		public string? Text { get; set; }

        /// <summary>
        /// A collection of BlockKit blocks the message is composed from.
        /// </summary>
        [JsonPropertyName("blocks")]
        public List<BlockBase> Blocks { get; } = new List<BlockBase>();

        /// <summary>
        /// Optional value that determines if Slack should try to replace the original message for the command that
        /// this message is associated with.
        /// </summary>
        [JsonPropertyName("replace_original")]
        public bool? ReplaceOriginal { get; set; }

        /// <summary>
        /// Optional value that determines if Slack should try to replace to delete original message for the command
        /// that this message is associated with.
        /// </summary>
        [JsonPropertyName("delete_original")]
        public bool? DeleteOriginal { get; set; }

		/// <summary>
		/// A collection of attachments.
		/// NOTE: This feature is not supported any more but is not deprecated.
		/// </summary>
		[JsonPropertyName("attachments")]
		public List<BlockKitAttachment> Attachments { get; } = new List<BlockKitAttachment>();
    }
}
