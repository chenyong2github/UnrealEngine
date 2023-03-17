// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.Security.Claims;
using System.Text.Json;
using System.Text.Json.Serialization;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization.Serializers;

namespace Horde.Server.Acls
{
	/// <summary>
	/// Interface for parent classes that implement authorization
	/// </summary>
	public interface IAclScope
	{
		/// <summary>
		/// The parent scope object
		/// </summary>
		IAclScope? ParentScope { get; }

		/// <summary>
		/// Name of this scope
		/// </summary>
		AclScopeName ScopeName { get; }

		/// <summary>
		/// ACL for this scope
		/// </summary>
		AclConfig? Acl { get; }
	}

	/// <summary>
	/// Name of an ACL scope
	/// </summary>
	[DebuggerDisplay("{Name}")]
	[JsonConverter(typeof(AclScopeNameJsonConverter))]
	[BsonSerializer(typeof(AclScopeNameBsonSerializer))]
	public record struct AclScopeName(string Text)
	{
		/// <summary>
		/// The root scope name
		/// </summary>
		public static AclScopeName Root { get; } = new AclScopeName("horde");

		/// <summary>
		/// Append another name to this scope
		/// </summary>
		/// <param name="name">Name to append</param>
		/// <returns>New scope name</returns>
		public AclScopeName Append(string name) => new AclScopeName($"{Text}/{name}");

		/// <summary>
		/// Append another name to this scope
		/// </summary>
		/// <param name="type">Type of the scope</param>
		/// <param name="name">Name to append</param>
		/// <returns>New scope name</returns>
		public AclScopeName Append(string type, string name) => new AclScopeName($"{Text}/{type}:{name}");

		/// <inheritdoc/>
		public bool Equals(AclScopeName other) => Text.Equals(other.Text, StringComparison.Ordinal);

		/// <inheritdoc/>
		public override int GetHashCode() => Text.GetHashCode(StringComparison.Ordinal);
	}

	/// <summary>
	/// Serializes <see cref="AclScopeName"/> objects to JSON
	/// </summary>
	class AclScopeNameJsonConverter : JsonConverter<AclScopeName>
	{
		/// <inheritdoc/>
		public override AclScopeName Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options) => new AclScopeName(reader.GetString() ?? String.Empty);

		/// <inheritdoc/>
		public override void Write(Utf8JsonWriter writer, AclScopeName value, JsonSerializerOptions options) => writer.WriteStringValue(value.Text);
	}

	/// <summary>
	/// Serializes <see cref="AclScopeName"/> objects to JSON
	/// </summary>
	class AclScopeNameBsonSerializer : SerializerBase<AclScopeName> 
	{
		/// <inheritdoc/>
		public override AclScopeName Deserialize(BsonDeserializationContext context, BsonDeserializationArgs args) => new AclScopeName(context.Reader.ReadString());

		/// <inheritdoc/>
		public override void Serialize(BsonSerializationContext context, BsonSerializationArgs args, AclScopeName value) => context.Writer.WriteString(value.Text);
	}

	/// <summary>
	/// Extension methods for ACL scopes
	/// </summary>
	public static class AclScopeExtensions
	{
		/// <summary>
		/// Authorize a particular operation against a scope
		/// </summary>
		/// <param name="scope">Scope to query</param>
		/// <param name="action">Action to check</param>
		/// <param name="principal">Principal to authorize</param>
		/// <returns>True if the given principal is allowed to perform a particular action</returns>
		public static bool Authorize(this IAclScope scope, AclAction action, ClaimsPrincipal principal)
		{
			for (IAclScope? next = scope; next != null; next = next.ParentScope)
			{
				if (next.Acl != null)
				{
					bool? result = next.Acl.Authorize(action, principal);
					if (result.HasValue)
					{
						return result.Value;
					}
				}
			}
			return false;
		}
	}
}
