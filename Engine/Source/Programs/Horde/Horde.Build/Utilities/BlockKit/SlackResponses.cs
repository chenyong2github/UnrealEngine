// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Utilities.Slack.BlockKit;

namespace Horde.Build.Utilities.BlockKit
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
		/// <param name="message">A message explaining what was wrong with the data.</param>
		/// <returns>A <see cref="BlockKitMessage"/> to be sent to Slack.</returns>
		public static BlockKitMessage CreateBadDataResponse(string message)
		{
			BlockKitMessage blockKitMessage = new BlockKitMessage();
			BlockKitAttachment attachment = new BlockKitAttachment();
			attachment.Color = BlockKitAttachmentColors.Error;
			attachment.Blocks.Add(new SectionBlock("Failed to process request due to bad data from Slack:"));
			attachment.Blocks.Add(new SectionBlock(message));
			blockKitMessage.Attachments.Add(attachment);
			return blockKitMessage;
		}

		/// <summary>
		/// Creates a response to send to Slack when failing to assign an issue due to it not
		/// being found in the DB.
		/// </summary>
		/// <returns>A <see cref="BlockKitMessage"/> to be sent to Slack.</returns>
		public static BlockKitMessage CreateIssueNotFoundResponse(int issueId)
		{
			BlockKitMessage blockKitMessage = new BlockKitMessage();
			BlockKitAttachment attachment = new BlockKitAttachment();
			attachment.Color = BlockKitAttachmentColors.Error;
			attachment.Blocks.Add(new SectionBlock($"Failed to assign issue as issue {issueId} could not be found."));
			blockKitMessage.Attachments.Add(attachment);
			return blockKitMessage;
		}
	}
}
