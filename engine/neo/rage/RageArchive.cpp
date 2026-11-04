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

// The smallest table entry: an id, three name lengths, four numbers and the tail
const size_t	MIN_ENTRY_SIZE = 4 + 3 * 4 + 4 * 4 + ENTRY_TAIL_SIZE;

// Deflate can't expand data more than about 1032 times
const uint64_t	MAX_INFLATE_RATIO = 1032;

void SetError( std::string* error, const std::string& message )
{
	if( error != nullptr )
	{
		*error = message;
	}
}

// 64-bit seeks: archive offsets go up to 4 GB, and a long stops at 2 GB on Windows
bool SeekTo( FILE* f, uint64_t offset )
{
#if defined( _WIN32 )
	return _fseeki64( f, int64_t( offset ), SEEK_SET ) == 0;
#else
	return fseeko( f, off_t( offset ), SEEK_SET ) == 0;
#endif
}

uint64_t SizeOf( FILE* f )
{
#if defined( _WIN32 )
	const int64_t size = ( _fseeki64( f, 0, SEEK_END ) == 0 ) ? _ftelli64( f ) : -1;
#else
	const off_t size = ( fseeko( f, 0, SEEK_END ) == 0 ) ? ftello( f ) : -1;
#endif
	return size < 0 ? 0 : uint64_t( size );
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
	fileSize = 0;
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

	fileSize = SizeOf( file );
	uint8_t header[ARCHIVE_HEADER_SIZE];
	if( !SeekTo( file, 0 ) || fread( header, 1, sizeof( header ), file ) != sizeof( header ) || memcmp( header, ARCHIVE_MAGIC, 4 ) != 0 )
	{
		SetError( error, path_ + " isn't a RAGE .resources archive" );
		Close();
		return false;
	}

	const uint32_t tableOffset = ( uint32_t( header[4] ) << 24 ) | ( uint32_t( header[5] ) << 16 ) | ( uint32_t( header[6] ) << 8 ) | header[7];
	const uint32_t tableSize = ( uint32_t( header[8] ) << 24 ) | ( uint32_t( header[9] ) << 16 ) | ( uint32_t( header[10] ) << 8 ) | header[11];

	// Check the table fits in the file before allocating it, so a damaged header fails here
	if( uint64_t( tableOffset ) + tableSize > fileSize )
	{
		SetError( error, path_ + ": the table is cut short" );
		Close();
		return false;
	}

	std::vector<uint8_t> table( tableSize );
	if( !SeekTo( file, tableOffset ) || fread( table.data(), 1, tableSize, file ) != tableSize )
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

	if( count > ( table.size() - 4 ) / MIN_ENTRY_SIZE )
	{
		SetError( error, path_ + ": the table can't hold " + std::to_string( count ) + " entries" );
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

const ArchiveEntry* Archive::Find( const std::string& name, bool sourceNames ) const
{
	const std::string key = NormalizeName( name );
	auto it = byName.find( key );
	if( it != byName.end() )
	{
		return &entries[it->second];
	}
	if( !sourceNames )
	{
		return nullptr;
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

	// Check the sizes against the file and against what deflate can do before allocating, so a
	// damaged table entry fails here
	if( uint64_t( entry.offset ) + entry.storedSize > fileSize )
	{
		SetError( error, path + ": " + entry.name + " runs past the end of the archive" );
		return false;
	}
	if( entry.storedSize != entry.size && uint64_t( entry.size ) > uint64_t( entry.storedSize ) * MAX_INFLATE_RATIO + 1024 )
	{
		SetError( error, path + ": " + entry.name + " can't unpack to its recorded size of " + std::to_string( entry.size ) );
		return false;
	}

	std::vector<uint8_t> stored( entry.storedSize );
	if( !SeekTo( file, entry.offset ) || fread( stored.data(), 1, stored.size(), file ) != stored.size() )
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

const ArchiveEntry* Library::Find( const std::string& name, const Archive** archiveOut, bool sourceNames ) const
{
	for( const Archive* archive : archives )
	{
		const ArchiveEntry* entry = archive->Find( name, sourceNames );
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
