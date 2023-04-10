// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// In-process buffer used to store compute messages
	/// </summary>
	public abstract class ComputeBufferBase : IComputeBuffer
	{
		/// <summary>
		/// Write state for a chunk
		/// </summary>
		protected enum WriteState
		{
			/// <summary>
			/// Chunk is still being appended to
			/// </summary>
			Writing = 0,

			/// <summary>
			/// Writer has moved to the next chunk
			/// </summary>
			MovedToNext = 2,

			/// <summary>
			/// This chunk marks the end of the stream
			/// </summary>
			Complete = 3,
		}

		/// <summary>
		/// Stores the state of a chunk in a 64-bit value, which can be updated atomically
		/// </summary>
		protected record struct ChunkState(ulong Value)
		{
			/// <summary>
			/// Written length of this chunk
			/// </summary>
			public int Length => (int)(Value & 0x7fffffff);

			/// <summary>
			/// Set of flags which are set for each reader that still has to read from a chunk
			/// </summary>
			public int ReaderFlags => (int)((Value >> 31) & 0x7fffffff);

			/// <summary>
			/// State of the writer
			/// </summary>
			public WriteState WriteState => (WriteState)(Value >> 62);

			/// <summary>
			/// Constructor
			/// </summary>
			public ChunkState(WriteState writerState, int readerFlags, int length) : this(((ulong)writerState << 62) | ((ulong)readerFlags << 31) | (uint)length) { }

			/// <summary>
			/// Test whether a particular reader is still referencing the chunk
			/// </summary>
			public bool HasReaderFlag(int readerIdx) => (Value & (1UL << (readerIdx + 31))) != 0;

			/// <inheritdoc/>
			public override string ToString() => $"{WriteState}, Length: {Length}, Readers: {ReaderFlags}";
		}

		/// <summary>
		/// Base class for chunks of data in the buffer
		/// </summary>
		protected abstract class ChunkBase
		{
			/// <summary>
			/// Accessor for the current state
			/// </summary>
			public ChunkState State => new ChunkState(Interlocked.CompareExchange(ref StateValue, 0, 0));

			/// <summary>
			/// Data underlying the chunk
			/// </summary>
			public Memory<byte> Memory { get; }

			/// <summary>
			/// Current state of this chunk
			/// </summary>
			protected abstract ref ulong StateValue { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			protected ChunkBase(Memory<byte> memory)
			{
				Memory = memory;
			}

			/// <summary>
			/// Append data to the chunk
			/// </summary>
			public void Append(int length) => Interlocked.Add(ref StateValue, (uint)length);

			/// <summary>
			/// Mark the chunk as being written to
			/// </summary>
			public void StartWriting(int numReaders) => Interlocked.Exchange(ref StateValue, new ChunkState(WriteState.Writing, (1 << numReaders) - 1, 0).Value);

			/// <summary>
			/// Move to the next chunk
			/// </summary>
			public void MoveToNext() => Interlocked.Or(ref StateValue, (ulong)WriteState.MovedToNext << 62);

			/// <summary>
			/// Move to the next chunk
			/// </summary>
			public void MarkComplete() => Interlocked.Or(ref StateValue, (ulong)WriteState.Complete << 62);

			/// <summary>
			/// Clear the reader flag
			/// </summary>
			public void FinishReading(int readerIdx) => Interlocked.And(ref StateValue, ~(1UL << (readerIdx + 31)));

			/// <inheritdoc/>
			public override string ToString() => State.ToString();
		}

		sealed class ReaderImpl : IComputeBufferReader
		{
			readonly ComputeBufferBase _buffer;
			readonly ChunkBase[] _chunks;
			readonly int _readerIdx;

			int _chunkIdx;
			int _offset;

			public bool IsComplete
			{
				get
				{
					ChunkState state = _chunks[_chunkIdx].State;
					return state.WriteState == WriteState.Complete && _offset == state.Length;
				}
			}

			public ReaderImpl(ComputeBufferBase buffer, ChunkBase[] chunks, int readerIdx)
			{
				_buffer = buffer;
				_chunks = chunks;
				_readerIdx = readerIdx;
			}

			/// <inheritdoc/>
			public void Advance(int length) => _offset += length;

			/// <inheritdoc/>
			public ReadOnlyMemory<byte> GetMemory()
			{
				ChunkBase chunk = _chunks[_chunkIdx];
				ChunkState state = chunk.State;
				return state.HasReaderFlag(_readerIdx) ? chunk.Memory.Slice(_offset, state.Length - _offset) : ReadOnlyMemory<byte>.Empty;
			}

			/// <inheritdoc/>
			public async ValueTask WaitToReadAsync(int currentLength, CancellationToken cancellationToken = default)
			{
				for (; ; )
				{
					ChunkBase chunk = _chunks[_chunkIdx];
					ChunkState state = chunk.State;

					if (!state.HasReaderFlag(_readerIdx))
					{
						// Wait until the current chunk is readable
						_buffer.ResetReadEvent(_readerIdx);
						if (!chunk.State.HasReaderFlag(_readerIdx))
						{
							await _buffer.WaitForReadEvent(_readerIdx, cancellationToken);
						}
					}
					else if (_offset + currentLength < state.Length || state.WriteState == WriteState.Complete)
					{
						// Still have data to read from this chunk
						break;
					}
					else if (_offset + currentLength >= state.Length && state.WriteState == WriteState.Writing)
					{
						// Wait until there is more data in the chunk
						_buffer.ResetReadEvent(_readerIdx);
						if (chunk.State == state)
						{
							await _buffer.WaitForReadEvent(_readerIdx, cancellationToken);
						}
					}
					else if (state.WriteState == WriteState.MovedToNext)
					{
						// Move to the next chunk
						chunk.FinishReading(_readerIdx);
						_buffer.SetWriteEvent();

						_chunkIdx++;
						if (_chunkIdx == _chunks.Length)
						{
							_chunkIdx = 0;
						}
						_offset = 0;
					}
					else
					{
						// Still need to read data from the current buffer
						break;
					}
				}
			}
		}

		sealed class WriterImpl : IComputeBufferWriter
		{
			readonly ComputeBufferBase _buffer;
			readonly ChunkBase[] _chunks;

			int _chunkIdx;

			public WriterImpl(ComputeBufferBase buffer, ChunkBase[] chunks)
			{
				_buffer = buffer;
				_chunks = chunks;
			}

			public void Advance(int size)
			{
				ChunkBase chunk = _chunks[_chunkIdx];
				chunk.Append(size);
				_buffer.SetAllReadEvents();
			}

			public Memory<byte> GetMemory()
			{
				ChunkBase chunk = _chunks[_chunkIdx];

				ChunkState state = chunk.State;
				if (state.WriteState != WriteState.Writing)
				{
					throw new InvalidOperationException();
				}

				return chunk.Memory.Slice(state.Length);
			}

			public void MarkComplete()
			{
				ChunkBase chunk = _chunks[_chunkIdx];
				chunk.MarkComplete();
				_buffer.SetAllReadEvents();
			}

			public async ValueTask WaitToWriteAsync(int currentLength, CancellationToken cancellationToken)
			{
				if (currentLength >= _chunks[_chunkIdx].Memory.Length)
				{
					throw new NotImplementedException("Will never succeed");
				}

				for (; ; )
				{
					Memory<byte> memory = GetMemory();
					if (memory.Length != currentLength)
					{
						break;
					}
					await MoveNextWriteChunk(cancellationToken);
				}
			}

			async ValueTask MoveNextWriteChunk(CancellationToken cancellationToken)
			{
				ChunkBase chunk = _chunks[_chunkIdx];
				chunk.MoveToNext();

				int nextWriteChunkIdx = _chunkIdx + 1;
				if (nextWriteChunkIdx == _chunks.Length)
				{
					nextWriteChunkIdx = 0;
				}

				ChunkBase nextChunk = _chunks[nextWriteChunkIdx];
				while (nextChunk.State.ReaderFlags != 0)
				{
					await _buffer.WaitForWriteEvent(cancellationToken);
					_buffer.ResetWriteEvent();
				}

				nextChunk.StartWriting(_buffer._numReaders);
				_chunkIdx = nextWriteChunkIdx;
			}
		}

		readonly ChunkBase[] _chunks;
		readonly ReaderImpl _reader;
		readonly WriterImpl _writer;
		readonly int _numReaders;

		/// <inheritdoc/>
		public IComputeBufferReader Reader => _reader;

		/// <inheritdoc/>
		public IComputeBufferWriter Writer => _writer;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="chunks">Data for the buffer</param>
		/// <param name="numReaders">Number of readers for this buffer</param>
		protected ComputeBufferBase(ChunkBase[] chunks, int numReaders)
		{
			_chunks = chunks;
			_chunks[0].StartWriting(numReaders);

			_numReaders = numReaders;

			_reader = new ReaderImpl(this, _chunks, 0);
			_writer = new WriterImpl(this, _chunks);
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
			for (int readerIdx = 0; readerIdx < _numReaders; readerIdx++)
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
