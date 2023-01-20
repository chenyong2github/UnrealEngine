// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
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
	public class ComputeChannel
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
		readonly int _blockSize;
		readonly ICryptoTransform _decryptor;
		readonly ICryptoTransform _encryptor;

		readonly CbWriter _writer;

		byte[] _readBuffer = new byte[128];
		int _decryptedLength;
		int _encryptedLength;
		int _paddedMessageLength;

		byte[] _writeBuffer = new byte[128];

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="socket">Socket for communication</param>
		/// <param name="aesKey">AES encryption key</param>
		/// <param name="aesIv">AES initialization vector</param>
		public ComputeChannel(Socket socket, ReadOnlyMemory<byte> aesKey, ReadOnlyMemory<byte> aesIv)
		{
			using Aes aes = Aes.Create();
			aes.Key = aesKey.ToArray();
			aes.IV = aesIv.ToArray();
			aes.Padding = PaddingMode.None;

			_socket = socket;
			_blockSize = aes.BlockSize / 8;
			_decryptor = aes.CreateDecryptor();
			_encryptor = aes.CreateEncryptor();

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
			// Discard any data from the previous message
			if (_paddedMessageLength > 0)
			{
				Buffer.BlockCopy(_readBuffer, _paddedMessageLength, _readBuffer, 0, _encryptedLength - _paddedMessageLength);
				_encryptedLength -= _paddedMessageLength;
				_decryptedLength -= _paddedMessageLength;
			}

			// Read the next message data
			for (; ; )
			{
				// Check the object in the buffer
				if (_decryptedLength >= 2)
				{
					int messageLength = 1 + VarInt.Measure(_readBuffer[1]);
					if (_decryptedLength > messageLength)
					{
						messageLength += (int)VarInt.ReadUnsigned(_readBuffer.AsSpan(1));
					}

					_paddedMessageLength = messageLength + GetPadding(messageLength);

					if (_decryptedLength >= _paddedMessageLength)
					{
						CbObject obj = new CbObject(_readBuffer.AsMemory(0, messageLength));

						CbField typeField = obj.Find(s_typeField);
						Utf8String type = typeField.AsUtf8String();

						ICbConverter converter = s_typeToConverter[type];

						CbField dataField = obj.Find(s_dataField);
						MessageBase message = (MessageBase)converter.ReadObject(dataField)!;

						return message;
					}

					if (_readBuffer.Length < _paddedMessageLength)
					{
						Array.Resize(ref _readBuffer, _paddedMessageLength);
					}
				}

				// Read the next chunk of data from the socket
				int read = await _socket.ReceiveAsync(_readBuffer.AsMemory(_encryptedLength), SocketFlags.Partial, cancellationToken);
				if (read == 0)
				{
					throw new EndOfStreamException();
				}
				_encryptedLength += read;

				// Decrypt any new full blocks that have been received
				int nextTransformSize = _encryptedLength - _decryptedLength;
				if (nextTransformSize >= _blockSize)
				{
					nextTransformSize -= nextTransformSize % _blockSize;
					_decryptedLength += _decryptor.TransformBlock(_readBuffer, _decryptedLength, nextTransformSize, _readBuffer, _decryptedLength);
				}
			}
		}

		/// <summary>
		/// Sends a message to the remote
		/// </summary>
		/// <param name="message">Message to be sent</param>
		/// <param name="cancellationToken">Cancellation token</param>
		/// <returns></returns>
		public async Task WriteAsync(MessageBase message, CancellationToken cancellationToken)
		{
			// Format the output object
			Utf8String name = message.Type;
		
			_writer.Clear();
			_writer.BeginObject();
			_writer.WriteUtf8String(s_typeField, name);

			ICbConverter converter = CbConverter.GetConverter(message.GetType());
			converter.WriteNamedObject(_writer, s_dataField, message);

			_writer.EndObject();

			// Serialize it to the write buffer
			int length = _writer.GetSize();
			length += GetPadding(length);

			if (length > _writeBuffer.Length)
			{
				_writeBuffer = new byte[length];
			}

			_writer.CopyTo(_writeBuffer);

			// Encrypt the data and send it
			int transformed = _encryptor.TransformBlock(_writeBuffer, 0, length, _writeBuffer, 0);
			if (transformed != length)
			{
				throw new InvalidOperationException();
			}
			await _socket.SendAsync(_writeBuffer.AsMemory(0, length), SocketFlags.Partial, cancellationToken);
		}

		int GetPadding(int length)
		{
			int modulo = length % _blockSize;
			if (modulo == 0)
			{
				return 0;
			}
			else
			{
				return _blockSize - modulo;
			}
		}
	}
}
