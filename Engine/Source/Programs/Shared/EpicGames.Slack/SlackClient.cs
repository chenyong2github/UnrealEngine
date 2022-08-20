// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;

namespace EpicGames.Slack
{
	public class SlackException : Exception
	{
		public string Code { get; }

		public SlackException(string code) : base(code)
		{
			Code = code;
		}
	}

	public class SlackClient
	{
		class SlackResponse
		{
			[JsonPropertyName("ok")]
			public bool Ok { get; set; }

			[JsonPropertyName("error")]
			public string? Error { get; set; }
		}

		readonly HttpClient _httpClient;
		readonly JsonSerializerOptions _serializerOptions;
		readonly ILogger _logger;

		public SlackClient(HttpClient httpClient, ILogger logger)
		{
			_httpClient = httpClient;

			_serializerOptions = new JsonSerializerOptions();
			_serializerOptions.DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull;

			_logger = logger;
		}

		static bool ShouldLogError<TResponse>(TResponse response) where TResponse : SlackResponse => !response.Ok;

		private Task<TResponse> SendRequestAsync<TResponse>(string requestUrl, object request) where TResponse : SlackResponse
		{
			return SendRequestAsync<TResponse>(requestUrl, request, ShouldLogError);
		}

		private async Task<TResponse> SendRequestAsync<TResponse>(string requestUrl, object request, Func<TResponse, bool> shouldLogError) where TResponse : SlackResponse
		{
			using (HttpRequestMessage sendMessageRequest = new HttpRequestMessage(HttpMethod.Post, requestUrl))
			{
				string requestJson = JsonSerializer.Serialize(request, _serializerOptions);
				using (StringContent messageContent = new StringContent(requestJson, Encoding.UTF8, "application/json"))
				{
					sendMessageRequest.Content = messageContent;
					return await SendRequestAsync<TResponse>(sendMessageRequest, requestJson, shouldLogError);
				}
			}
		}

		private Task<TResponse> SendRequestAsync<TResponse>(HttpRequestMessage request, string requestJson) where TResponse : SlackResponse
		{
			return SendRequestAsync<TResponse>(request, requestJson, ShouldLogError);
		}

		private async Task<TResponse> SendRequestAsync<TResponse>(HttpRequestMessage request, string requestJson, Func<TResponse, bool> shouldLogError) where TResponse : SlackResponse
		{
			HttpResponseMessage response = await _httpClient.SendAsync(request);
			byte[] responseBytes = await response.Content.ReadAsByteArrayAsync();

			TResponse responseObject = JsonSerializer.Deserialize<TResponse>(responseBytes)!;
			if (shouldLogError(responseObject))
			{
				_logger.LogError("Failed to send Slack message ({Error}). Request: {Request}. Response: {Response}", responseObject.Error, requestJson, Encoding.UTF8.GetString(responseBytes));
				throw new SlackException(responseObject.Error ?? "unknown");
			}
			return responseObject;
		}

		#region Chat

		const string PostMessageUrl = "https://slack.com/api/chat.postMessage";
		const string UpdateMessageUrl = "https://slack.com/api/chat.update";
		const string GetPermalinkUrl = "https://slack.com/api/chat.getPermalink";

		class PostMessageRequest
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("ts")]
			public string? Ts { get; set; }

			[JsonPropertyName("thread_ts")]
			public string? ThreadTs { get; set; }

			[JsonPropertyName("text")]
			public string? Text { get; set; }

			[JsonPropertyName("mrkdwn")]
			public bool? Markdown { get; set; }

			[JsonPropertyName("blocks")]
			public List<Block>? Blocks { get; set; }
			
			[JsonPropertyName("reply_broadcast")]
			public bool? ReplyBroadcast { get; set; }

			[JsonPropertyName("unfurl_links")]
			public bool? UnfurlLinks { get; set; }

