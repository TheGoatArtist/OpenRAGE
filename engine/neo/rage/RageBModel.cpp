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

// .bmodel reader. See docs/research/formats/bmodel.md.

#include "RageFormats.h"

#include <cstring>

namespace rage
{

namespace
{

const uint8_t	BMODEL_MAGIC[4] = { 0x18, 0x4C, 0x4D, 0x42 };
const uint16_t	STRIP_RESTART = 0xFFFF;

// Big-endian numbers, little-endian name lengths
class Reader
{
public:
	Reader( const uint8_t* data_, size_t size_ ) : data( data_ ), size( size_ ) {}

	bool Has( size_t count ) const
	{
		return size - pos >= count;
	}
	bool Skip( size_t count )
	{
		if( !Has( count ) )
		{
			return false;
		}
		pos += count;
		return true;
	}
	bool U16( uint16_t& value )
	{
		if( !Has( 2 ) )
		{
			return false;
		}
		value = uint16_t( ( data[pos] << 8 ) | data[pos + 1] );
		pos += 2;
		return true;
	}
	bool U32( uint32_t& value )
	{
		if( !Has( 4 ) )
		{
			return false;
		}
		value = ( uint32_t( data[pos] ) << 24 ) | ( uint32_t( data[pos + 1] ) << 16 ) | ( uint32_t( data[pos + 2] ) << 8 ) | data[pos + 3];
		pos += 4;
		return true;
	}
	bool U64( uint64_t& value )
	{
		uint32_t high, low;
		if( !U32( high ) || !U32( low ) )
		{
			return false;
		}
		value = ( uint64_t( high ) << 32 ) | low;
		return true;
	}
	bool Float( float& value )
	{
		uint32_t bits;
		if( !U32( bits ) )
		{
			return false;
		}
		memcpy( &value, &bits, sizeof( value ) );
		return true;
	}
	bool Name( std::string& value )
	{
		if( !Has( 4 ) )
		{
			return false;
		}
		const uint32_t length = data[pos] | ( uint32_t( data[pos + 1] ) << 8 ) | ( uint32_t( data[pos + 2] ) << 16 ) | ( uint32_t( data[pos + 3] ) << 24 );
		pos += 4;
		if( !Has( length ) )
		{
			return false;
		}
		value.assign( reinterpret_cast<const char*>( data + pos ), length );
		pos += length;
		return true;
	}
	bool Magic()
	{
		if( !Has( 4 ) || memcmp( data + pos, BMODEL_MAGIC, 4 ) != 0 )
		{
			return false;
		}
		pos += 4;
		return true;
	}
	const uint8_t* Here() const
	{
		return data + pos;
	}
	size_t Position() const
	{
		return pos;
	}
	bool AtEnd() const
	{
		return pos == size;
	}

private:
	const uint8_t*	data;
	size_t			size;
	size_t			pos = 0;
};

inline float BigFloat( const uint8_t* p )
{
	const uint32_t bits = ( uint32_t( p[0] ) << 24 ) | ( uint32_t( p[1] ) << 16 ) | ( uint32_t( p[2] ) << 8 ) | p[3];
	float value;
	memcpy( &value, &bits, sizeof( value ) );
	return value;
}

inline int16_t BigShort( const uint8_t* p )
{
	return int16_t( uint16_t( ( p[0] << 8 ) | p[1] ) );
}

// Three bytes with 128 as zero: (value - 128) / 127
inline void UnpackDirection( const uint8_t* p, float out[3] )
{
	for( int i = 0; i < 3; i++ )
	{
		out[i] = ( float( p[i] ) - 128.0f ) / 127.0f;
	}
}

size_t VertexSize( uint32_t layout )
{
	switch( layout )
	{
		case BMODEL_FORMAT_FLOAT:
			return 32;
		case BMODEL_FORMAT_PACKED_NORMAL:
			return 20;
		case BMODEL_FORMAT_PACKED:
			return 12;
		default:
			return 0;
	}
}

// Adds one triangle unless two of its corners are the same vertex
inline void AddTriangle( std::vector<uint32_t>& triangles, uint32_t a, uint32_t b, uint32_t c )
{
	if( a == b || b == c || a == c )
	{
		return;
	}
	triangles.push_back( a );
	triangles.push_back( b );
	triangles.push_back( c );
}

struct Transform
{
	float positionScale[3];
	float positionOffset[3];
	float stScale[2];
	float stOffset[2];
};

bool ParseSurface( Reader& reader, BModelSurface& surface, std::string& error )
{
	uint32_t unknown, vertexCount, indexCount;
	if( !reader.Name( surface.material ) || !reader.U32( unknown ) || !reader.U32( vertexCount ) || !reader.U32( indexCount ) || !reader.U32( surface.format ) )
	{
		error = "surface header is cut short";
		return false;
	}

	Transform transform;
	float* floats[10] =
	{
		&transform.positionScale[0], &transform.positionScale[1], &transform.positionScale[2],
		&transform.positionOffset[0], &transform.positionOffset[1], &transform.positionOffset[2],
		&transform.stScale[0], &transform.stScale[1],
		&transform.stOffset[0], &transform.stOffset[1]
	};
	for( float* f : floats )
	{
		if( !reader.Float( *f ) )
		{
			error = "surface scales are cut short";
			return false;
		}
	}

	const uint32_t layout = surface.format & ~uint32_t( BMODEL_FORMAT_STRIPS );
	const size_t vertexSize = VertexSize( layout );
	if( vertexSize == 0 )
	{
		error = "unknown vertex format " + std::to_string( surface.format );
		return false;
	}
	if( !reader.Has( size_t( vertexCount ) * vertexSize ) )
	{
		error = "vertexes are cut short";
		return false;
	}

	surface.hasNormals = ( layout != BMODEL_FORMAT_PACKED );
	surface.hasColors = ( layout == BMODEL_FORMAT_FLOAT );
	surface.vertexes.resize( vertexCount );

	const uint8_t* p = reader.Here();
	for( uint32_t i = 0; i < vertexCount; i++, p += vertexSize )
	{
		BModelVertex& v = surface.vertexes[i];
		const uint8_t* rest;
		if( layout == BMODEL_FORMAT_FLOAT )
		{
			// Full floats aren't scaled
			for( int k = 0; k < 3; k++ )
			{
				v.xyz[k] = BigFloat( p + k * 4 );
			}
			v.st[0] = BigFloat( p + 12 );
			v.st[1] = BigFloat( p + 16 );
			rest = p + 20;
		}
		else
		{
			// Three signed shorts, then 01 00, then two signed shorts, all scaled and offset
			for( int k = 0; k < 3; k++ )
			{
				v.xyz[k] = float( BigShort( p + k * 2 ) ) * transform.positionScale[k] + transform.positionOffset[k];
			}
			for( int k = 0; k < 2; k++ )
			{
				v.st[k] = float( BigShort( p + 8 + k * 2 ) ) * transform.stScale[k] + transform.stOffset[k];
			}
			rest = p + 12;
		}

		if( surface.hasNormals )
		{
			UnpackDirection( rest, v.normal );
			UnpackDirection( rest + 4, v.tangent );
			v.bitangentSign = ( rest[7] >= 128 ) ? -1.0f : 1.0f;
		}
		if( surface.hasColors )
		{
			memcpy( v.color, rest + 8, 4 );
		}
	}
	reader.Skip( size_t( vertexCount ) * vertexSize );

	// Check the count against what's left before allocating, as for the vertexes: a damaged
	// count must fail here, not allocate gigabytes
	if( !reader.Has( size_t( indexCount ) * 2 ) )
	{
		error = "indexes are cut short";
		return false;
	}
	std::vector<uint16_t> indexes( indexCount );
	for( uint32_t i = 0; i < indexCount; i++ )
	{
		if( !reader.U16( indexes[i] ) )
		{
			error = "indexes are cut short";
			return false;
		}
	}

	surface.triangles.clear();
	if( surface.format & BMODEL_FORMAT_STRIPS )
	{
		// Triangle strips separated by 0xFFFF. Triangle k of a strip uses k, k+1, k+2, with the
		// first two swapped when k is odd. Degenerate triangles only join pieces; drop them.
		size_t start = 0;
		while( start < indexes.size() )
		{
			size_t end = start;
			while( end < indexes.size() && indexes[end] != STRIP_RESTART )
			{
				end++;
			}
			for( size_t k = 0; k + 2 < end - start; k++ )
			{
				const uint32_t a = indexes[start + k];
				const uint32_t b = indexes[start + k + 1];
				const uint32_t c = indexes[start + k + 2];
				if( k & 1 )
				{
					AddTriangle( surface.triangles, b, a, c );
				}
				else
				{
					AddTriangle( surface.triangles, a, b, c );
				}
			}
			start = end + 1;
		}
	}
	else
	{
		if( indexCount % 3 != 0 )
		{
			error = "triangle list has " + std::to_string( indexCount ) + " indexes, not a multiple of 3";
			return false;
		}
		for( uint32_t i = 0; i < indexCount; i += 3 )
		{
			AddTriangle( surface.triangles, indexes[i], indexes[i + 1], indexes[i + 2] );
		}
	}

	for( uint32_t index : surface.triangles )
	{
		if( index >= vertexCount )
		{
			error = "index " + std::to_string( index ) + " is past the last vertex";
			return false;
		}
	}

	for( int k = 0; k < 3; k++ )
	{
		if( !reader.Float( surface.boundsMin[k] ) )
		{
			error = "bounds are cut short";
			return false;
		}
	}
	for( int k = 0; k < 3; k++ )
	{
		if( !reader.Float( surface.boundsMax[k] ) )
		{
			error = "bounds are cut short";
			return false;
		}
	}

	if( !reader.U32( unknown ) || !reader.Magic() )
	{
		error = "surface doesn't end with the end marker";
		return false;
	}
	return true;
}

// Parses the whole file with a given size for the header's time field
bool ParseWithTimeSize( const uint8_t* data, size_t size, size_t timeSize, BModel& out, std::string& error )
{
	out = BModel();
	Reader reader( data, size );

	if( !reader.Magic() )
	{
		// The first byte is a version: some occlusion meshes are an older 0x17 version
		if( size >= 4 && memcmp( data + 1, BMODEL_MAGIC + 1, 3 ) == 0 )
		{
			error = "unsupported .bmodel version " + std::to_string( data[0] ) + " (this reader knows version " + std::to_string( BMODEL_MAGIC[0] ) + ")";
		}
		else
		{
			error = "not a .bmodel (bad magic)";
		}
		return false;
	}

	if( timeSize == 8 )
	{
		if( !reader.U64( out.sourceTime ) )
		{
			error = "header is cut short";
			return false;
		}
	}
	else
	{
		uint32_t time32;
		if( !reader.U32( time32 ) )
		{
			error = "header is cut short";
			return false;
		}
		out.sourceTime = time32;
	}

	uint32_t surfaceCount;
	if( !reader.U32( surfaceCount ) )
	{
		error = "header is cut short";
		return false;
	}
	// Each surface takes at least 70 bytes, so a bigger count means a misread header
	if( size_t( surfaceCount ) > size / 70 + 1 )
	{
		error = "implausible surface count " + std::to_string( surfaceCount );
		return false;
	}

	out.surfaces.resize( surfaceCount );
	for( uint32_t i = 0; i < surfaceCount; i++ )
	{
		if( !ParseSurface( reader, out.surfaces[i], error ) )
		{
			error = "surface " + std::to_string( i ) + ": " + error;
			return false;
		}
	}

	uint32_t rangeCount;
	if( !reader.U32( rangeCount ) )
	{
		error = "trailer is cut short";
		return false;
	}
	if( size_t( rangeCount ) > size / 16 + 1 )
	{
		error = "implausible material range count " + std::to_string( rangeCount );
		return false;
	}
	out.ranges.resize( rangeCount );
	for( uint32_t i = 0; i < rangeCount; i++ )
	{
		BModelMaterialRange& range = out.ranges[i];
		if( !reader.Name( range.material ) || !reader.U32( range.surface ) || !reader.U32( range.firstVertex ) || !reader.U32( range.lastVertex ) )
		{
			error = "material range " + std::to_string( i ) + " is cut short";
			return false;
		}
	}

	if( !reader.AtEnd() )
	{
		error = std::to_string( size - reader.Position() ) + " bytes left after the trailer";
		return false;
	}
	return true;
}

} // namespace

bool ParseBModel( const uint8_t* data, size_t size, BModel& out, std::string* error )
{
	// bmodel.md doesn't record the size of the header's time field. Every surface ends with
	// the magic and the file ends right after the trailer, so try both sizes and keep the
	// one that reads the whole file cleanly.
	std::string error4, error8;
	if( ParseWithTimeSize( data, size, 4, out, error4 ) )
	{
		return true;
	}
	if( ParseWithTimeSize( data, size, 8, out, error8 ) )
	{
		return true;
	}
	out = BModel();
	if( error != nullptr )
	{
		*error = error4;
	}
	return false;
}

std::vector<BModelSurfacePart> SplitSurfaceByMaterial( const BModel& model, size_t surfaceIndex )
{
	std::vector<BModelSurfacePart> parts;
	if( surfaceIndex >= model.surfaces.size() )
	{
		return parts;
	}
	const BModelSurface& surface = model.surfaces[surfaceIndex];

	std::vector<const BModelMaterialRange*> ranges;
	for( const BModelMaterialRange& range : model.ranges )
	{
		if( range.surface == surfaceIndex )
		{
			ranges.push_back( &range );
		}
	}

	if( ranges.size() <= 1 )
	{
		BModelSurfacePart part;
		part.material = ranges.empty() ? surface.material : ranges[0]->material;
		part.triangles = surface.triangles;
		parts.push_back( std::move( part ) );
		return parts;
	}

	// bmodel.md doesn't say whether the last vertex is inclusive. Treat it as exclusive
	// only if some range ends exactly at the vertex count.
	bool exclusive = false;
	for( const BModelMaterialRange* range : ranges )
	{
		if( range->lastVertex == surface.vertexes.size() )
		{
			exclusive = true;
		}
	}

	parts.resize( ranges.size() + 1 );
	for( size_t r = 0; r < ranges.size(); r++ )
	{
		parts[r].material = ranges[r]->material;
	}
	parts.back().material = surface.material;

	for( size_t t = 0; t + 2 < surface.triangles.size(); t += 3 )
	{
		const uint32_t first = surface.triangles[t];
		size_t target = ranges.size();
		for( size_t r = 0; r < ranges.size(); r++ )
		{
			const uint32_t end = exclusive ? ranges[r]->lastVertex : ranges[r]->lastVertex + 1;
			if( first >= ranges[r]->firstVertex && first < end )
			{
				target = r;
				break;
			}
		}
		parts[target].triangles.insert( parts[target].triangles.end(), surface.triangles.begin() + t, surface.triangles.begin() + t + 3 );
	}

	// Drop empty parts
	std::vector<BModelSurfacePart> used;
	for( BModelSurfacePart& part : parts )
	{
		if( !part.triangles.empty() )
		{
			used.push_back( std::move( part ) );
		}
	}
	return used;
}

} // namespace rage
