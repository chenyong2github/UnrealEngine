// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Serialization;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Attribute used to annotate a message type with an identifier string
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class ComputeMessageAttribute : Attribute
	{
		/// <summary>
		/// Message type
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ComputeMessageAttribute(string name)
		{
			Name = name;
		}
	}

	/// <summary>
	/// Utility class for writing cb messages to a stream
	/// </summary>
	public sealed class ComputeMessageWriter
	{
		readonly IComputeChannel _channel;
		readonly CbWriter _writer;
		byte[] _buffer;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="channel"></param>
		/// <param name="reserve">Amount of data to reserve for serialization</param>
		public ComputeMessageWriter(IComputeChannel channel, int reserve = CbWriter.DefaultChunkSize)
		{
			_channel = channel;
			_writer = new CbWriter(reserve);
			_buffer = new byte[reserve];
		}

		/// <summary>
		/// Serializes a message to the channel
		/// </summary>
		/// <param name="message">Message to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task WriteCbMessageAsync(object message, CancellationToken cancellationToken)
		{
			ComputeMessage.Serialize(message, _writer);

			int size = _writer.GetSize();
			if (size > _buffer.Length)
			{
				_buffer = new byte[size + 1024];
			}
			_writer.CopyTo(_buffer);

			await _channel.WriteMessageAsync(_buffer.AsMemory(0, size), cancellationToken);
		}
	}

	/// <summary>
	/// Utility methods for compute messages
	/// </summary>
	public static class ComputeMessage
	{
		static readonly Utf8String s_typeField = "name";
		static readonly Utf8String s_dataField = "data";

		static readonly Type[] s_messageTypes =
		{
			typeof(CloseMessage),
			typeof(XorRequestMessage),
			typeof(XorResponseMessage),
			typeof(CppComputeMessage),
			typeof(CppComputeOutputMessage),
			typeof(CppComputeFinishMessage),
			typeof(BlobReadMessage),
			typeof(BlobDataMessage)
		};

		static readonly Dictionary<Utf8String, ICbConverter> s_typeToConverter = CreateTypeToConverterMap();

		/// <summary>
		/// Build a lookup of all known message types
		/// </summary>
		static Dictionary<Utf8String, ICbConverter> CreateTypeToConverterMap()
		{
			Dictionary<Utf8String, ICbConverter> typeToConverter = new Dictionary<Utf8String, ICbConverter>();
			foreach (Type messageType in s_messageTypes)
			{
				string name = messageType.GetCustomAttribute<ComputeMessageAttribute>()!.Name;
				ICbConverter converter = CbConverter.GetConverter(messageType);
				typeToConverter.Add(name, converter);
			}
			return typeToConverter;
		}

		static class TagCache<T>
		{
			public static string Tag { get; } = GetTag(typeof(T));
		}

		/// <summary>
		/// Gets the tag for a particular message class
		/// </summary>
		/// <typeparam name="T">Type of the message</typeparam>
		/// <returns>Tag name for the message</returns>
		public static string GetTag<T>()
		{
			return TagCache<T>.Tag;
		}

		/// <summary>
		/// Gets the tag for a particular message class
		/// </summary>
		/// <param name="type">Type of the message</param>
		/// <returns>Tag for the type</returns>
		public static string GetTag(Type type)
		{
			return type.GetCustomAttribute<ComputeMessageAttribute>()!.Name;
		}

		/// <summary>
		/// Serializes a message
		/// </summary>
		/// <param name="message">Message to serialize</param>
		/// <param name="writer">Writer for the message</param>
		public static void Serialize(object message, CbWriter writer)
		{
			Utf8String name = GetTag(message.GetType());

			writer.Clear();
			writer.BeginObject();
			writer.WriteUtf8String(s_typeField, name);

			ICbConverter converter = CbConverter.GetConverter(message.GetType());
			converter.WriteNamedObject(writer, s_dataField, message);

			writer.EndObject();
		}

		/// <summary>
		/// Deserialize a message from a block of memory
		/// </summary>
		/// <param name="memory"></param>
		/// <returns></returns>
		public static object Deserialize(ReadOnlyMemory<byte> memory)
		{
			CbObject obj = new CbObject(memory);

			CbField typeField = obj.Find(s_typeField);
			Utf8String type = typeField.AsUtf8String();

			ICbConverter converter = s_typeToConverter[type];

			CbField dataField = obj.Find(s_dataField);
			return converter.ReadObject(dataField)!;
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="channel">Channel to read from</param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async Task<object?> ReadCbMessageAsync(this IComputeChannel channel, CancellationToken cancellationToken)
		{
			ReadOnlyMemory<byte> message = await channel.ReadMessageAsync(cancellationToken);
			if (message.Length == 0)
			{
				return null;
			}
			return ComputeMessage.Deserialize(message.ToArray());
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="channel">Channel to read from</param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async Task<T> ReadCbMessageAsync<T>(this IComputeChannel channel, CancellationToken cancellationToken)
		{
			object? message = await ReadCbMessageAsync(channel, cancellationToken);
			if (message == null)
			{
				throw new EndOfStreamException();
			}
			return (T)message;
		}

		/// <summary>
		/// Receives a message from the remote
		/// </summary>
		/// <param name="channel">Channel to read from</param>
		/// <param name="message">Message to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task WriteCbMessageAsync(this IComputeChannel channel, object message, CancellationToken cancellationToken)
		{
			CbWriter writer = new CbWriter();
			Serialize(message, writer);

			int size = writer.GetSize();
			using (IMemoryOwner<byte> buffer = MemoryPool<byte>.Shared.Rent(size))
			{
				writer.CopyTo(buffer.Memory.Span);
				await channel.WriteMessageAsync(buffer.Memory.Slice(0, size), cancellationToken);
			}
		}
	}
}
