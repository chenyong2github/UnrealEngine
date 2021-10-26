// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Extension methods for MongoDB
	/// </summary>
	public static class MongoExtensions
	{
		/// <summary>
		/// Filters the documents returned from a search
		/// </summary>
		/// <param name="Query">The query to filter</param>
		/// <param name="Index">Index of the first document to return</param>
		/// <param name="Count">Number of documents to return</param>
		/// <returns>New query</returns>
		public static IFindFluent<TDocument, TProjection> Range<TDocument, TProjection>(this IFindFluent<TDocument, TProjection> Query, int? Index, int? Count)
		{
			if (Index != null && Index.Value != 0)
			{
				Query = Query.Skip(Index.Value);
			}
			if(Count != null)
			{
				Query = Query.Limit(Count.Value);
			}
			return Query;
		}

		/// <summary>
		/// Filters the documents returned from a search
		/// </summary>
		/// <param name="Query">The query to filter</param>
		/// <param name="Index">Index of the first document to return</param>
		/// <param name="Count">Number of documents to return</param>
		/// <returns>New query</returns>
		public static IAggregateFluent<TDocument> Range<TDocument>(this IAggregateFluent<TDocument> Query, int? Index, int? Count)
		{
			if (Index != null && Index.Value != 0)
			{
				Query = Query.Skip(Index.Value);
			}
			if (Count != null)
			{
				Query = Query.Limit(Count.Value);
			}
			return Query;
		}

		/// <summary>
		/// Filters the documents returned from a search
		/// </summary>
		/// <param name="Query">The query to filter</param>
		/// <returns>New query</returns>
		public static async Task<List<TResult>> ToListAsync<TDocument, TResult>(this IAsyncCursorSource<TDocument> Query) where TDocument : TResult
		{
			List<TResult> Results = new List<TResult>();
			using (IAsyncCursor<TDocument> Cursor = await Query.ToCursorAsync())
			{
				while (await Cursor.MoveNextAsync())
				{
					foreach(TDocument Document in Cursor.Current)
					{
						Results.Add(Document);
					}
				}
			}
			return Results;
		}

		/// <summary>
		/// Attempts to insert a document into a collection, handling the error case that a document with the given key already exists
		/// </summary>
		/// <typeparam name="TDocument"></typeparam>
		/// <param name="Collection">Collection to insert into</param>
		/// <param name="NewDocument">The document to insert</param>
		/// <returns>True if the document was inserted, false if it already exists</returns>
		public static async Task<bool> InsertOneIgnoreDuplicatesAsync<TDocument>(this IMongoCollection<TDocument> Collection, TDocument NewDocument)
		{
			try
			{
				await Collection.InsertOneAsync(NewDocument);
				return true;
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return false;
				}
				else
				{
					throw;
				}
			}
		}

		/// <summary>
		/// Attempts to insert a document into a collection, handling the error case that a document with the given key already exists
		/// </summary>
		/// <typeparam name="TDocument"></typeparam>
		/// <param name="Collection">Collection to insert into</param>
		/// <param name="NewDocuments">The document to insert</param>
		public static async Task InsertManyIgnoreDuplicatesAsync<TDocument>(this IMongoCollection<TDocument> Collection, List<TDocument> NewDocuments)
		{
			try
			{
				if (NewDocuments.Count > 0)
				{
					await Collection.InsertManyAsync(NewDocuments, new InsertManyOptions { IsOrdered = false });
				}
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category != ServerErrorCategory.DuplicateKey)
				{
					throw;
				}
			}
			catch (MongoBulkWriteException Ex)
			{
				if(Ex.WriteErrors.Any(x => x.Category != ServerErrorCategory.DuplicateKey))
				{
					throw;
				}
			}
		}

		/// <summary>
		/// Sets a field to a value, or unsets it if the value is null
		/// </summary>
		/// <typeparam name="TDocument">The document type</typeparam>
		/// <typeparam name="TField">Type of the field to set</typeparam>
		/// <param name="UpdateBuilder">Update builder</param>
		/// <param name="Field">Expression for the field to set</param>
		/// <param name="Value">New value to set</param>
		/// <returns>Update defintiion</returns>
		public static UpdateDefinition<TDocument> SetOrUnsetNull<TDocument, TField>(this UpdateDefinitionBuilder<TDocument> UpdateBuilder, Expression<Func<TDocument, TField?>> Field, TField? Value) where TField : struct
		{
			if (Value.HasValue)
			{
				return UpdateBuilder.Set(Field, Value.Value);
			}
			else
			{
				return UpdateBuilder.Unset(new ExpressionFieldDefinition<TDocument, TField?>(Field));
			}
		}

		/// <summary>
		/// Sets a field to a value, or unsets it if the value is null
		/// </summary>
		/// <typeparam name="TDocument">The document type</typeparam>
		/// <typeparam name="TField">Type of the field to set</typeparam>
		/// <param name="Update">Update builder</param>
		/// <param name="Field">Expression for the field to set</param>
		/// <param name="Value">New value to set</param>
		/// <returns>Update defintiion</returns>
		public static UpdateDefinition<TDocument> SetOrUnsetNull<TDocument, TField>(this UpdateDefinition<TDocument> Update, Expression<Func<TDocument, TField?>> Field, TField? Value) where TField : struct
		{
			if (Value.HasValue)
			{
				return Update.Set(Field, Value.Value);
			}
			else
			{
				return Update.Unset(new ExpressionFieldDefinition<TDocument, TField?>(Field));
			}
		}

		/// <summary>
		/// Sets a field to a value, or unsets it if the value is null
		/// </summary>
		/// <typeparam name="TDocument">The document type</typeparam>
		/// <typeparam name="TField">Type of the field to set</typeparam>
		/// <param name="UpdateBuilder">Update builder</param>
		/// <param name="Field">Expression for the field to set</param>
		/// <param name="Value">New value to set</param>
		/// <returns>Update defintiion</returns>
		public static UpdateDefinition<TDocument> SetOrUnsetNullRef<TDocument, TField>(this UpdateDefinitionBuilder<TDocument> UpdateBuilder, Expression<Func<TDocument, TField?>> Field, TField? Value) where TField : class
		{
			if (Value != null)
			{
				return UpdateBuilder.Set(Field, Value);
			}
			else
			{
				return UpdateBuilder.Unset(new ExpressionFieldDefinition<TDocument, TField?>(Field));
			}
		}

		/// <summary>
		/// Creates a filter definition from a linq expression. This is not generally explicitly castable, so expose it as a FilterDefinitionBuilder method.
		/// </summary>
		/// <typeparam name="TDocument">The document type</typeparam>
		/// <param name="Filter">The filter builder</param>
		/// <param name="Expression">Expression to parse</param>
		/// <returns>New filter definition</returns>
		public static FilterDefinition<TDocument> Expr<TDocument>(this FilterDefinitionBuilder<TDocument> Filter, Expression<Func<TDocument, bool>> Expression)
		{
			return Expression;
		}

		/// <summary>
		/// Converts a JsonElement to a BsonValue
		/// </summary>
		/// <param name="Element">The element to convert</param>
		/// <returns>Bson value</returns>
		public static BsonValue ToBsonValue(this JsonElement Element)
		{
			switch(Element.ValueKind)
			{
				case JsonValueKind.True:
					return true;
				case JsonValueKind.False:
					return false;
				case JsonValueKind.String:
					return Element.GetString();
				case JsonValueKind.Array:
					return new BsonArray(Element.EnumerateArray().Select(x => x.ToBsonValue()));
				case JsonValueKind.Object:
					return new BsonDocument(Element.EnumerateObject().Select(x => new BsonElement(x.Name, x.Value.ToBsonValue())));
				default:
					return BsonNull.Value;
			}
		}
	}
}
