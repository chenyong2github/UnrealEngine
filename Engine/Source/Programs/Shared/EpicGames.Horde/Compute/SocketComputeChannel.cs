// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

#pragma warning disable CA5401

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Transfers messages via a AES-encrypted socket
	/// </summary>
	public sealed class SocketComputeChannel : IComputeChannel, IDisposable
	{
		readonly Socket _socket;
		readonly int _blockSize;
		readonly ICryptoTransform _decryptor;
		readonly ICryptoTransform _encryptor;

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
		public SocketComputeChannel(Socket socket, ReadOnlyMemory<byte> aesKey, ReadOnlyMemory<byte> aesIv)
		{
			using Aes aes = Aes.Create();
			aes.Key = aesKey.ToArray();
			aes.IV = aesIv.ToArray();
			aes.Padding = PaddingMode.None;

			_socket = socket;
			_blockSize = aes.BlockSize / 8;
			_decryptor = aes.CreateDecryptor();
			_encryptor = aes.CreateEncryptor();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			_decryptor.Dispose();
			_encryptor.Dispose();
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<ReadOnlyMemory<byte>> ReadMessageAsync(CancellationToken cancellationToken)
		{
			// Discard any data from the previous message
			if (_paddedMessageLength > 0)
			{
				Buffer.BlockCopy(_readBuffer, _paddedMessageLength, _readBuffer, 0, _encryptedLength - _paddedMessageLength);
				_encryptedLength -= _paddedMessageLength;
				_decryptedLength -= _paddedMessageLength;
				_paddedMessageLength = 0;
			}

			// Read the next message data
			for (; ; )
			{
				// Check the object in the buffer
				if (_decryptedLength > 0)
				{
					int headerLength = VarInt.Measure(_readBuffer);

					int messageLength = headerLength;
					if (_decryptedLength >= headerLength)
					{
						messageLength += (int)VarInt.ReadUnsigned(_readBuffer);
					}

					_paddedMessageLength = messageLength + GetPadding(messageLength);

					if (_decryptedLength >= _paddedMessageLength)
					{
						return _readBuffer.AsMemory(headerLength, messageLength - headerLength);
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
					return ReadOnlyMemory<byte>.Empty;
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
		/// <param name="message">Message to be written</param>
		/// <param name="cancellationToken">Cancellation token</param>
		/// <returns></returns>
		public async Task WriteMessageAsync(ReadOnlyMemory<byte> message, CancellationToken cancellationToken)
		{
			// Serialize it to the write buffer
			int headerLength = VarInt.MeasureUnsigned(message.Length);

			int length = headerLength + message.Length;
			length += GetPadding(length);

			if (length > _writeBuffer.Length)
			{
				_writeBuffer = new byte[length];
			}

			// Write the data to the buffer
			VarInt.WriteUnsigned(_writeBuffer, message.Length);
			message.CopyTo(_writeBuffer.AsMemory(headerLength));

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
