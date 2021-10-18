// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Redis;
using HordeServer.Models;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization.Serializers;
using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Normalized hostname of an agent
	/// </summary>
	[RedisConverter(typeof(AgentIdRedisConverter))]
	[BsonSerializer(typeof(AgentIdBsonSerializer))]
	public struct AgentId : IEquatable<AgentId>
	{
		/// <summary>
		/// The text representing this id
		/// </summary>
		readonly string Name;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Hostname of the agent</param>
		public AgentId(string Name)
		{
			if (Name.Length == 0)
			{
				throw new ArgumentException("Agent name may not be empty");
			}

			char[] Result = new char[Name.Length];
			for (int Idx = 0; Idx < Name.Length; Idx++)
			{
				char Character = Name[Idx];
				if ((Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-')
				{
					Result[Idx] = Character;
				}
				else if (Character >= 'a' && Character <= 'z')
				{
					Result[Idx] = (char)('A' + (Character - 'a'));
				}
				else if (Character == '.')
				{
					if (Idx == 0)
					{
						throw new ArgumentException("Agent name may not begin with the '.' character");
					}
					else if (Idx == Name.Length - 1)
					{
						throw new ArgumentException("Agent name may not end with the '.' character");
					}
					else if (Name[Idx - 1] == '.')
					{
						throw new ArgumentException("Agent name may not contain two consecutive '.' characters");
					}
					else
					{
						Result[Idx] = Character;
					}
				}
				else
				{
					throw new ArgumentException($"Character '{Character}' in agent '{Name}' is invalid");
				}
			}
			this.Name = new string(Result);
		}

		/// <inheritdoc/>
		public override bool Equals(object? Obj)
		{
			return Obj is AgentId && Equals((AgentId)Obj);
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return Name.GetHashCode(StringComparison.Ordinal);
		}

		/// <inheritdoc/>
		public bool Equals(AgentId Other)
		{
			return Name.Equals(Other.Name, StringComparison.Ordinal);
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return Name;
		}

		/// <summary>
		/// Compares two string ids for equality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are equal</returns>
		public static bool operator ==(AgentId Left, AgentId Right)
		{
			return Left.Equals(Right);
		}

		/// <summary>
		/// Compares two string ids for inequality
		/// </summary>
		/// <param name="Left">The first string id</param>
		/// <param name="Right">Second string id</param>
		/// <returns>True if the two string ids are not equal</returns>
		public static bool operator !=(AgentId Left, AgentId Right)
		{
			return !Left.Equals(Right);
		}
	}

	/// <summary>
	/// Converter to/from Redis values
	/// </summary>
	public sealed class AgentIdRedisConverter : IRedisConverter<AgentId>
	{
		/// <inheritdoc/>
		public AgentId FromRedisValue(RedisValue Value) => new AgentId((string)Value);

		/// <inheritdoc/>
		public RedisValue ToRedisValue(AgentId Value) => Value.ToString();
	}

	/// <summary>
	/// Serializer for StringId objects
	/// </summary>
	public sealed class AgentIdBsonSerializer : SerializerBase<AgentId>
	{
		/// <inheritdoc/>
		public override AgentId Deserialize(BsonDeserializationContext Context, BsonDeserializationArgs Args)
		{
			string Argument;
			if (Context.Reader.CurrentBsonType == MongoDB.Bson.BsonType.ObjectId)
			{
				Argument = Context.Reader.ReadObjectId().ToString();
			}
			else
			{
				Argument = Context.Reader.ReadString();
			}
			return new AgentId(Argument);
		}

		/// <inheritdoc/>
		public override void Serialize(BsonSerializationContext Context, BsonSerializationArgs Args, AgentId Value)
		{
			Context.Writer.WriteString(Value.ToString());
		}
	}

	/// <summary>
	/// Extension methods for AgentId types
	/// </summary>
	static class AgentIdExtensions
	{
		/// <summary>
		/// Create an AgentId from a string
		/// </summary>
		/// <param name="Text">String to parse</param>
		/// <returns>New agent id</returns>
		public static AgentId ToAgentId(this string Text)
		{
			return new AgentId(Text);
		}
	}
}
