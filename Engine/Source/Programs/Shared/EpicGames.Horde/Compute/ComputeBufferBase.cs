// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// In-process buffer used to store compute messages
	/// </summary>
	public abstract class ComputeBufferBase : IDisposable
	{
		const int MaxChunks = 16;
		const int MaxReaders = 16;

		internal const int HeaderSize = (2 + MaxChunks + MaxReaders) * sizeof(ulong);

		/// <summary>
		/// Tracked state of the buffer
		/// </summary>
		internal readonly unsafe struct HeaderPtr
		{
			readonly ulong* _data;

			public HeaderPtr(ulong* data) => _data = data;

			public HeaderPtr(ulong* data, int numReaders, int numChunks, int chunkLength)
			{
				_data = data;

				data[0] = ((ulong)(uint)numChunks << 32) | (uint)numReaders;
				data[1] = (uint)chunkLength;

				GetChunkStatePtr(0).StartWriting(numReaders);
			}

			public int NumReaders => (int)_data[0];
			public int NumChunks => (int)(_data[0] >> 32);
			public int ChunkLength => (int)_data[1];
			public int WriteChunkIdx
			{
				get => (int)(_data[1] >> 32);
				set => _data[1] = ((ulong)value << 32) | (uint)ChunkLength;
			}

			public ChunkStatePtr GetChunkStatePtr(int chunkIdx) => new ChunkStatePtr(_data + 2 + chunkIdx);

			public ReaderStatePtr GetReaderStatePtr(int readerIdx) => new ReaderStatePtr(_data + 2 + MaxChunks + readerIdx);
		}

		/// <summary>
		/// Write state for a chunk
		/// </summary>
		internal enum WriteState
		{
			/// <summary>
			/// Writer has moved to the next chunk
			/// </summary>
			MovedToNext = 0,

			/// <summary>
			/// Chunk is still being appended to
			/// </summary>
			Writing = 2,

			/// <summary>
			/// This chunk marks the end of the stream
			/// </summary>
			Complete = 3,
		}

		/// <summary>
		/// Stores the state of a chunk in a 64-bit value, which can be updated atomically
		/// </summary>
		internal record struct ChunkState(ulong Value)
		{
			// Written length of this chunk
			public int Length => (int)(Value & 0x7fffffff);

			// Set of flags which are set for each reader that still has to read from a chunk
			public int ReaderFlags => (int)((Value >> 31) & 0x7fffffff);

			// State of the writer
			public WriteState WriteState => (WriteState)(Value >> 62);

			// Constructor
			public ChunkState(WriteState writerState, int readerFlags, int length) : this(((ulong)writerState << 62) | ((ulong)readerFlags << 31) | (uint)length) { }

			// Test whether a particular reader is still referencing the chunk
			public bool HasReaderFlag(int readerIdx) => (Value & (1UL << (readerIdx + 31))) != 0;

			/// <inheritdoc/>
			public override string ToString() => $"{WriteState}, Length: {Length}, Readers: {ReaderFlags}";
		}

		/// <summary>
		/// Wraps a pointer to the state of a chunk
		/// </summary>
		internal readonly unsafe struct ChunkStatePtr
		{
			readonly ulong* _data;

			public ChunkStatePtr(ulong* data) => _data = data;

			// Current value of the chunk state
			public ChunkState Value => new ChunkState(Interlocked.CompareExchange(ref *_data, 0, 0));

			// Written length of this chunk
			public int Length => Value.Length;

			// Set of flags which are set for each reader that still has to read from a chunk
			public int ReaderFlags => Value.ReaderFlags;

			// State of the writer
			public WriteState WriteState => Value.WriteState;

			// Append data to the chunk
			public void Append(int length) => Interlocked.Add(ref *_data, (uint)length);

			// Mark the chunk as being written to
			public void StartWriting(int numReaders) => Interlocked.Exchange(ref *_data, new ChunkState(WriteState.Writing, (1 << numReaders) - 1, 0).Value);

			// Move to the next chunk
			public void MarkComplete() => Interlocked.Or(ref *_data, (ulong)WriteState.Complete << 62);

			// Clear the reader flag
			public void FinishReading(int readerIdx) => Interlocked.And(ref *_data, ~(1UL << (readerIdx + 31)));

			// Move to the next chunk
			public void FinishWriting() => Interlocked.And(ref *_data, ~((ulong)WriteState.Writing << 62));

			/// <inheritdoc/>
			public override string ToString() => Value.ToString();
		}

		/// <summary>
		/// Encodes the state of a reader in a 64-bit value
		/// </summary>
		internal record struct ReaderState(ulong Value)
		{
			public ReaderState(int chunkIdx, int offset) : this(((ulong)chunkIdx << 32) | (uint)offset) { }

			public int ChunkIdx => (int)(Value >> 32);
			public int Offset => (int)Value;

			public override string ToString() => $"Chunk {ChunkIdx}, Offset {Offset}";
		}

		/// <summary>
		/// Pointer to a reader state value
		/// </summary>
		internal readonly unsafe struct ReaderStatePtr
		{
			readonly ulong* _data;

			public ReaderStatePtr(ulong* data) => _data = data;

			public ReaderState Value
			{
				get => new ReaderState(Interlocked.CompareExchange(ref *_data, 0, 0));
				set => Interlocked.Exchange(ref *_data, value.Value);
			}

			public int ChunkIdx => Value.ChunkIdx;
			public int Offset => Value.Offset;

			public void Advance(int length) => Interlocked.Add(ref *_data, (ulong)length);

			public override string ToString() => Value.ToString();
		}

		/// <inheritdoc cref="IComputeBufferReader"/>
		sealed class ReaderImpl : IComputeBufferReader
		{
			readonly ComputeBufferBase _buffer;
			readonly HeaderPtr _header;
			readonly Memory<byte>[] _chunks;
			readonly int _readerIdx;

			public bool IsComplete
			{
				get
				{
					ReaderState readerState = _header.GetReaderStatePtr(_readerIdx).Value;
					ChunkState chunkState = _header.GetChunkStatePtr(readerState.ChunkIdx).Value;
					return chunkState.WriteState == WriteState.Complete && readerState.Offset == chunkState.Length;
				}
			}

			public ReaderImpl(ComputeBufferBase buffer, HeaderPtr header, Memory<byte>[] chunks, int readerIdx)
			{
				_buffer = buffer;
				_header = header;
				_chunks = chunks;
				_readerIdx = readerIdx;
			}

			/// <inheritdoc/>
			public void AdvanceReadPosition(int length)
			{
				ReaderStatePtr readerStatePtr = _header.GetReaderStatePtr(_readerIdx);
				readerStatePtr.Advance(length);
			}

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetReadBuffer()
			{
				ReaderStatePtr readerStatePtr = _header.GetReaderStatePtr(_readerIdx);
				ReaderState readerState = readerStatePtr.Value;

				ChunkStatePtr chunkStatePtr = _header.GetChunkStatePtr(readerState.ChunkIdx);
				ChunkState chunkState = chunkStatePtr.Value;

				if (chunkState.HasReaderFlag(_readerIdx))
				{
					return _chunks[readerState.ChunkIdx].Slice(readerState.Offset, chunkState.Length - readerState.Offset);
				}
				else
				{
					return ReadOnlyMemory<byte>.Empty;
				}
			}

			/// <inheritdoc/>
			public async ValueTask<bool> WaitToReadAsync(int minLength, CancellationToken cancellationToken = default)
			{
				for (; ; )
				{
					ReaderStatePtr readerStatePtr = _header.GetReaderStatePtr(_readerIdx);
					ReaderState readerState = readerStatePtr.Value;

					ChunkStatePtr chunkStatePtr = _header.GetChunkStatePtr(readerState.ChunkIdx);
					ChunkState chunkState = chunkStatePtr.Value;

					if (!chunkState.HasReaderFlag(_readerIdx))
					{
						// Wait until the current chunk is readable
						_buffer.ResetReadEvent(_readerIdx);
						if (!chunkState.HasReaderFlag(_readerIdx))
						{
							await _buffer.WaitForReadEvent(_readerIdx, cancellationToken);
						}
					}
					else if (readerState.Offset + minLength <= chunkState.Length)
					{
						// We have enough data in the chunk to be able to read a message
						return true;
					}
					else if (chunkState.WriteState == WriteState.Writing)
					{
						// Wait until there is more data in the chunk
						_buffer.ResetReadEvent(_readerIdx);
						if (_header.GetChunkStatePtr(readerState.ChunkIdx).Value == chunkState)
						{
							await _buffer.WaitForReadEvent(_readerIdx, cancellationToken);
						}
					}
					else if (readerState.Offset < chunkState.Length || chunkState.WriteState == WriteState.Complete)
					{
						// Cannot read the requested amount of data from this chunk.
						return false;
					}
					else if (chunkState.WriteState == WriteState.MovedToNext)
					{
						// Move to the next chunk
						chunkStatePtr.FinishReading(_readerIdx);
						_buffer.SetWriteEvent();

						int chunkIdx = readerStatePtr.ChunkIdx + 1;
						if (chunkIdx == _chunks.Length)
						{
							chunkIdx = 0;
						}

						readerStatePtr.Value = new ReaderState(chunkIdx, 0);
					}
					else
					{
						throw new NotImplementedException($"Invalid write state for buffer: {chunkState.WriteState}");
					}
				}
			}
		}

		/// <inheritdoc cref="IComputeBufferWriter"/>
		sealed class WriterImpl : IComputeBufferWriter
		{
			readonly ComputeBufferBase _buffer;
			readonly HeaderPtr _header;
			readonly Memory<byte>[] _chunks;

			public WriterImpl(ComputeBufferBase buffer, HeaderPtr header, Memory<byte>[] chunks)
			{
				_buffer = buffer;
				_header = header;
				_chunks = chunks;
			}

			public void AdvanceWritePosition(int size)
			{
				if (size > 0)
				{
					ChunkStatePtr chunkStatePtr = _header.GetChunkStatePtr(_header.WriteChunkIdx);
					ChunkState chunkState = chunkStatePtr.Value;

					Debug.Assert(chunkState.WriteState == WriteState.Writing);
					chunkStatePtr.Append(size);

					_buffer.SetAllReadEvents();
				}
			}

			public Memory<byte> GetWriteBuffer()
			{
				ChunkState state = _header.GetChunkStatePtr(_header.WriteChunkIdx).Value;
				if (state.WriteState == WriteState.Writing)
				{
					return _chunks[_header.WriteChunkIdx].Slice(state.Length);
				}
				else
				{
					return Memory<byte>.Empty;
				}
			}

			public bool MarkComplete()
			{
				ChunkStatePtr chunkStatePtr = _header.GetChunkStatePtr(_header.WriteChunkIdx);
				if (chunkStatePtr.WriteState != WriteState.Complete)
				{
					chunkStatePtr.MarkComplete();
					_buffer.SetAllReadEvents();
					return true;
				}
				return false;
			}

			public async ValueTask WaitToWriteAsync(int minSize, CancellationToken cancellationToken)
			{
				if (minSize > _header.ChunkLength)
				{
					throw new ArgumentException("Requested read size is larger than chunk size.", nameof(minSize));
				}

				for (; ; )
				{
					ChunkStatePtr chunkStatePtr = _header.GetChunkStatePtr(_header.WriteChunkIdx);

					ChunkState chunkState = chunkStatePtr.Value;
					if (chunkState.WriteState == WriteState.Writing)
					{
						int length = chunkState.Length;
						if (length + minSize <= _header.ChunkLength)
						{
							return;
						}
						chunkStatePtr.FinishWriting();
					}

					int nextChunkIdx = _header.WriteChunkIdx + 1;
					if (nextChunkIdx == _chunks.Length)
					{
						nextChunkIdx = 0;
					}

					ChunkStatePtr nextChunkStatePtr = _header.GetChunkStatePtr(nextChunkIdx);
					while (nextChunkStatePtr.ReaderFlags != 0)
					{
						await _buffer.WaitForWriteEvent(cancellationToken);
						_buffer.ResetWriteEvent();
					}

					_header.WriteChunkIdx = nextChunkIdx;
					nextChunkStatePtr.StartWriting(_header.NumReaders);

					_buffer.SetAllReadEvents();
				}
			}
		}

		readonly HeaderPtr _headerPtr;
		readonly Memory<byte>[] _chunks;
		readonly ReaderImpl _reader;
		readonly WriterImpl _writer;

		int _refCount = 1;

		/// <inheritdoc/>
		public IComputeBufferReader Reader => _reader;

		/// <inheritdoc/>
		public IComputeBufferWriter Writer => _writer;

#pragma warning disable IDE0051 // Remove unused private members
		// For debugging purposes only
		ChunkState[] ChunkStates => Enumerable.Range(0, _headerPtr.NumChunks).Select(x => _headerPtr.GetChunkStatePtr(x).Value).ToArray();
		ReaderState[] ReaderStates => Enumerable.Range(0, _headerPtr.NumReaders).Select(x => _headerPtr.GetReaderStatePtr(x).Value).ToArray();
		int WriteChunkIdx => _headerPtr.WriteChunkIdx;
#pragma warning restore IDE0051 // Remove unused private members

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="headerPtr">Header for the buffer</param>
		/// <param name="chunks">Data for the buffer</param>
		internal ComputeBufferBase(HeaderPtr headerPtr, Memory<byte>[] chunks)
		{
			_headerPtr = headerPtr;
			_chunks = chunks;

			_reader = new ReaderImpl(this, _headerPtr, _chunks, 0);
			_writer = new WriterImpl(this, _headerPtr, _chunks);
		}

		/// <inheritdoc/>
		public void AddRef()
		{
			Interlocked.Increment(ref _refCount);
		}

		/// <inheritdoc/>
		public void Release()
		{
			if (Interlocked.Decrement(ref _refCount) == 0)
			{
				Dispose();
			}
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		/// <summary>
		/// Overriable dispose method
		/// </summary>
		protected virtual void Dispose(bool disposing)
		{
		}

		private void SetAllReadEvents()
		{
			for (int readerIdx = 0; readerIdx < _headerPtr.NumReaders; readerIdx++)
			{
				SetReadEvent(readerIdx);
			}
		}

		/// <summary>
		/// Signals a read event
		/// </summary>
		protected abstract void SetReadEvent(int readerIdx);

		/// <summary>
		/// Resets a read event
		/// </summary>
		protected abstract void ResetReadEvent(int readerIdx);

		/// <summary>
		/// Waits for a read event to be signalled
		/// </summary>
		protected abstract Task WaitForReadEvent(int readerIdx, CancellationToken cancellationToken);

		/// <summary>
		/// Signals the write event
		/// </summary>
		protected abstract void SetWriteEvent();

		/// <summary>
		/// Resets the write event
		/// </summary>
		protected abstract void ResetWriteEvent();

		/// <summary>
		/// Waits for the write event to be signalled
		/// </summary>
		protected abstract Task WaitForWriteEvent(CancellationToken cancellationToken);
	}
}