			[JsonPropertyName("unfurl_media")]
			public bool? UnfurlMedia { get; set; }
		}

		class PostMessageResponse : SlackResponse
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("ts")]
			public string? Ts { get; set; }
		}

		public async Task<string> PostMessageAsync(string recipient, SlackMessage message)
		{
			return await PostOrUpdateMessageAsync(recipient, null, null, message, false);
		}

		public async Task<string> PostMessageAsync(string recipient, string threadTs, SlackMessage message, bool replyBroadcast = false)
		{
			return await PostOrUpdateMessageAsync(recipient, null, threadTs, message, replyBroadcast);
		}

		public async Task UpdateMessageAsync(string recipient, string ts, SlackMessage message)
		{
			await PostOrUpdateMessageAsync(recipient, ts, null, message, false);
		}

		public async Task UpdateMessageAsync(string recipient, string ts, string threadTs, SlackMessage message, bool replyBroadcast = false)
		{
			await PostOrUpdateMessageAsync(recipient, ts, threadTs, message, replyBroadcast);
		}

		async Task<string> PostOrUpdateMessageAsync(string recipient, string? ts, string? threadTs, SlackMessage message, bool replyBroadcast)
		{
			PostMessageRequest request = new PostMessageRequest();
			request.Channel = recipient;
			request.Ts = ts;
			request.ThreadTs = threadTs;
			request.Text = message.Text;
			if (message.Blocks.Count > 0)
			{
				request.Blocks = message.Blocks;
			}
			if (replyBroadcast)
			{
				request.ReplyBroadcast = replyBroadcast;
			}
			if (!message.UnfurlLinks)
			{
				request.UnfurlLinks = false;
			}
			if (!message.UnfurlMedia)
			{
				request.UnfurlMedia = false;
			}

			PostMessageResponse response = await SendRequestAsync<PostMessageResponse>(ts == null? PostMessageUrl : UpdateMessageUrl, request);
			if (!response.Ok || response.Ts == null)
			{
				throw new SlackException(response.Error ?? "unknown");
			}

			return response.Ts;
		}

		class GetPermalinkRequest
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("message_ts")]
			public string? MessageTs { get; set; }
		}

		class GetPermalinkResponse : SlackResponse
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("permalink")]
			public string? Permalink { get; set; }
		}

		public async Task<string> GetPermalinkAsync(string channel, string ts)
		{
			string requestUrl = $"{GetPermalinkUrl}?channel={channel}&message_ts={ts}";
			using (HttpRequestMessage sendMessageRequest = new HttpRequestMessage(HttpMethod.Get, requestUrl))
			{
				GetPermalinkResponse response = await SendRequestAsync<GetPermalinkResponse>(sendMessageRequest, "");
				if (!response.Ok || response.Permalink == null)
				{
					throw new SlackException(response.Error ?? "unknown");
				}
				return response.Permalink;
			}
		}

		#endregion

		#region Reactions

		const string AddReactionUrl = "https://slack.com/api/reactions.add";
		const string RemoveReactionUrl = "https://slack.com/api/reactions.remove";

		class ReactionMessage
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("timestamp")]
			public string? Ts { get; set; }

			[JsonPropertyName("name")]
			public string? Name { get; set; }
		}

		public async Task AddReactionAsync(string channel, string ts, string name)
		{
			ReactionMessage message = new ReactionMessage();
			message.Channel = channel;
			message.Ts = ts;
			message.Name = name;

			static bool ShouldLogError(SlackResponse response) => !response.Ok && !String.Equals(response.Error, "already_reacted", StringComparison.Ordinal);

			await SendRequestAsync<SlackResponse>(AddReactionUrl, message, ShouldLogError);
		}

		public async Task RemoveReactionAsync(string channel, string ts, string name)
		{
			ReactionMessage message = new ReactionMessage();
			message.Channel = channel;
			message.Ts = ts;
			message.Name = name;

			static bool ShouldLogError(SlackResponse response) => !response.Ok && !String.Equals(response.Error, "no_reaction", StringComparison.Ordinal);

			await SendRequestAsync<SlackResponse>(RemoveReactionUrl, message, ShouldLogError);
		}

		#endregion

		#region Conversations

		const string ConversationsInviteUrl = "https://slack.com/api/conversations.invite";

		class InviteMessage
		{
			[JsonPropertyName("channel")]
			public string? Channel { get; set; }

			[JsonPropertyName("users")]
			public string? Users { get; set; } // Comma separated list of ids
		}

		public Task InviteUserAsync(string channel, string userId) => InviteUsersAsync(channel, new[] { userId });

		public async Task InviteUsersAsync(string channel, IEnumerable<string> userIds)
		{
			InviteMessage message = new InviteMessage();
			message.Channel = channel;
			message.Users = String.Join(",", userIds);
			await SendRequestAsync<SlackResponse>(ConversationsInviteUrl, message);
		}

		#endregion

		#region Users

		const string UsersInfoUrl = "https://slack.com/api/users.info";
		const string UsersLookupByEmailUrl = "https://slack.com/api/users.lookupByEmail";

		class UserResponse : SlackResponse
		{
			[JsonPropertyName("user")]
			public SlackUser? User { get; set; }
		}

		public async Task<SlackUser> GetUserAsync(string userId)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, $"{UsersInfoUrl}?user={userId}"))
			{
				UserResponse response = await SendRequestAsync<UserResponse>(request, "", ShouldLogError);
				return response.User!;
			}
		}

		public async Task<SlackUser?> FindUserByEmailAsync(string email)
		{
			using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, $"{UsersLookupByEmailUrl}?email={email}"))
			{
				static bool ShouldLogError(UserResponse response) => !response.Ok && !String.Equals(response.Error, "users_not_found", StringComparison.Ordinal);

				UserResponse response = await SendRequestAsync<UserResponse>(request, "", ShouldLogError);
				return response.User;
			}
		}

		#endregion

		#region Views

		const string ViewsOpenUrl = "https://slack.com/api/views.open";

		class ViewsOpenRequest
		{
			[JsonPropertyName("trigger_id")]
			public string? TriggerId { get; set; }

			[JsonPropertyName("view")]
			public SlackView? View { get; set; }
		}

		public async Task OpenViewAsync(string triggerId, SlackView view)
		{
			ViewsOpenRequest request = new ViewsOpenRequest();
			request.TriggerId = triggerId;
			request.View = view;
			await SendRequestAsync<PostMessageResponse>(ViewsOpenUrl, request);
		}

		#endregion
