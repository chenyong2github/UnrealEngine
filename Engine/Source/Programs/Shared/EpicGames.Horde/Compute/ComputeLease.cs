// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Serialization;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Handles a compute lease with a particular remote, multiplexing messages across a single socket.
	/// </summary>
	public sealed class ComputeLease : IComputeLease
	{
		/// <summary>
		/// Length of the required encrption key. 
		/// </summary>
		public const int KeyLength = 32;

		/// <summary>
		/// Length of the nonce. This should be a cryptographically random number, and does not have to be secret.
		/// </summary>
		public const int NonceLength = 12;

		/// <summary>
		/// Maximum length of a single message
		/// </summary>
		public const int MaxMessageLength = 1024 * 1024;

		sealed class Message : IComputeMessage
		{
			internal IMemoryOwner<byte>? _owner;
			internal readonly TaskCompletionSource<Message> _next = new TaskCompletionSource<Message>();

			/// <summary>
			/// Type of the message
			/// </summary>
			public ComputeMessageType Type { get; }

			/// <summary>
			/// Message data
			/// </summary>
			public ReadOnlyMemory<byte> Data { get; private set; }

			public Message(IMemoryOwner<byte>? owner, ComputeMessageType type, ReadOnlyMemory<byte> data)
			{
				_owner = owner;

				Type = type;
				Data = data;
			}

			/// <inheritdoc/>
			public void Dispose()
			{
				if (_owner != null)
				{
					_owner.Dispose();
					_owner = null;
				}
			}
		}

		/// <summary>
		/// Allows creating new messages in rented memory
		/// </summary>
		class Writer : IComputeMessageWriter
		{
			readonly ComputeLease _owner;
			PooledMemoryWriter _writer;

			public int Length => _writer.Length;

			public Writer(ComputeLease owner, int channelId, ComputeMessageType type, PooledMemoryWriter writer)
			{
				_owner = owner;
				_writer = writer;

				_writer.WriteUInt32(0); // Placeholder for size
				_writer.WriteUnsignedVarInt(channelId);
				_writer.WriteUInt8((byte)type);
			}

			public void Dispose()
			{
				if (_writer != null)
				{
					_owner.ReleaseWriter(_writer);
					_writer = null!;
				}
			}

			public ValueTask SendAsync(CancellationToken cancellationToken)
			{
				int bodySize = _writer.Length - HeaderLength;
				_writer.Advance(FooterLength);

				Span<byte> message = _writer.WrittenMemory.Span;

				Span<byte> header = message.Slice(0, HeaderLength);
				BinaryPrimitives.WriteInt32LittleEndian(header, bodySize);

				Span<byte> body = message.Slice(HeaderLength, bodySize);
				Span<byte> footer = message.Slice(header.Length + body.Length);
				_owner._aesGcm.Encrypt(_owner._writeNonce, body, body, footer, header);
				IncrementNonce(_owner._writeNonce);

				return _owner.SendMessageAsync(_writer.WrittenMemory, cancellationToken);
			}

			/// <inheritdoc/>
			public void Advance(int count) => _writer.Advance(count);

			/// <inheritdoc/>
			public Memory<byte> GetMemory(int sizeHint = 0) => _writer.GetMemory(sizeHint);

			/// <inheritdoc/>
			public Span<byte> GetSpan(int sizeHint = 0) => _writer.GetSpan(sizeHint);
		}

		/// <summary>
		/// Multiplexes data onto the owner's socket
		/// </summary>
		class Channel : IComputeChannel
		{
			readonly ComputeLease _owner;
			readonly int _channelId;

			TaskCompletionSource<Message> _readTail;
			Task<Message> _readNext;

			public int Id => _channelId;

			public Channel(ComputeLease owner, int channelId)
			{
				_owner = owner;
				_channelId = channelId;
				_readTail = new TaskCompletionSource<Message>();
				_readNext = _readTail.Task;
			}

			public IComputeMessageWriter CreateMessage(ComputeMessageType type, int reserve) => _owner.CreateWriter(type, _channelId, reserve);

			public void Dispose()
			{
				_owner.RemoveChannel(_channelId);
			}

			public void MarkComplete()
			{
#pragma warning disable CA2000 // Dispose objects before losing scope
				_readTail.SetResult(new Message(null, ComputeMessageType.None, ReadOnlyMemory<byte>.Empty));
#pragma warning restore CA2000 // Dispose objects before losing scope
			}

			public void QueueMessage(Message message)
			{
				_readTail.SetResult(message);
				_readTail = message._next;
			}

			/// <inheritdoc/>
			public async ValueTask<IComputeMessage> ReadAsync(CancellationToken cancellationToken)
			{
				Message message = await _readNext;
				_readNext = message._next.Task;
				return message;
			}
		}

		/// <inheritdoc/>
		public IComputeChannel DefaultChannel => _defaultChannel;

		/// <inheritdoc/>
		public IReadOnlyDictionary<string, int> AssignedResources { get; }

		const int HeaderLength = sizeof(int); // Unencrypted size of the message
		const int FooterLength = 16; // 16-byte auth tag for encryption

		readonly Socket _socket;
		readonly MemoryPool<byte> _pool;
		readonly AesGcm _aesGcm;
		readonly BackgroundTask _backgroundTask;
		readonly Channel _defaultChannel;

		readonly object _lockObject = new object();
		readonly Dictionary<int, Channel> _channels = new Dictionary<int, Channel>();

		readonly byte[] _readNonce;
		readonly byte[] _writeNonce;

		readonly Stack<PooledMemoryWriter> _freeWriters = new Stack<PooledMemoryWriter>();

		readonly SemaphoreSlim _writeSemaphore;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="socket">Socket for communication</param>
		/// <param name="key">AES encryption key (256 bits / 32 bytes)</param>
		/// <param name="nonce">Cryptographic nonce to identify the connection. Must be longer than <see cref="NonceLength"/>.</param>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		public ComputeLease(Socket socket, ReadOnlySpan<byte> key, ReadOnlySpan<byte> nonce, IReadOnlyDictionary<string, int> assignedResources)
			: this(socket, key, nonce, assignedResources, MemoryPool<byte>.Shared)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="socket">Socket for communication</param>
		/// <param name="key">AES encryption key (256 bits / 32 bytes)</param>
		/// <param name="nonce">Cryptographic nonce to identify the connection. Must be longer than <see cref="NonceLength"/>.</param>
		/// <param name="assignedResources">Resources assigned to this lease</param>
		/// <param name="pool">Memory pool from which to allocate message buffers</param>
		public ComputeLease(Socket socket, ReadOnlySpan<byte> key, ReadOnlySpan<byte> nonce, IReadOnlyDictionary<string, int> assignedResources, MemoryPool<byte> pool)
		{
			if (key.Length != KeyLength)
			{
				throw new ArgumentException($"Key must be {KeyLength} bytes", nameof(key));
			}

			_socket = socket;
			_pool = pool;
			_aesGcm = new AesGcm(key);

			_readNonce = nonce.Slice(0, NonceLength).ToArray();
			_writeNonce = nonce.Slice(0, NonceLength).ToArray();

			_writeSemaphore = new SemaphoreSlim(1);

			_defaultChannel = new Channel(this, 0);
			_channels.Add(0, _defaultChannel);

			_backgroundTask = BackgroundTask.StartNew(BackgroundReadAsync);

			AssignedResources = assignedResources;
		}

		/// <inheritdoc/>
		public async ValueTask DisposeAsync()
		{
			await _backgroundTask.DisposeAsync();

			foreach (Channel channel in _channels.Values)
			{
				channel.MarkComplete();
			}

			_writeSemaphore.Dispose();
			_aesGcm.Dispose();
		}

		/// <summary>
		/// Creates a new encryption key for the compute lead
		/// </summary>
		/// <returns>New key bytes</returns>
		public static byte[] CreateKey() => RandomNumberGenerator.GetBytes(KeyLength);

		/// <inheritdoc/>
		public IComputeChannel OpenChannel(int channelId)
		{
			Channel channel = new Channel(this, channelId);
			lock (_lockObject)
			{
				_channels.Add(channelId, channel);
			}
			return channel;
		}

		async Task BackgroundReadAsync(CancellationToken cancellationToken)
		{
			byte[] sizeBuffer = new byte[4];
			while (!cancellationToken.IsCancellationRequested)
			{
				if (!await _socket.TryReceiveMessageAsync(sizeBuffer, SocketFlags.None, cancellationToken))
				{
					break;
				}

				int length = BinaryPrimitives.ReadInt32LittleEndian(sizeBuffer);
				if (length < 0 || length > MaxMessageLength)
				{
					throw new InvalidOperationException();
				}

				IMemoryOwner<byte> owner = _pool.Rent(length + FooterLength);
				try
				{
					// Decrypt the message
					await _socket.ReceiveMessageAsync(owner.Memory.Slice(0, length + FooterLength), SocketFlags.None, cancellationToken);
					_aesGcm.Decrypt(_readNonce, owner.Memory.Span.Slice(0, length), owner.Memory.Span.Slice(length, FooterLength), owner.Memory.Span.Slice(0, length), sizeBuffer);
					IncrementNonce(_readNonce);

					// Decode the message data
					ReadOnlyMemory<byte> data = owner.Memory.Slice(0, length);

					int channelId = (int)VarInt.ReadUnsigned(data.Span, out int channelIdByteCount);
					data = data.Slice(channelIdByteCount);

					ComputeMessageType type = (ComputeMessageType)data.Span[0];
					data = data.Slice(1);

#pragma warning disable CA2000 // Dispose objects before losing scope
					Message message = new Message(owner, type, data);
#pragma warning restore CA2000 // Dispose objects before losing scope

					Channel? channel;
					lock (_lockObject)
					{
						channel = _channels[channelId];
					}

					channel.QueueMessage(message);
				}
				catch
				{
					owner.Dispose();
					throw;
				}
			}
		}

		void RemoveChannel(int channelId)
		{
			lock (_lockObject)
			{
				_channels.Remove(channelId);
			}
		}

		Writer CreateWriter(ComputeMessageType type, int channelId, int reserve)
		{
			PooledMemoryWriter? writer;
			if (_freeWriters.TryPop(out writer))
			{
				writer.Clear(reserve);
			}
			else
			{
				writer = new PooledMemoryWriter(reserve);
			}
			return new Writer(this, channelId, type, writer);
		}

		void ReleaseWriter(PooledMemoryWriter writer)
		{
			_freeWriters.Push(writer);
		}

		async ValueTask SendMessageAsync(ReadOnlyMemory<byte> message, CancellationToken cancellationToken)
		{
			await _writeSemaphore.WaitAsync(cancellationToken);
			try
			{
				await _socket.SendMessageAsync(message, SocketFlags.None, cancellationToken);
			}
			finally
			{
				_writeSemaphore.Release();
			}
		}

		static void IncrementNonce(byte[] nonce)
		{
			BinaryPrimitives.WriteInt64LittleEndian(nonce, BinaryPrimitives.ReadInt64LittleEndian(nonce) + 1);
		}
	}
}
