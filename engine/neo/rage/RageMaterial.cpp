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

// Material declarations: only the keys that place a material in a megatexture.
// See docs/research/formats/materials.md.

#include "RageFormats.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace rage
{

namespace
{

// Words, quoted strings (without their quotes) and single punctuation characters; comments
// are left out
class Tokens
{
public:
	Tokens( const char* text_, size_t size_ ) : text( text_ ), size( size_ ) {}

	bool Next( std::string& token, bool& quoted )
	{
		quoted = false;
		for( ;; )
		{
			while( pos < size && std::isspace( static_cast<unsigned char>( text[pos] ) ) )
			{
				pos++;
			}
			if( pos + 1 < size && text[pos] == '/' && text[pos + 1] == '/' )
			{
				while( pos < size && text[pos] != '\n' )
				{
					pos++;
				}
				continue;
			}
			if( pos + 1 < size && text[pos] == '/' && text[pos + 1] == '*' )
			{
				pos += 2;
				while( pos + 1 < size && !( text[pos] == '*' && text[pos + 1] == '/' ) )
				{
					pos++;
				}
				pos = ( pos + 2 <= size ) ? pos + 2 : size;
				continue;
			}
			break;
		}
		if( pos >= size )
		{
			return false;
		}

		const char c = text[pos];
		if( c == '"' )
		{
			const size_t start = ++pos;
			while( pos < size && text[pos] != '"' )
			{
				pos++;
			}
			token.assign( text + start, pos - start );
			if( pos < size )
			{
				pos++;
			}
			quoted = true;
			return true;
		}
		if( std::strchr( "{}(),;=", c ) != nullptr )
		{
			token.assign( 1, c );
			pos++;
			return true;
		}
		const size_t start = pos;
		while( pos < size && !std::isspace( static_cast<unsigned char>( text[pos] ) ) && std::strchr( "{}(),;=\"", text[pos] ) == nullptr )
		{
			pos++;
		}
		token.assign( text + start, pos - start );
		return true;
	}

private:
	const char*	text;
	size_t		size;
	size_t		pos = 0;
};

bool SameKey( const std::string& a, const char* b )
{
	const size_t n = std::strlen( b );
	if( a.size() != n )
	{
		return false;
	}
	for( size_t i = 0; i < n; i++ )
	{
		if( std::tolower( static_cast<unsigned char>( a[i] ) ) != std::tolower( static_cast<unsigned char>( b[i] ) ) )
		{
			return false;
		}
	}
	return true;
}

} // namespace

bool ParseMaterialDecl( const char* text, size_t size, MaterialDecl& out, std::string* error )
{
	out = MaterialDecl();
	Tokens tokens( text, size );
	std::string token;
	bool quoted = false;
	int depth = 0;
	bool sawBlock = false;

	while( tokens.Next( token, quoted ) )
	{
		if( !quoted && token == "{" )
		{
			depth++;
			sawBlock = true;
			continue;
		}
		if( !quoted && token == "}" )
		{
			depth--;
			continue;
		}
		if( quoted || depth != 1 )
		{
			continue;
		}

		if( SameKey( token, "landPageFile" ) )
		{
			if( !tokens.Next( out.landPageFile, quoted ) )
			{
				break;
			}
		}
		else if( SameKey( token, "pagetablemap" ) )
		{
			if( !tokens.Next( out.pageTableMap, quoted ) )
			{
				break;
			}
		}
		else if( SameKey( token, "transmap" ) )
		{
			out.hasTransMap = true;
		}
		else if( SameKey( token, "virtualmapping" ) )
		{
			// { w, h, x, y }
			std::string value;
			if( !tokens.Next( value, quoted ) || value != "{" )
			{
				if( error != nullptr )
				{
					*error = "virtualmapping isn't followed by a { w, h, x, y } tuple";
				}
				return false;
			}
			int count = 0;
			while( tokens.Next( value, quoted ) && value != "}" )
			{
				if( value == "," )
				{
					continue;
				}
				if( count < 4 )
				{
					out.virtualMapping[count] = std::strtof( value.c_str(), nullptr );
				}
				count++;
			}
			if( count != 4 )
			{
				if( error != nullptr )
				{
					*error = "virtualmapping has " + std::to_string( count ) + " numbers, not 4";
				}
				return false;
			}
			out.hasVirtualMapping = true;
		}
	}

	if( !sawBlock )
	{
		if( error != nullptr )
		{
			*error = "no { } block";
		}
		return false;
	}
	return true;
}

} // namespace rage