/*
		#region Interactions

		const string AppConnectionOpenUrl = "https://slack.com/api/apps.connections.open";

		class OpenSocketResponse : SlackResponse
		{
			[JsonPropertyName("url")]
			public Uri? Url { get; set; }
		}

		class EventMessage
		{
			[JsonPropertyName("type")]
			public string? Type { get; set; }

			[JsonPropertyName("envelope_id")]
			public string? EnvelopeId { get; set; }

			[JsonPropertyName("payload")]
			public EventPayload? Payload { get; set; }
		}

		class EventPayload
		{
			[JsonPropertyName("type")]
			public string? Type { get; set; }

			[JsonPropertyName("trigger_id")]
			public string? TriggerId { get; set; }

			[JsonPropertyName("user")]
			public UserInfo? User { get; set; }

			[JsonPropertyName("response_url")]
			public string? ResponseUrl { get; set; }

			[JsonPropertyName("actions")]
			public List<ActionInfo> Actions { get; set; } = new List<ActionInfo>();
		}

		/// <inheritdoc/>
		public async Task HandleInteractionsAsync(CancellationToken stoppingToken)
		{
			using HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Post, AppConnectionOpenUrl);
			request.Content = new FormUrlEncodedContent(Array.Empty<KeyValuePair<string?, string?>>());

			OpenSocketResponse response = await SendRequestAsync<OpenSocketResponse>(request, "");
		}

		private async Task HandleInteractionsInternalAsync(Uri socketUrl, CancellationToken stoppingToken)
		{
			using ClientWebSocket socket = new ClientWebSocket();
			await socket.ConnectAsync(socketUrl, stoppingToken);

			byte[] buffer = new byte[2048];
			while (!stoppingToken.IsCancellationRequested)
			{
				// Read the next message
				int length = 0;
				for (; ; )
				{
					if (length == buffer.Length)
					{
						Array.Resize(ref buffer, buffer.Length + 2048);
					}

					WebSocketReceiveResult result = await socket.ReceiveAsync(new ArraySegment<byte>(buffer, length, buffer.Length - length), stoppingToken);
					if (result.MessageType == WebSocketMessageType.Close)
					{
						return;
					}
					length += result.Count;

					if (result.EndOfMessage)
					{
						break;
					}
				}

				// Get the message data
				_logger.LogInformation("Slack event: {Message}", Encoding.UTF8.GetString(buffer, 0, length));
				EventMessage eventMessage = JsonSerializer.Deserialize<EventMessage>(buffer.AsSpan(0, length))!;

				// Acknowledge the message
				if (eventMessage.EnvelopeId != null)
				{
					object response = new { eventMessage.EnvelopeId };
					await socket.SendAsync(JsonSerializer.SerializeToUtf8Bytes(response), WebSocketMessageType.Text, true, stoppingToken);
				}

				// Handle the message type
				if (eventMessage.Type != null)
				{
					string type = eventMessage.Type;
					if (type.Equals("disconnect", StringComparison.Ordinal))
					{
						break;
					}
					else if (type.Equals("interactive", StringComparison.Ordinal))
					{
						await HandleInteractionMessage(eventMessage);
					}
					else
					{
						_logger.LogDebug("Unhandled event type ({Type})", type);
					}
				}
			}
		}

		#endregion
*/
	}
}
