// Copyright Epic Games, Inc. All Rights Reserved.

using System.Drawing;
using HordeServer.Utilities.Slack.BlockKit;

namespace HordeServer.Utilities.BlockKit
{
	/// <summary>
	/// Helper methods for creating <see cref="BlockKitMessage"/> responses to send
	/// to users on Slack via the response_url of an interaction payload.
	/// </summary>
	public static class SlackResponses
	{
		/// <summary>
		/// Creates a response to send to Slack about receiving bad data for a request.
		/// </summary>
		/// <param name="Message">A message explaining what was wrong with the data.</param>
		/// <returns>A <see cref="BlockKitMessage"/> to be sent to Slack.</returns>
		public static BlockKitMessage CreateBadDataResponse(string Message)
		{
			BlockKitMessage BlockKitMessage = new BlockKitMessage();
			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.Color = BlockKitAttachmentColors.Error;
			Attachment.Blocks.Add(new SectionBlock("Failed to process request due to bad data from Slack:"));
			Attachment.Blocks.Add(new SectionBlock(Message));
			BlockKitMessage.Attachments.Add(Attachment);
			return BlockKitMessage;
		}

		/// <summary>
		/// Creates a response to send to Slack when failing to assign an issue due to it not
		/// being found in the DB.
		/// </summary>
		/// <returns>A <see cref="BlockKitMessage"/> to be sent to Slack.</returns>
		public static BlockKitMessage CreateIssueNotFoundResponse(int IssueId)
		{
			BlockKitMessage BlockKitMessage = new BlockKitMessage();
			BlockKitAttachment Attachment = new BlockKitAttachment();
			Attachment.Color = BlockKitAttachmentColors.Error;
			Attachment.Blocks.Add(new SectionBlock($"Failed to assign issue as issue {IssueId} could not be found."));
			BlockKitMessage.Attachments.Add(Attachment);
			return BlockKitMessage;
		}
	}
}
