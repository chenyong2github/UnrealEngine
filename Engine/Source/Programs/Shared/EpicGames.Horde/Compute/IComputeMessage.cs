// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Buffers;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Nodes;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Type of a compute message
	/// </summary>
	public enum ComputeMessageType
	{
		/// <summary>
		/// No message was received (end of stream)
		/// </summary>
		None = 0x00,

		/// <summary>
		/// Sent in place of a regular response if an error occurs on the remote
		/// </summary>
		Exception = 0x01,

		/// <summary>
		/// Fork a new request channel
		/// </summary>
		Fork = 0x02,

		/// <summary>
		/// Untyped user data
		/// </summary>
		UserData = 0x03,

		#region Process Management

		/// <summary>
		/// Extract files on the remote machine (Initiator -> Remote)
		/// </summary>
		WriteFiles = 0x10,

		/// <summary>
		/// Notification that files have been extracted (Remote -> Initiator)
		/// </summary>
		WriteFilesResponse = 0x11,

		/// <summary>
		/// Deletes files on the remote machine (Initiator -> Remote)
		/// </summary>
		DeleteFiles = 0x12,

		/// <summary>
		/// Delete a directory in the remote (Initiator -> Remote)
		/// </summary>
		DeleteDirectory = 0x13,

		/// <summary>
		/// Execute a process in a sandbox (Initiator -> Remote)
		/// </summary>
		ExecuteProcess = 0x16,

		/// <summary>
		/// Returns the process exit code (Remote -> Initiator)
		/// </summary>
		ExecuteProcessResponse = 0x17,

		/// <summary>
		/// Returns output from the child process to the caller (Remote -> Initiator)
		/// </summary>
		ProcessOutput = 0x18,

		#endregion

		#region Storage

		/// <summary>
		/// Reads a blob from storage
		/// </summary>
		ReadBlob = 0x20,

		/// <summary>
		/// Response to a <see cref="ReadBlob"/> request.
		/// </summary>
		ReadBlobResponse = 0x21,

		#endregion

		#region Buffers

		/// <summary>
		/// Creates a new receive buffer for messages
		/// </summary>
		CreateBufferRequest = 0x30,

		/// <summary>
		/// Receives information about the new receive buffer
		/// </summary>
		CreateBufferResponse = 0x31,

		#endregion

		#region Test Requests

		/// <summary>
		/// Xor a block of data with a value
		/// </summary>
		XorRequest = 0xf0,

		/// <summary>
		/// Result from an <see cref="XorRequest"/> request.
		/// </summary>
		XorResponse = 0xf1,

		#endregion
	}

	/// <summary>
	/// Information about a received compute message
	/// </summary>
	public interface IComputeMessage : IMemoryReader, IDisposable
	{
		/// <summary>
		/// Type of the message
		/// </summary>
		public ComputeMessageType Type { get; }

		/// <summary>
		/// Data that was read
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }
	}

	/// <summary>
	/// Message for reporting an error
	/// </summary>
	public record struct ExceptionMessage(string Message, string Description);

	/// <summary>
	/// Message to request forking the message loop
	/// </summary>
	public record struct ForkMessage(int ChannelId);

	/// <summary>
	/// Extract files from a bundle to a path in the remote sandbox
	/// </summary>
	/// <param name="Name">Path to extract the files to</param>
	/// <param name="Locator">Locator for the tree to extract</param>
	public record struct UploadFilesMessage(string Name, NodeLocator Locator);

	/// <summary>
	/// Deletes files or directories in the remote
	/// </summary>
	/// <param name="Filter">Filter for files to delete</param>
	public record struct DeleteFilesMessage(IReadOnlyList<string> Filter);

	/// <summary>
	/// Message to execute a new child process
	/// </summary>
	/// <param name="Executable">Executable path</param>
	/// <param name="Arguments">Arguments for the executable</param>
	/// <param name="WorkingDir">Working directory to execute in</param>
	/// <param name="EnvVars">Environment variables for the child process. Null values unset variables.</param>
	public record struct ExecuteProcessMessage(string Executable, IReadOnlyList<string> Arguments, string? WorkingDir, IReadOnlyDictionary<string, string?> EnvVars);

	/// <summary>
	/// Response from executing a child process
	/// </summary>
	/// <param name="ExitCode">Exit code for the process</param>
	public record struct ExecuteProcessResponseMessage(int ExitCode);

	/// <summary>
	/// Creates a blob read request
	/// </summary>
	public record struct ReadBlobMessage(BlobLocator Locator, int Offset, int Length);

	/// <summary>
	/// Request creation of a new buffer, and attaching it to the lease
	/// </summary>
	public record struct CreateBufferRequest(int ChannelId, long Capacity, bool Send);

	/// <summary>
	/// Response to create a new buffer
	/// </summary>
	public record struct CreateBufferResponse(string Handle);

	/// <summary>
	/// Message for running an XOR command
	/// </summary>
	/// <param name="Data">Data to xor</param>
	/// <param name="Value">Value to XOR with</param>
	public record struct XorRequestMessage(ReadOnlyMemory<byte> Data, byte Value);

	/// <summary>
	/// Wraps various requests across compute channels
	/// </summary>
	public static class ComputeMessageExtensions
	{
		/// <summary>
		/// Sends an exception response to the remote
		/// </summary>
		public static void Exception(this IComputeChannel channel, Exception ex) => Exception(channel, ex.Message, ex.StackTrace);

		/// <summary>
		/// Sends an exception response to the remote
		/// </summary>
		public static void Exception(this IComputeChannel channel, string description, string? trace)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.Exception))
			{
				builder.WriteString(description);
				builder.WriteOptionalString(trace);
				builder.Send();
			}
		}

		/// <summary>
		/// Parses a message as an <see cref="ExceptionMessage"/>
		/// </summary>
		public static ExceptionMessage ParseExceptionMessage(this IComputeMessage message)
		{
			string msg = message.ReadString();
			string description = message.ReadString();
			return new ExceptionMessage(msg, description);
		}

		/// <inheritdoc/>
		public static void Fork(this IComputeChannel channel, int newChannelId)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.Fork))
			{
				builder.WriteInt32(newChannelId);
				builder.Send();
			}
		}

		/// <summary>
		/// Parses a fork message from the given compute message
		/// </summary>
		public static ForkMessage ParseForkMessage(this IComputeMessage message)
		{
			int channelId = message.ReadInt32();
			return new ForkMessage(channelId);
		}

		/// <summary>
		/// Sends untyped user data to the remote
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="span">Data to send</param>
		public static void SendUserData(this IComputeChannel channel, ReadOnlySpan<byte> span)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.UserData))
			{
				builder.WriteFixedLengthBytes(span);
				builder.Send();
			}
		}

		#region Process

		static async Task<IComputeMessage> RunStorageServer(this IComputeChannel channel, IStorageClient storage, CancellationToken cancellationToken)
		{
			for (; ; )
			{
				IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
				if (message.Type != ComputeMessageType.ReadBlob)
				{
					return message;
				}

				try
				{
					ReadBlobMessage readBlob = message.ParseReadBlobRequest();
					await SendBlobDataAsync(channel, readBlob, storage, cancellationToken);
				}
				finally
				{
					message.Dispose();
				}
			}
		}

		/// <summary>
		/// Creates a sandbox on the remote machine
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="path">Root directory to extract files within the sandbox</param>
		/// <param name="locator">Location of a <see cref="DirectoryNode"/> describing contents of the sandbox</param>
		/// <param name="storage">Storage for the sandbox data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task UploadFilesAsync(this IComputeChannel channel, string path, NodeLocator locator, IStorageClient storage, CancellationToken cancellationToken)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.WriteFiles))
			{
				writer.WriteString(path);
				writer.WriteNodeLocator(locator);
				writer.Send();
			}

			using IComputeMessage response = await RunStorageServer(channel, storage, cancellationToken);
			if (response.Type != ComputeMessageType.WriteFilesResponse)
			{
				throw new NotSupportedException();
			}
		}

		/// <summary>
		/// Parses a message as a <see cref="UploadFilesMessage"/>
		/// </summary>
		public static UploadFilesMessage ParseUploadFilesMessage(this IComputeMessage message)
		{
			string name = message.ReadString();
			NodeLocator locator = message.ReadNodeLocator();
			return new UploadFilesMessage(name, locator);
		}

		/// <summary>
		/// Destroys a sandbox on the remote machine
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="paths">Paths of files or directories to delete</param>
		public static void DeleteFiles(this IComputeChannel channel, IReadOnlyList<string> paths)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.DeleteDirectory))
			{
				writer.WriteList(paths, MemoryWriterExtensions.WriteString);
				writer.Send();
			}
		}

		/// <summary>
		/// Parses a message as a <see cref="DeleteFilesMessage"/>
		/// </summary>
		public static DeleteFilesMessage ParseDeleteFilesMessage(this IComputeMessage message)
		{
			List<string> files = message.ReadList(MemoryReaderExtensions.ReadString);
			return new DeleteFilesMessage(files);
		}

		/// <summary>
		/// Executes a remote process
		/// </summary>
		/// <param name="channel">Current channel</param>
		/// <param name="executable">Executable to run, relative to the sandbox root</param>
		/// <param name="arguments">Arguments for the child process</param>
		/// <param name="workingDir">Working directory for the process</param>
		/// <param name="outputHandler">Output callback for stdout</param>
		/// <param name="envVars">Environment variables for the child process</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task<int> ExecuteProcessAsync(this IComputeChannel channel, string executable, IReadOnlyList<string> arguments, string? workingDir, IReadOnlyDictionary<string, string?>? envVars, Action<ReadOnlyMemory<byte>> outputHandler, CancellationToken cancellationToken)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.ExecuteProcess))
			{
				writer.WriteString(executable);
				writer.WriteList(arguments, MemoryWriterExtensions.WriteString);
				writer.WriteOptionalString(workingDir);
				writer.WriteDictionary(envVars ?? new Dictionary<string, string?>(), MemoryWriterExtensions.WriteString, MemoryWriterExtensions.WriteOptionalString);
				writer.Send();
			}

			for (; ; )
			{
				using IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
				switch (message.Type)
				{
					case ComputeMessageType.Exception:
						ExceptionMessage exception = message.ParseExceptionMessage();
						throw new ComputeRemoteException(exception);
					case ComputeMessageType.ProcessOutput:
						outputHandler(message.Data);
						break;
					case ComputeMessageType.ExecuteProcessResponse:
						ExecuteProcessResponseMessage executeProcessResponse = message.ParseExecuteProcessResponse();
						return executeProcessResponse.ExitCode;
					default:
						throw new ComputeInvalidMessageException(message);
				}
			}
		}

		/// <summary>
		/// Parses a message as a <see cref="ExecuteProcessMessage"/>
		/// </summary>
		public static ExecuteProcessMessage ParseExecuteProcessMessage(this IComputeMessage message)
		{
			string executable = message.ReadString();
			List<string> arguments = message.ReadList(MemoryReaderExtensions.ReadString);
			string? workingDir = message.ReadOptionalString();
			Dictionary<string, string?> envVars = message.ReadDictionary(MemoryReaderExtensions.ReadString, MemoryReaderExtensions.ReadOptionalString);
			return new ExecuteProcessMessage(executable, arguments, workingDir, envVars);
		}

		/// <summary>
		/// Sends output from a child process
		/// </summary>
		public static void SendProcessOutput(this IComputeChannel channel, ReadOnlyMemory<byte> data)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.ProcessOutput))
			{
				builder.WriteFixedLengthBytes(data.Span);
				builder.Send();
			}
		}

		/// <summary>
		/// Sends a response from executing a child process
		/// </summary>
		/// <param name="channel"></param>
		/// <param name="exitCode">Exit code from the process</param>
		public static void SendExecuteProcessResponse(this IComputeChannel channel, int exitCode)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.ExecuteProcessResponse))
			{
				builder.WriteInt32(exitCode);
				builder.Send();
			}
		}

		/// <summary>
		/// Parses a message as a <see cref="ExecuteProcessMessage"/>
		/// </summary>
		public static ExecuteProcessResponseMessage ParseExecuteProcessResponse(this IComputeMessage message)
		{
			int exitCode = message.ReadInt32();
			return new ExecuteProcessResponseMessage(exitCode);
		}

		#endregion

		#region Storage

		/// <summary>
		/// 
		/// </summary>
		/// <param name="message"></param>
		/// <returns></returns>
		public static ReadBlobMessage ParseReadBlobRequest(this IComputeMessage message)
		{
			BlobLocator locator = message.ReadBlobLocator();
			int offset = (int)message.ReadUnsignedVarInt();
			int length = (int)message.ReadUnsignedVarInt();
			return new ReadBlobMessage(locator, offset, length);
		}

		/// <summary>
		/// Wraps a compute message containing blob data
		/// </summary>
		sealed class BlobDataStream : ReadOnlyMemoryStream
		{
			readonly IComputeMessage _message;

			public BlobDataStream(IComputeMessage message)
				: base(message.Data)
			{
				_message = message;
			}

			protected override void Dispose(bool disposing)
			{
				base.Dispose(disposing);

				if (disposing)
				{
					_message.Dispose();
				}
			}
		}

		/// <summary>
		/// Reads a blob from the remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob</param>
		/// <param name="offset">Offset within the blob</param>
		/// <param name="length">Length of data to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream containing the blob data</returns>
		public static async Task<Stream> ReadBlobAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, CancellationToken cancellationToken)
		{
			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.ReadBlob))
			{
				writer.WriteBlobLocator(locator);
				writer.WriteUnsignedVarInt(offset);
				writer.WriteUnsignedVarInt(length);
				writer.Send();
			}

			IComputeMessage response = await channel.ReceiveAsync(cancellationToken);
			if (response.Type != ComputeMessageType.ReadBlobResponse)
			{
				response.Dispose();
				throw new ComputeInvalidMessageException(response);
			}

			return new BlobDataStream(response);
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="message">The read request</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task SendBlobDataAsync(this IComputeChannel channel, ReadBlobMessage message, IStorageClient storage, CancellationToken cancellationToken)
		{
			return SendBlobDataAsync(channel, message.Locator, message.Offset, message.Length, storage, cancellationToken);
		}

		/// <summary>
		/// Writes blob data to a compute channel
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="locator">Locator for the blob to send</param>
		/// <param name="offset">Starting offset of the data</param>
		/// <param name="length">Length of the data</param>
		/// <param name="storage">Storage client to retrieve the blob from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task SendBlobDataAsync(this IComputeChannel channel, BlobLocator locator, int offset, int length, IStorageClient storage, CancellationToken cancellationToken)
		{
			byte[] data;
			if (offset == 0 && length == 0)
			{
				using (Stream stream = await storage.ReadBlobAsync(locator, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}
			else
			{
				using (Stream stream = await storage.ReadBlobRangeAsync(locator, offset, length, cancellationToken))
				{
					using MemoryStream target = new MemoryStream();
					await stream.CopyToAsync(target, cancellationToken);
					data = target.ToArray();
				}
			}

			using (IComputeMessageBuilder writer = channel.CreateMessage(ComputeMessageType.ReadBlobResponse, length + 128))
			{
				writer.WriteFixedLengthBytes(data);
				writer.Send();
			}
		}

		#endregion

		#region Child process

		/// <summary>
		/// Wrapper around the lifetime of an <see cref="IpcBuffer"/> which only exposes the writer interface
		/// </summary>
		class WrappedBufferWriter : IComputeBufferWriter
		{
			readonly IpcBuffer _buffer;

			public WrappedBufferWriter(IpcBuffer buffer) => _buffer = buffer;

			public void Dispose() => _buffer.Dispose();

			public void Advance(int size) => _buffer.Writer.Advance(size);

			public ValueTask FlushAsync(CancellationToken cancellationToken) => _buffer.Writer.FlushAsync(cancellationToken);

			public Memory<byte> GetMemory() => _buffer.Writer.GetMemory();

			public void MarkComplete() => _buffer.Writer.MarkComplete();
		}

		/// <summary>
		/// Creates a buffer which can be used to send data to the remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="id">Identifier for the new buffer</param>
		/// <param name="capacity">Capacity of the buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New compute buffer</returns>
		public static async Task<IComputeBufferWriter> CreateSendBufferAsync(this IComputeChannel channel, int id, long capacity, CancellationToken cancellationToken)
		{
			IpcBuffer buffer = await CreateBufferAsync(channel, id, capacity, true, cancellationToken);
			return new WrappedBufferWriter(buffer);
		}

		/// <summary>
		/// Wrapper around the lifetime of an <see cref="IpcBuffer"/> which only exposes the reader interface
		/// </summary>
		class WrappedBufferReader : IComputeBufferReader
		{
			readonly IpcBuffer _buffer;

			public WrappedBufferReader(IpcBuffer buffer) => _buffer = buffer;

			public void Dispose() => _buffer.Dispose();

			public bool IsComplete => _buffer.Reader.IsComplete;

			public void Advance(int size) => _buffer.Reader.Advance(size);

			public ReadOnlyMemory<byte> GetMemory() => _buffer.Reader.GetMemory();

			public ValueTask WaitAsync(int currentLength, CancellationToken cancellationToken) => _buffer.Reader.WaitAsync(currentLength, cancellationToken);
		}

		/// <summary>
		/// Creates a buffer which can be used to receive data from the remote
		/// </summary>
		/// <param name="channel">Channel to write to</param>
		/// <param name="id">Identifier for the new buffer</param>
		/// <param name="capacity">Capacity of the buffer</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New compute buffer</returns>
		public static async Task<IComputeBufferReader> CreateReceiveBufferAsync(this IComputeChannel channel, int id, long capacity, CancellationToken cancellationToken)
		{
			IpcBuffer buffer = await CreateBufferAsync(channel, id, capacity, false, cancellationToken);
			return new WrappedBufferReader(buffer);
		}

		static async Task<IpcBuffer> CreateBufferAsync(this IComputeChannel channel, int id, long capacity, bool send, CancellationToken cancellationToken)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.CreateBufferRequest))
			{
				builder.WriteInt32(id);
				builder.WriteInt64(capacity);
				builder.WriteBoolean(send);
				builder.Send();
			}

			using IComputeMessage response = await channel.ReceiveAsync(cancellationToken);
			if (response.Type != ComputeMessageType.CreateBufferResponse)
			{
				throw new NotSupportedException();
			}

			string handle = response.ReadString();
			return IpcBuffer.OpenExisting(handle);
		}

		/// <summary>
		/// Parse a <see cref="CreateBufferRequest"/> message
		/// </summary>
		public static CreateBufferRequest ParseCreateBufferRequest(this IComputeMessage message)
		{
			int channelId = message.ReadInt32();
			long capacity = message.ReadInt64();
			bool send = message.ReadBoolean();
			return new CreateBufferRequest(channelId, capacity, send);
		}

		#endregion

		#region Test Messages

		/// <summary>
		/// Send a message to request that a byte string be xor'ed with a particular value
		/// </summary>
		public static void XorRequest(this IComputeChannel channel, ReadOnlyMemory<byte> data, byte value)
		{
			using (IComputeMessageBuilder builder = channel.CreateMessage(ComputeMessageType.XorRequest))
			{
				builder.WriteFixedLengthBytes(data.Span);
				builder.WriteUInt8(value);
				builder.Send();
			}
		}

		/// <summary>
		/// Parse a message as an XOR request
		/// </summary>
		public static XorRequestMessage AsXorRequest(this IComputeMessage message)
		{
			ReadOnlyMemory<byte> data = message.Data;
			return new XorRequestMessage(data[0..^1], data.Span[^1]);
		}

		#endregion
	}
}
