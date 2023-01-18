// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Base class for messages
	/// </summary>
	public abstract class MessageBase
	{
		/// <summary>
		/// Type of the message
		/// </summary>
		public Utf8String Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		protected MessageBase()
		{
			Type = GetType().GetCustomAttribute<MessageAttribute>()!.Name;
		}
	}

	/// <summary>
	/// Attribute used to annotate a message type with an identifier string
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class MessageAttribute : Attribute
	{
		/// <summary>
		/// Message type
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public MessageAttribute(string name)
		{
			Name = name;
		}
	}

	/// <summary>
	/// Reads messages from a socket
	/// </summary>
	public class MessageChannel
	{
		static readonly Utf8String s_typeField = "name";
		static readonly Utf8String s_dataField = "data";

		static readonly Type[] s_messageTypes =
		{
			typeof(CloseMessage),
			typeof(XorRequestMessage),
			typeof(XorResponseMessage)
		};

		static readonly Dictionary<Utf8String, ICbConverter> s_typeToConverter = CreateTypeToConverterMap();

		readonly Socket _socket;
		readonly CbWriter _writer;

		byte[] _buffer = new byte[64];

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="socket"></param>
		public MessageChannel(Socket socket)
		{
			_socket = socket;
			_writer = new CbWriter();
		}

		/// <summary>
		/// Build a lookup of all known message types
		/// </summary>
		static Dictionary<Utf8String, ICbConverter> CreateTypeToConverterMap()
		{
			Dictionary<Utf8String, ICbConverter> typeToConverter = new Dictionary<Utf8String, ICbConverter>();
			foreach (Type messageType in s_messageTypes)
			{
				string name = messageType.GetCustomAttribute<MessageAttribute>()!.Name;
				ICbConverter converter = CbConverter.GetConverter(messageType);
				typeToConverter.Add(name, converter);
			}
			return typeToConverter;
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<T> ReadAsync<T>(CancellationToken cancellationToken) where T : MessageBase
		{
			return (T)await ReadAsync(cancellationToken);
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<MessageBase> ReadAsync(CancellationToken cancellationToken)
		{
			int length = 0;
			for (; ; )
			{
				int read = await _socket.ReceiveAsync(_buffer.AsMemory(length), SocketFlags.None, cancellationToken);
				length += read;

				if (length >= 2)
				{
					int requiredLength = 1 + VarInt.Measure(_buffer[1]);
					if (length > requiredLength)
					{
						requiredLength += (int)VarInt.ReadUnsigned(_buffer.AsSpan(1));
					}
					if (length >= requiredLength)
					{
						break;
					}

					if (requiredLength > _buffer.Length)
					{
						Array.Resize(ref _buffer, requiredLength);
					}
				}
			}

			CbObject obj = new CbObject(_buffer.AsMemory(0, length));

			CbField typeField = obj.Find(s_typeField);
			Utf8String type = typeField.AsUtf8String();

			ICbConverter converter = s_typeToConverter[type];

			CbField dataField = obj.Find(s_dataField);
			return (MessageBase)converter.ReadObject(dataField)!;
		}

		/// <summary>
		/// Sends a message to the remote
		/// </summary>
		/// <param name="message">Message to be sent</param>
		/// <param name="cancellationToken">Cancellation token</param>
		/// <returns></returns>
		public async Task WriteAsync(MessageBase message, CancellationToken cancellationToken)
		{
			Utf8String name = message.Type;
		
			_writer.Clear();
			_writer.BeginObject();
			_writer.WriteUtf8String(s_typeField, name);

			ICbConverter converter = CbConverter.GetConverter(message.GetType());
			converter.WriteNamedObject(_writer, s_dataField, message);

			_writer.EndObject();

			byte[] packet = _writer.ToByteArray();
			await _socket.SendAsync(packet, SocketFlags.None, cancellationToken);
		}
	}
}
