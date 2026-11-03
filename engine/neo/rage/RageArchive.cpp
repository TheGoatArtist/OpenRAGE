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

// .resources / .patch archive reader. See docs/research/formats/resources-archive.md.

#include "RageFormats.h"

#include <cstring>
#include <zlib.h>

namespace rage
{

namespace
{

const uint8_t	ARCHIVE_MAGIC[4] = { 0x22, 0x94, 0xAB, 0xCD };
const size_t	ARCHIVE_HEADER_SIZE = 12;
const size_t	LANGUAGE_RECORD_SIZE = 24;	// 20-byte name, 4-byte size
const size_t	ENTRY_TAIL_SIZE = 20;		// not understood yet

void SetError( std::string* error, const std::string& message )
{
	if( error != nullptr )
	{
		*error = message;
	}
}

// Walks the table. Numbers are big-endian; string lengths are little-endian.
class TableReader
{
public:
	TableReader( const uint8_t* data_, size_t size_ ) : data( data_ ), size( size_ ) {}

	bool U32( uint32_t& value )
	{
		if( size - pos < 4 )
		{
			return false;
		}
		value = ( uint32_t( data[pos] ) << 24 ) | ( uint32_t( data[pos + 1] ) << 16 ) | ( uint32_t( data[pos + 2] ) << 8 ) | data[pos + 3];
		pos += 4;
		return true;
	}

	bool String( std::string& value )
	{
		if( size - pos < 4 )
		{
			return false;
		}
		uint32_t length = data[pos] | ( uint32_t( data[pos + 1] ) << 8 ) | ( uint32_t( data[pos + 2] ) << 16 ) | ( uint32_t( data[pos + 3] ) << 24 );
		pos += 4;
		if( size - pos < length )
		{
			return false;
		}
		value.assign( reinterpret_cast<const char*>( data + pos ), length );
		pos += length;
		return true;
	}

	bool Skip( size_t count )
	{
		if( size - pos < count )
		{
			return false;
		}
		pos += count;
		return true;
	}

