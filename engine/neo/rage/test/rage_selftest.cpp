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

// Self-test for the RAGE format readers in RageFormats.h.
//
// No game data is used: every file here is built from scratch, following the layouts in
// docs/research/formats/. Values are made up. Run it with a folder it may write two
// temporary archives into:
//
//     rage_selftest <temporary folder>

#include "../RageFormats.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{

int failures = 0;
int checks = 0;

#define CHECK( condition ) \
	do { \
		checks++; \
		if( !( condition ) ) { \
			failures++; \
			printf( "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition ); \
		} \
	} while( 0 )

bool Near( float a, float b )
{
	return std::fabs( a - b ) < 1e-4f;
}

// ---- byte writers: big-endian numbers, little-endian name lengths

struct Bytes
{
	std::vector<uint8_t> data;

	void U8( uint8_t v )
	{
		data.push_back( v );
	}
	void U16( uint16_t v )
	{
		U8( uint8_t( v >> 8 ) );
		U8( uint8_t( v ) );
	}
	void S16( int16_t v )
	{
		U16( uint16_t( v ) );
	}
	void U32( uint32_t v )
	{
		U8( uint8_t( v >> 24 ) );
		U8( uint8_t( v >> 16 ) );
		U8( uint8_t( v >> 8 ) );
		U8( uint8_t( v ) );
	}
	void F32( float f )
	{
		uint32_t bits;
		memcpy( &bits, &f, 4 );
		U32( bits );
	}
	void Name( const std::string& s )
	{
		const uint32_t n = uint32_t( s.size() );
		U8( uint8_t( n ) );
		U8( uint8_t( n >> 8 ) );
		U8( uint8_t( n >> 16 ) );
		U8( uint8_t( n >> 24 ) );
		data.insert( data.end(), s.begin(), s.end() );
	}
	void Raw( const void* p, size_t n )
	{
		data.insert( data.end(), ( const uint8_t* )p, ( const uint8_t* )p + n );
	}
	void Magic()
	{
		const uint8_t m[4] = { 0x18, 0x4C, 0x4D, 0x42 };
		Raw( m, 4 );
	}
};

// Raw deflate that is flushed but never finished, like RAGE's archives
std::vector<uint8_t> DeflateWithoutFinalBlock( const std::string& text )
{
	z_stream s;
	memset( &s, 0, sizeof( s ) );
	deflateInit2( &s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY );
	std::vector<uint8_t> out( text.size() + 1024 );
	s.next_in = ( Bytef* )text.data();
	s.avail_in = uInt( text.size() );
	s.next_out = out.data();
	s.avail_out = uInt( out.size() );
	deflate( &s, Z_SYNC_FLUSH );
	out.resize( s.total_out );
	deflateEnd( &s );
	return out;
}

/*
===============================================================================
	InflateRaw
===============================================================================
*/
void TestInflate()
{
	std::string text;
	for( int i = 0; i < 200; i++ )
	{
		text += "the wasteland stretches out in every direction. ";
	}
	std::vector<uint8_t> packed = DeflateWithoutFinalBlock( text );
	std::vector<uint8_t> out( text.size() );
	CHECK( rage::InflateRaw( packed.data(), packed.size(), out.data(), out.size() ) );
	CHECK( memcmp( out.data(), text.data(), text.size() ) == 0 );

	// Asking for more than the stream holds must fail
	std::vector<uint8_t> tooBig( text.size() + 10 );
	CHECK( !rage::InflateRaw( packed.data(), packed.size(), tooBig.data(), tooBig.size() ) );
}

/*
===============================================================================
	Archives
===============================================================================
*/
struct FakeEntry
{
	uint32_t	id;
	std::string	type, source, name;
	std::string	content;
	bool		compress;
	int			languages;
};

bool WriteArchive( const std::string& path, const std::vector<FakeEntry>& fakes )
{
	Bytes data;
	std::vector<uint32_t> offsets, stored;
	const uint32_t dataStart = 12;
	for( const FakeEntry& f : fakes )
	{
		offsets.push_back( dataStart + uint32_t( data.data.size() ) );
		if( f.content.empty() )
		{
			stored.push_back( 0 );
		}
		else if( f.compress )
		{
			std::vector<uint8_t> packed = DeflateWithoutFinalBlock( f.content );
			stored.push_back( uint32_t( packed.size() ) );
			data.Raw( packed.data(), packed.size() );
		}
		else
		{
			stored.push_back( uint32_t( f.content.size() ) );
			data.Raw( f.content.data(), f.content.size() );
		}
	}

	Bytes table;
	table.U32( uint32_t( fakes.size() ) );
	for( size_t i = 0; i < fakes.size(); i++ )
	{
		const FakeEntry& f = fakes[i];
		table.U32( f.id );
		table.Name( f.type );
		table.Name( f.source );
		table.Name( f.name );
		table.U32( offsets[i] );
		table.U32( uint32_t( f.content.size() ) );
		table.U32( stored[i] );
		table.U32( uint32_t( f.languages ) );
		for( int l = 0; l < f.languages; l++ )
		{
			char record[20] = {};
			snprintf( record, sizeof( record ), "language%d", l );
			table.Raw( record, 20 );
			table.U32( 1000 + l );
		}
		const uint8_t tail[20] = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
		table.Raw( tail, 20 );
	}

	Bytes file;
	const uint8_t magic[4] = { 0x22, 0x94, 0xAB, 0xCD };
	file.Raw( magic, 4 );
	file.U32( dataStart + uint32_t( data.data.size() ) );
	file.U32( uint32_t( table.data.size() ) );
	file.Raw( data.data.data(), data.data.size() );
	file.Raw( table.data.data(), table.data.size() );

	FILE* f = fopen( path.c_str(), "wb" );
	if( f == nullptr )
	{
		return false;
	}
	fwrite( file.data.data(), 1, file.data.size(), f );
	fclose( f );
	return true;
}

void TestArchives( const std::string& folder )
{
	std::string bigText;
	for( int i = 0; i < 300; i++ )
	{
		bigText += "area geometry line " + std::to_string( i ) + "\n";
	}

	const std::string basePath = folder + "/selftest_base.resources";
	const std::string patchPath = folder + "/selftest_patch.patch";

	std::vector<FakeEntry> base =
	{
		{ 0, "decl", "", "generated/decls/test/plain.decl", "plain text entry", false, 0 },
		{ 1, "model", "maps/test/_combo/_area0.lwo", "maps/test/_combo/_area0.bmodel", bigText, true, 0 },
		{ 2, "renderParm", "", "generated/empty.parm", "", false, 0 },
		{ 3, "sample", "sound/vo/test.wav", "generated/sound/vo/test.msadpcm", "voice", false, 8 },
		{ 0xFFFFFFFFu, "image", "", "_builtin", "x", false, 0 },
		{ 5, "decl", "", "shared/override.decl", "from base", false, 0 },
	};
	std::vector<FakeEntry> patch =
	{
		{ 7, "decl", "", "shared/override.decl", "from patch", true, 0 },
	};
	CHECK( WriteArchive( basePath, base ) );
	CHECK( WriteArchive( patchPath, patch ) );

	rage::Archive archive;
	std::string error;
	CHECK( archive.Open( basePath, &error ) );
	CHECK( archive.Entries().size() == base.size() );
	CHECK( archive.Entries()[3].type == "sample" );
	CHECK( archive.Entries()[4].id == 0xFFFFFFFFu );

	std::vector<uint8_t> out;
	const rage::ArchiveEntry* plain = archive.Find( "GENERATED\\decls\\Test\\PLAIN.decl" );
	CHECK( plain != nullptr );
	CHECK( plain && archive.Read( *plain, out, &error ) && std::string( out.begin(), out.end() ) == "plain text entry" );

	const rage::ArchiveEntry* area = archive.Find( "maps/test/_combo/_area0.bmodel" );
	CHECK( area != nullptr && area->storedSize < area->size );
	CHECK( area && archive.Read( *area, out, &error ) && std::string( out.begin(), out.end() ) == bigText );

	// Found by its source name too
	CHECK( archive.Find( "maps/test/_combo/_area0.lwo" ) == area );
	CHECK( archive.Find( "maps/test/_combo/_area0.lwo", false ) == nullptr );	// built names only

	const rage::ArchiveEntry* empty = archive.Find( "generated/empty.parm" );
	CHECK( empty != nullptr && archive.Read( *empty, out, &error ) && out.empty() );

	CHECK( archive.Find( "not/there" ) == nullptr );

	// A patch mounted first replaces the base entry with the same name
	rage::Library library;
	CHECK( library.Mount( patchPath, &error ) );
	CHECK( library.Mount( basePath, &error ) );
	CHECK( library.Read( "shared/override.decl", out, &error ) && std::string( out.begin(), out.end() ) == "from patch" );
	CHECK( library.Read( "generated/decls/test/plain.decl", out, &error ) );
	CHECK( !library.Read( "missing.decl", out, &error ) );

	// Something that isn't an archive
	rage::Archive bad;
	CHECK( !bad.Open( folder + "/does_not_exist.resources", &error ) );

	// A damaged table size or entry count fails at once instead of allocating for it
	{
		std::vector<uint8_t> bytes;
		FILE* f = fopen( basePath.c_str(), "rb" );
		for( int c; f != nullptr && ( c = fgetc( f ) ) != EOF; )
		{
			bytes.push_back( uint8_t( c ) );
		}
		if( f != nullptr )
		{
			fclose( f );
		}
		CHECK( bytes.size() > 12 );

		const std::string damagedPath = folder + "/selftest_damaged.resources";
		auto openDamaged = [&]( const std::vector<uint8_t>& damaged )
		{
			FILE* out = fopen( damagedPath.c_str(), "wb" );
			if( out == nullptr )
			{
				return true;
			}
			fwrite( damaged.data(), 1, damaged.size(), out );
			fclose( out );
			rage::Archive archive;
			return archive.Open( damagedPath, &error );
		};

		// The table size (big-endian, header bytes 8 to 11) runs past the end of the file
		std::vector<uint8_t> damaged = bytes;
		damaged[8] = 0x7F;
		CHECK( !openDamaged( damaged ) );

		// The entry count (the table's first number) is more than the table can hold
		damaged = bytes;
		const uint32_t tableOffset = ( uint32_t( bytes[4] ) << 24 ) | ( uint32_t( bytes[5] ) << 16 ) | ( uint32_t( bytes[6] ) << 8 ) | bytes[7];
		CHECK( tableOffset < bytes.size() );
		if( tableOffset < bytes.size() )
		{
			damaged[tableOffset] = 0x7F;
			CHECK( !openDamaged( damaged ) );
		}
		remove( damagedPath.c_str() );
	}

	remove( basePath.c_str() );
	remove( patchPath.c_str() );
}

/*
===============================================================================
	.bmodel
===============================================================================
*/
void WriteSurfaceHeader( Bytes& b, const std::string& material, uint32_t vertexCount, uint32_t indexCount, uint32_t format,
						 const float scales[10] )
{
	b.Name( material );
	b.U32( 0 );	// unknown
	b.U32( vertexCount );
	b.U32( indexCount );
	b.U32( format );
	for( int i = 0; i < 10; i++ )
	{
		b.F32( scales[i] );
	}
}

void WriteSurfaceEnd( Bytes& b )
{
	for( int i = 0; i < 6; i++ )
	{
		b.F32( float( i ) );
	}
	b.U32( 0 );
	b.Magic();
}

std::vector<uint8_t> BuildBModel( bool eightByteTime, bool breakMarker )
{
	Bytes b;
	b.Magic();
	if( eightByteTime )
	{
		b.U32( 0x00000001 );
		b.U32( 0x23456789 );
	}
	else
	{
		b.U32( 0x23456789 );
	}
	b.U32( 3 );

	const float noScale[10] = { 1, 1, 1, 0, 0, 0, 1, 1, 0, 0 };

	// Surface 0: float vertexes, strips. Strip 1 is 0 1 2 2 3 4 (two degenerates), then 5 0 1.
	{
		const uint16_t indexes[] = { 0, 1, 2, 2, 3, 4, 0xFFFF, 5, 0, 1 };
		WriteSurfaceHeader( b, "textures/test/float_strip", 6, 10, 0x21F, noScale );
		for( int v = 0; v < 6; v++ )
		{
			b.F32( float( v ) * 10.0f );
			b.F32( float( v ) * -5.0f );
			b.F32( 1.5f );
			b.F32( 0.25f * v );
			b.F32( 0.75f );
			const uint8_t normal[4] = { 128, 128, 255, 0 };		// +Z
			const uint8_t tangent[4] = { 255, 128, 128, 255 };	// +X, sign byte 255
			const uint8_t color[4] = { 200, 200, 200, 255 };
			b.Raw( normal, 4 );
			b.Raw( tangent, 4 );
			b.Raw( color, 4 );
		}
		for( uint16_t i : indexes )
		{
			b.U16( i );
		}
		WriteSurfaceEnd( b );
	}

	// Surface 1: packed vertexes with normals, a triangle list with one degenerate triangle
	{
		const float scales[10] = { 0.25f, 0.25f, 0.25f, 100, 200, 300, 1.0f / 1024, 1.0f / 1024, 0.5f, 0 };
		const uint16_t indexes[] = { 0, 1, 2, 0, 0, 1 };
		WriteSurfaceHeader( b, "textures/test/packed_list", 3, 6, 0x77, scales );
		const int16_t positions[3][3] = { { 4, 8, -12 }, { -400, 0, 32767 }, { 1, 2, 3 } };
		for( int v = 0; v < 3; v++ )
		{
			b.S16( positions[v][0] );
			b.S16( positions[v][1] );
			b.S16( positions[v][2] );
			b.U8( 1 );
			b.U8( 0 );
			b.S16( int16_t( 512 * v ) );
			b.S16( -1024 );
			const uint8_t normal[4] = { 1, 128, 128, 0 };		// -X
			const uint8_t tangent[4] = { 128, 255, 128, 0 };	// +Y, sign byte 0
			b.Raw( normal, 4 );
			b.Raw( tangent, 4 );
		}
		for( uint16_t i : indexes )
		{
			b.U16( i );
		}
		WriteSurfaceEnd( b );
	}

	// Surface 2: packed vertexes without normals, one strip of two triangles
	{
		const float scales[10] = { 2, 2, 2, 0, 0, 0, 1, 1, 0, 0 };
		const uint16_t indexes[] = { 0, 1, 2, 3 };
		WriteSurfaceHeader( b, "textures/test/packed_strip", 4, 4, 0x263, scales );
		for( int v = 0; v < 4; v++ )
		{
			b.S16( int16_t( v ) );
			b.S16( 0 );
			b.S16( 0 );
			b.U8( 1 );
			b.U8( 0 );
			b.S16( 0 );
			b.S16( 0 );
		}
		for( uint16_t i : indexes )
		{
			b.U16( i );
		}
		if( breakMarker )
		{
			for( int i = 0; i < 6; i++ )
			{
				b.F32( 0 );
			}
			b.U32( 0 );
			b.U32( 0xDEADBEEF );
		}
		else
		{
			WriteSurfaceEnd( b );
		}
	}

	// Trailer: two material ranges that split surface 0
	b.U32( 2 );
	b.Name( "textures/test/first_half" );
	b.U32( 0 );
	b.U32( 0 );
	b.U32( 2 );
	b.Name( "textures/test/second_half" );
	b.U32( 0 );
	b.U32( 3 );
	b.U32( 5 );
	return b.data;
}

void CheckModel( const rage::BModel& m, uint64_t expectedTime )
{
	CHECK( m.sourceTime == expectedTime );
	CHECK( m.surfaces.size() == 3 );
	if( m.surfaces.size() != 3 )
	{
		return;
	}

	const rage::BModelSurface& s0 = m.surfaces[0];
	CHECK( s0.material == "textures/test/float_strip" );
	CHECK( s0.format == 0x21F && s0.hasNormals && s0.hasColors );
	CHECK( s0.vertexes.size() == 6 );
	CHECK( Near( s0.vertexes[3].xyz[0], 30.0f ) && Near( s0.vertexes[3].xyz[1], -15.0f ) && Near( s0.vertexes[3].xyz[2], 1.5f ) );
	CHECK( Near( s0.vertexes[2].st[0], 0.5f ) && Near( s0.vertexes[2].st[1], 0.75f ) );
	CHECK( Near( s0.vertexes[0].normal[2], 1.0f ) && Near( s0.vertexes[0].normal[0], 0.0f ) );
	CHECK( Near( s0.vertexes[0].tangent[0], 1.0f ) && s0.vertexes[0].bitangentSign == -1.0f );
	CHECK( s0.vertexes[5].color[0] == 200 && s0.vertexes[5].color[3] == 255 );
	const std::vector<uint32_t> expected0 = { 0, 1, 2, 3, 2, 4, 5, 0, 1 };
	CHECK( s0.triangles == expected0 );
	CHECK( Near( s0.boundsMax[2], 5.0f ) );

	const rage::BModelSurface& s1 = m.surfaces[1];
	CHECK( s1.format == 0x77 && s1.hasNormals && !s1.hasColors );
	CHECK( Near( s1.vertexes[0].xyz[0], 101.0f ) && Near( s1.vertexes[0].xyz[1], 202.0f ) && Near( s1.vertexes[0].xyz[2], 297.0f ) );
	CHECK( Near( s1.vertexes[1].xyz[0], 0.0f ) && Near( s1.vertexes[1].xyz[2], 32767 * 0.25f + 300.0f ) );
	CHECK( Near( s1.vertexes[2].st[0], 1024.0f / 1024 + 0.5f ) && Near( s1.vertexes[2].st[1], -1.0f ) );
	CHECK( Near( s1.vertexes[0].normal[0], -1.0f ) && Near( s1.vertexes[0].tangent[1], 1.0f ) && s1.vertexes[0].bitangentSign == 1.0f );
	const std::vector<uint32_t> expected1 = { 0, 1, 2 };
	CHECK( s1.triangles == expected1 );

	const rage::BModelSurface& s2 = m.surfaces[2];
	CHECK( s2.format == 0x263 && !s2.hasNormals );
	CHECK( Near( s2.vertexes[3].xyz[0], 6.0f ) );
	const std::vector<uint32_t> expected2 = { 0, 1, 2, 2, 1, 3 };
	CHECK( s2.triangles == expected2 );

	CHECK( m.ranges.size() == 2 && m.ranges[1].material == "textures/test/second_half" && m.ranges[1].lastVertex == 5 );

	// Surface 0 splits into two parts by its ranges; the others keep their own material
	std::vector<rage::BModelSurfacePart> parts = rage::SplitSurfaceByMaterial( m, 0 );
	CHECK( parts.size() == 2 );
	if( parts.size() == 2 )
	{
		CHECK( parts[0].material == "textures/test/first_half" && parts[0].triangles == std::vector<uint32_t>( { 0, 1, 2 } ) );
		CHECK( parts[1].material == "textures/test/second_half" && parts[1].triangles == std::vector<uint32_t>( { 3, 2, 4, 5, 0, 1 } ) );
	}
	parts = rage::SplitSurfaceByMaterial( m, 1 );
	CHECK( parts.size() == 1 && parts[0].material == "textures/test/packed_list" );
}

void TestBModel()
{
	std::string error;

	rage::BModel model;
	std::vector<uint8_t> file = BuildBModel( false, false );
	CHECK( rage::ParseBModel( file.data(), file.size(), model, &error ) );
	if( !error.empty() )
	{
		printf( "  parse error: %s\n", error.c_str() );
	}
	CheckModel( model, 0x23456789 );

	// The same file with an 8-byte time field
	file = BuildBModel( true, false );
	error.clear();
	CHECK( rage::ParseBModel( file.data(), file.size(), model, &error ) );
	CheckModel( model, 0x0000000123456789ull );

	// A surface without its end marker is rejected
	file = BuildBModel( false, true );
	CHECK( !rage::ParseBModel( file.data(), file.size(), model, &error ) );
	CHECK( !error.empty() );

	// Extra bytes after the trailer are rejected
	file = BuildBModel( false, false );
	file.push_back( 0 );
	CHECK( !rage::ParseBModel( file.data(), file.size(), model, &error ) );

	// A file with no surfaces and no ranges
	Bytes emptyModel;
	emptyModel.Magic();
	emptyModel.U32( 42 );
	emptyModel.U32( 0 );
	emptyModel.U32( 0 );
	CHECK( rage::ParseBModel( emptyModel.data.data(), emptyModel.data.size(), model, &error ) );
	CHECK( model.surfaces.empty() && model.sourceTime == 42 );

	// A damaged index count fails at once instead of allocating gigabytes for it
	Bytes hugeIndexes;
	hugeIndexes.Magic();
	hugeIndexes.U32( 42 );
	hugeIndexes.U32( 1 );
	const float scales[10] = { 1, 1, 1, 0, 0, 0, 1, 1, 0, 0 };
	WriteSurfaceHeader( hugeIndexes, "textures/test/huge", 0, 0xC0000000u, 0x1F, scales );
	CHECK( !rage::ParseBModel( hugeIndexes.data.data(), hugeIndexes.data.size(), model, &error ) );

	// Wrong magic
	const uint8_t junk[16] = { 1, 2, 3, 4 };
	CHECK( !rage::ParseBModel( junk, sizeof( junk ), model, &error ) );

	// An older version, as some occlusion meshes are, is named as such
	const uint8_t older[16] = { 0x17, 0x4C, 0x4D, 0x42 };
	CHECK( !rage::ParseBModel( older, sizeof( older ), model, &error ) );
	CHECK( error.find( "unsupported .bmodel version 23" ) != std::string::npos );
}

/*
===============================================================================
	.entities
===============================================================================
*/
void TestEntities()
{
	const char* text =
		"Version 3\n"
		"// a comment\n"
		"entity {\n"
		"\tlayers {\n"
		"\t\t\"layer/test/start\"\n"
		"\t}\n"
		"\tentityDef test_start_1 {\n"
		"\t\tinherit = \"player/start\";\n"
		"\t\tclass = \"idPlayerStart\";\n"
		"\t\texpandInheritance = false;\n"
		"\t\tedit = {\n"
		"\t\t\t\"float\" gravity = 900.5;\n"
		"\t\t\tspawnPosition = {\n"
		"\t\t\t\tx = 12.5;\n"
		"\t\t\t\ty = -40;\n"
		"\t\t\t}\n"
		"\t\t\tspawnOrientation = {\n"
		"\t\t\t\tmat = {\n"
		"\t\t\t\t\tmat[0] = { y = 1; }\n"
		"\t\t\t\t\tmat[1] = { x = -1; }\n"
		"\t\t\t\t\tmat[2] = { z = 1; }\n"
		"\t\t\t\t}\n"
		"\t\t\t}\n"
		"\t\t\tsomething = ! {\n"
		"\t\t\t\tnum = 2;\n"
		"\t\t\t}\n"
		"\t\t}\n"
		"\t}\n"
		"}\n"
		"entity {\n"
		"\tentityDef test_crate {\n"
		"\t\tinherit = \"props/crate\";\n"
		"\t\tclass = \"idStaticEntity\";\n"
		"\t\tedit = { spawnPosition = { z = 64; } }\n"
		"\t}\n"
		"}\n";

	std::vector<rage::MapEntity> entities;
	std::string error;
	CHECK( rage::ParseEntities( text, strlen( text ), entities, &error ) );
	if( !error.empty() )
	{
		printf( "  parse error: %s\n", error.c_str() );
	}
	CHECK( entities.size() == 2 );
	if( entities.size() == 2 )
	{
		const rage::MapEntity& start = entities[0];
		CHECK( start.name == "test_start_1" && start.className == "idPlayerStart" && start.inherit == "player/start" );
		CHECK( start.hasPosition && Near( start.position[0], 12.5f ) && Near( start.position[1], -40.0f ) && Near( start.position[2], 0.0f ) );
		CHECK( start.hasForward && Near( start.forward[0], 0.0f ) && Near( start.forward[1], 1.0f ) );

		const rage::MapEntity& crate = entities[1];
		CHECK( crate.className == "idStaticEntity" && crate.hasPosition && Near( crate.position[2], 64.0f ) && !crate.hasForward );
	}

	// An unclosed block is an error
	const char* broken = "Version 3\nentity { entityDef x { class = \"y\";";
	CHECK( !rage::ParseEntities( broken, strlen( broken ), entities, &error ) );
}

} // namespace

/*
===============================================================================

	Material declarations

===============================================================================
*/

void TestMaterials()
{
	rage::MaterialDecl decl;
	std::string error;

	// A land material: its coordinates are positions in the named page file
	const char* land =
		"{\n"
		"\tlandPageFile\t\"dusty8\"\n"
		"\tlandDimensions\t{ 1.000000, 2.000000 }\n"
		"\tdiffusemap\t\"maps/game/tracks/dusty8/mega.tga\"\n"
		"}\n";
	CHECK( rage::ParseMaterialDecl( land, strlen( land ), decl, &error ) );
	CHECK( decl.landPageFile == "dusty8" && !decl.hasVirtualMapping && !decl.hasTransMap );

	// A model material in the shared megatexture; keys in mixed case, a comment, another tuple
	const char* model =
		"// a comment\n"
		"{\n"
		"\tpagetablemap\t_vmtrpagetable\n"
		"\tvirtualtexturefeedbackfloat\t{ 1.000000, 512.000000, 0.000000, 0.000000 }\n"
		"\tvirtualMapping\t{ 0.007813, 0.003906, 0.125000, 0.421875 }\n"
		"\ttransmap\t\"models/test/fence_trans.tga\"\n"
		"}\n";
	CHECK( rage::ParseMaterialDecl( model, strlen( model ), decl, &error ) );
	CHECK( decl.pageTableMap == "_vmtrpagetable" && decl.landPageFile.empty() );
	CHECK( decl.hasVirtualMapping && decl.virtualMapping[0] > 0.0078f && decl.virtualMapping[0] < 0.0079f
		   && decl.virtualMapping[2] == 0.125f && decl.virtualMapping[3] == 0.421875f );
	CHECK( decl.hasTransMap );

	// A tuple with the wrong count, and text with no block
	const char* bad = "{\n\tvirtualmapping\t{ 0.5, 0.5 }\n}\n";
	CHECK( !rage::ParseMaterialDecl( bad, strlen( bad ), decl, &error ) && !error.empty() );
	const char* none = "pagetablemap _vmtrpagetable";
	CHECK( !rage::ParseMaterialDecl( none, strlen( none ), decl, &error ) );
}

int main( int argc, char** argv )
{
	const std::string folder = ( argc > 1 ) ? argv[1] : ".";

	TestInflate();
	TestArchives( folder );
	TestBModel();
	TestEntities();
	TestMaterials();

	printf( "rage_selftest: %d checks, %d failed\n", checks, failures );
	return failures == 0 ? 0 : 1;
}
