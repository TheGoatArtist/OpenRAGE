/*
===========================================================================

OpenRAGE GPL Source Code
Copyright (C) 2026 OpenRAGE contributors

This file is part of OpenRAGE. OpenRAGE is free software: you can redistribute it and/or
modify it under the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version, with the
additional terms in COPYING.txt.

===========================================================================
*/

// .entities reader. See docs/research/formats/maps.md.
//
// The file is "Version 3" followed by `entity { ... }` blocks. Inside, an entity has an
// optional `layers { "..." }` list and one `entityDef <name> { ... }` block of settings:
// `key = value;`, `key = { ... }`, `key = ! { ... }`, and typed keys such as
// `"float" gravity = 1066.0;`. This reader builds a small tree and pulls out the fields
// the engine needs so far.

#include "RageFormats.h"

#include <cstdlib>
#include <memory>

namespace rage
{

namespace
{

enum TokenType
{
	TOKEN_END,
	TOKEN_WORD,		// identifiers, numbers and keys such as mat[0]
	TOKEN_STRING,	// "quoted"
	TOKEN_PUNCT		// { } = ; !
};

struct Token
{
	TokenType	type = TOKEN_END;
	std::string	text;
};

class Lexer
{
public:
	Lexer( const char* text_, size_t size_ ) : text( text_ ), size( size_ ) {}

	Token Next()
	{
		SkipSpaceAndComments();
		Token token;
		if( pos >= size )
		{
			return token;
		}
		const char c = text[pos];
		if( c == '{' || c == '}' || c == '=' || c == ';' || c == '!' )
		{
			token.type = TOKEN_PUNCT;
			token.text = c;
			pos++;
			return token;
		}
		if( c == '"' )
		{
			pos++;
			const size_t start = pos;
			while( pos < size && text[pos] != '"' )
			{
				pos++;
			}
			token.type = TOKEN_STRING;
			token.text.assign( text + start, pos - start );
			if( pos < size )
			{
				pos++;	// closing quote
			}
			return token;
		}
		const size_t start = pos;
		while( pos < size && !IsSpace( text[pos] ) && text[pos] != '{' && text[pos] != '}' && text[pos] != '=' && text[pos] != ';' && text[pos] != '"' )
		{
			pos++;
		}
		token.type = TOKEN_WORD;
		token.text.assign( text + start, pos - start );
		return token;
	}

	Token Peek()
	{
		const size_t saved = pos;
		Token token = Next();
		pos = saved;
		return token;
	}

	Token PeekSecond()
	{
		const size_t saved = pos;
		Next();
		Token token = Next();
		pos = saved;
		return token;
	}

private:
	static bool IsSpace( char c )
	{
		return c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	void SkipSpaceAndComments()
	{
		while( pos < size )
		{
			if( IsSpace( text[pos] ) )
			{
				pos++;
			}
			else if( text[pos] == '/' && pos + 1 < size && text[pos + 1] == '/' )
			{
				while( pos < size && text[pos] != '\n' )
				{
					pos++;
				}
			}
			else if( text[pos] == '/' && pos + 1 < size && text[pos + 1] == '*' )
			{
				pos += 2;
				while( pos + 1 < size && !( text[pos] == '*' && text[pos + 1] == '/' ) )
				{
					pos++;
				}
				pos += 2;
			}
			else
			{
				break;
			}
		}
	}

	const char*	text;
	size_t		size;
	size_t		pos = 0;
};

struct Node
{
	std::string							key;
	std::string							value;		// a scalar value, or the name after the key ("entityDef <name>")
	std::vector<std::unique_ptr<Node>>	children;