	size_t Position() const
	{
		return pos;
	}

private:
	const uint8_t*	data;
	size_t			size;
	size_t			pos = 0;
};

} // namespace

std::string NormalizeName( const std::string& name )
{
	std::string out = name;
	for( char& c : out )
	{
		if( c == '\\' )
		{
			c = '/';
		}
		else if( c >= 'A' && c <= 'Z' )
		{
			c = char( c - 'A' + 'a' );
		}
	}
	return out;
}

bool InflateRaw( const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize )
{
	if( outSize == 0 )
	{
		return true;
	}

	z_stream stream;
	memset( &stream, 0, sizeof( stream ) );
	if( inflateInit2( &stream, -MAX_WBITS ) != Z_OK )
	{
		return false;
	}

	stream.next_in = const_cast<Bytef*>( in );
	stream.avail_in = uInt( inSize );
	stream.next_out = out;
	stream.avail_out = uInt( outSize );

	// No final block, so inflate never reports Z_STREAM_END. Stop once the output is full or
	// inflate can't make progress.
	while( stream.avail_out > 0 )
	{
		const uInt before = stream.avail_out;
		const int result = inflate( &stream, Z_SYNC_FLUSH );
		if( result == Z_STREAM_END )
		{
			break;
		}
		if( result != Z_OK && result != Z_BUF_ERROR )
		{
			break;
		}
		if( stream.avail_out == before )
		{
			break;
		}
	}

	const bool complete = ( stream.total_out == outSize );
	inflateEnd( &stream );
	return complete;
}

/*
===============================================================================

	Archive

===============================================================================
*/

Archive::~Archive()
{
	Close();
}

void Archive::Close()
{
	if( file != nullptr )
	{
		fclose( file );
		file = nullptr;
	}
	entries.clear();
	byName.clear();
	bySource.clear();
	path.clear();
}

bool Archive::Open( const std::string& path_, std::string* error )
{
	Close();

	file = fopen( path_.c_str(), "rb" );
	if( file == nullptr )
	{
		SetError( error, "can't open " + path_ );
		return false;
	}

	uint8_t header[ARCHIVE_HEADER_SIZE];
	if( fread( header, 1, sizeof( header ), file ) != sizeof( header ) || memcmp( header, ARCHIVE_MAGIC, 4 ) != 0 )
	{
		SetError( error, path_ + " isn't a RAGE .resources archive" );
		Close();
		return false;
	}

	const uint32_t tableOffset = ( uint32_t( header[4] ) << 24 ) | ( uint32_t( header[5] ) << 16 ) | ( uint32_t( header[6] ) << 8 ) | header[7];
	const uint32_t tableSize = ( uint32_t( header[8] ) << 24 ) | ( uint32_t( header[9] ) << 16 ) | ( uint32_t( header[10] ) << 8 ) | header[11];

	std::vector<uint8_t> table( tableSize );
	if( fseek( file, long( tableOffset ), SEEK_SET ) != 0 || fread( table.data(), 1, tableSize, file ) != tableSize )
	{
		SetError( error, path_ + ": the table is cut short" );
		Close();
		return false;
	}

	TableReader reader( table.data(), table.size() );
	uint32_t count = 0;
	if( !reader.U32( count ) )
	{
		SetError( error, path_ + ": bad table" );
		Close();
		return false;
	}

	entries.reserve( count );
	for( uint32_t i = 0; i < count; i++ )
	{
		ArchiveEntry entry;
		uint32_t languages = 0;
		if( !reader.U32( entry.id ) || !reader.String( entry.type ) || !reader.String( entry.source ) || !reader.String( entry.name )
				|| !reader.U32( entry.offset ) || !reader.U32( entry.size ) || !reader.U32( entry.storedSize )
				|| !reader.U32( languages ) || !reader.Skip( size_t( languages ) * LANGUAGE_RECORD_SIZE ) || !reader.Skip( ENTRY_TAIL_SIZE ) )
		{
			SetError( error, path_ + ": the table ends inside entry " + std::to_string( i ) );
			Close();
			return false;
		}
		entries.push_back( std::move( entry ) );
	}

	for( size_t i = 0; i < entries.size(); i++ )
	{
		// The first entry with a name wins inside one archive
		byName.emplace( NormalizeName( entries[i].name ), i );
		if( !entries[i].source.empty() )
		{
			bySource.emplace( NormalizeName( entries[i].source ), i );
		}
	}

	path = path_;
	return true;
}

const ArchiveEntry* Archive::Find( const std::string& name ) const
{
	const std::string key = NormalizeName( name );
	auto it = byName.find( key );
	if( it != byName.end() )
	{
		return &entries[it->second];
	}
	it = bySource.find( key );
	if( it != bySource.end() )
	{
		return &entries[it->second];
	}
	return nullptr;
}

bool Archive::Read( const ArchiveEntry& entry, std::vector<uint8_t>& out, std::string* error ) const
{
	out.clear();
	if( entry.storedSize == 0 || entry.size == 0 )
	{
		return true;
	}
	if( file == nullptr )
	{
		SetError( error, "archive isn't open" );
		return false;
	}

	std::vector<uint8_t> stored( entry.storedSize );
	if( fseek( file, long( entry.offset ), SEEK_SET ) != 0 || fread( stored.data(), 1, stored.size(), file ) != stored.size() )
	{
		SetError( error, path + ": can't read " + entry.name );
		return false;
	}

	if( entry.storedSize == entry.size )
	{
		out.swap( stored );
		return true;
	}

	out.resize( entry.size );
	if( !InflateRaw( stored.data(), stored.size(), out.data(), out.size() ) )
	{
		out.clear();
		SetError( error, path + ": " + entry.name + " doesn't unpack to its recorded size" );
		return false;
	}
	return true;
}

/*
===============================================================================

	Library

===============================================================================
*/

Library::~Library()
{
	Clear();
}

void Library::Clear()
{
	for( Archive* archive : archives )
	{
		delete archive;
	}
	archives.clear();
}

bool Library::Mount( const std::string& path, std::string* error )
{
	Archive* archive = new Archive;
	if( !archive->Open( path, error ) )
	{
		delete archive;
		return false;
	}
	archives.push_back( archive );
	return true;
}

const ArchiveEntry* Library::Find( const std::string& name, const Archive** archiveOut ) const
{
	for( const Archive* archive : archives )
	{
		const ArchiveEntry* entry = archive->Find( name );
		if( entry != nullptr )
		{
			if( archiveOut != nullptr )
			{
				*archiveOut = archive;
			}
			return entry;
		}
	}
	return nullptr;
}

bool Library::Read( const std::string& name, std::vector<uint8_t>& out, std::string* error ) const
{
	const Archive* archive = nullptr;
	const ArchiveEntry* entry = Find( name, &archive );
	if( entry == nullptr )
	{
		SetError( error, name + " isn't in any mounted archive" );
		return false;
	}
	return archive->Read( *entry, out, error );
}

} // namespace rage