	const Node* Child( const char* name ) const
	{
		for( const auto& child : children )
		{
			if( child->key == name )
			{
				return child.get();
			}
		}
		return nullptr;
	}
};

const int MAX_DEPTH = 64;

// Parses block contents up to and including the closing brace
bool ParseBlock( Lexer& lexer, Node& parent, int depth, std::string& error )
{
	if( depth > MAX_DEPTH )
	{
		error = "blocks are nested too deeply";
		return false;
	}

	while( true )
	{
		Token token = lexer.Next();
		if( token.type == TOKEN_END )
		{
			error = "file ends inside a block";
			return false;
		}
		if( token.type == TOKEN_PUNCT && token.text == "}" )
		{
			return true;
		}
		if( token.type == TOKEN_PUNCT && token.text == ";" )
		{
			continue;
		}

		std::unique_ptr<Node> node( new Node );

		// A quoted type before a key: "float" gravity = 1066.0;
		if( token.type == TOKEN_STRING && lexer.Peek().type == TOKEN_WORD && lexer.PeekSecond().text == "=" )
		{
			token = lexer.Next();
		}

		if( token.type == TOKEN_STRING )
		{
			// A bare string, such as a layer name
			node->value = token.text;
			parent.children.push_back( std::move( node ) );
			continue;
		}

		if( token.type != TOKEN_WORD )
		{
			error = "unexpected '" + token.text + "'";
			return false;
		}
		node->key = token.text;

		Token next = lexer.Next();
		if( next.type == TOKEN_WORD || next.type == TOKEN_STRING )
		{
			// key name { ... }, such as entityDef player_start_2 { ... }
			node->value = next.text;
			next = lexer.Next();
		}

		if( next.type == TOKEN_PUNCT && next.text == "=" )
		{
			next = lexer.Next();
			if( next.type == TOKEN_PUNCT && next.text == "!" )
			{
				// key = ! { ... } probably replaces an inherited block; read it the same way
				next = lexer.Next();
			}
			if( next.type == TOKEN_PUNCT && next.text == "{" )
			{
				if( !ParseBlock( lexer, *node, depth + 1, error ) )
				{
					return false;
				}
			}
			else if( next.type == TOKEN_WORD || next.type == TOKEN_STRING )
			{
				node->value = next.text;
			}
			else
			{
				error = "missing value for '" + node->key + "'";
				return false;
			}
		}
		else if( next.type == TOKEN_PUNCT && next.text == "{" )
		{
			if( !ParseBlock( lexer, *node, depth + 1, error ) )
			{
				return false;
			}
		}
		else
		{
			error = "unexpected '" + next.text + "' after '" + node->key + "'";
			return false;
		}

		parent.children.push_back( std::move( node ) );
	}
}

// x, y and z of a block; zero components are left out of the file
bool ReadVector( const Node* node, float out[3] )
{
	if( node == nullptr )
	{
		return false;
	}
	const char* axes[3] = { "x", "y", "z" };
	for( int i = 0; i < 3; i++ )
	{
		const Node* axis = node->Child( axes[i] );
		out[i] = ( axis != nullptr ) ? float( strtod( axis->value.c_str(), nullptr ) ) : 0.0f;
	}
	return true;
}

} // namespace

bool ParseEntities( const char* text, size_t size, std::vector<MapEntity>& out, std::string* error )
{
	out.clear();
	Lexer lexer( text, size );
	std::string message;

	while( true )
	{
		Token token = lexer.Next();
		if( token.type == TOKEN_END )
		{
			return true;
		}
		if( token.type == TOKEN_WORD && token.text == "Version" )
		{
			lexer.Next();
			continue;
		}
		if( token.type != TOKEN_WORD || token.text != "entity" )
		{
			message = "expected 'entity', found '" + token.text + "'";
			break;
		}
		Token brace = lexer.Next();
		if( brace.text != "{" )
		{
			message = "expected '{' after 'entity'";
			break;
		}

		Node entityNode;
		if( !ParseBlock( lexer, entityNode, 0, message ) )
		{
			break;
		}

		const Node* def = entityNode.Child( "entityDef" );
		if( def == nullptr )
		{
			continue;
		}

		MapEntity entity;
		entity.name = def->value;
		if( const Node* node = def->Child( "class" ) )
		{
			entity.className = node->value;
		}
		if( const Node* node = def->Child( "inherit" ) )
		{
			entity.inherit = node->value;
		}
		if( const Node* edit = def->Child( "edit" ) )
		{
			entity.hasPosition = ReadVector( edit->Child( "spawnPosition" ), entity.position );
			if( const Node* orientation = edit->Child( "spawnOrientation" ) )
			{
				if( const Node* mat = orientation->Child( "mat" ) )
				{
					entity.hasForward = ReadVector( mat->Child( "mat[0]" ), entity.forward );
				}
			}
		}
		out.push_back( std::move( entity ) );
	}

	if( error != nullptr )
	{
		*error = message;
	}
	return false;
}

} // namespace rage
