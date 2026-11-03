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

/*
===============================================================================

	rage_check: runs OpenRAGE's RAGE readers over a real RAGE install.

		rage_check <RAGE folder> [--unpack]

	It mounts base/gameresources_sp_01.patch and base/gameresources.resources the way the
	engine does (patch first), then:

	- parses every .bmodel and .entities entry,
	- checks every map's _combo/<map>.proc: its header, its area count, and that each area's
	  _area<N>.bmodel is there for the engine to load,
	- with --unpack, unpacks every entry and checks it comes out at its recorded size.

	It reads the player's own game files and prints counts and failures only; nothing is
	written. rage_selftest checks the same readers without game data.

===============================================================================
*/

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "../RageFormats.h"

namespace
{

bool EndsWith( const std::string& s, const char* suffix )
{
	const size_t n = std::strlen( suffix );
	return s.size() >= n && s.compare( s.size() - n, n, suffix ) == 0;
}

// The tokens of a .proc file: braces and parentheses on their own, everything else split at
// white space, comments left out.
std::vector<std::string> ProcTokens( const char* text, size_t size, size_t maxTokens )
{
	std::vector<std::string> tokens;
	size_t i = 0;
	while( i < size && tokens.size() < maxTokens )
	{
		const char c = text[i];
		if( c == ' ' || c == '\t' || c == '\r' || c == '\n' )
		{
			i++;
		}
		else if( c == '/' && i + 1 < size && text[i + 1] == '*' )
		{
			const char* end = static_cast<const char*>( std::memchr( text + i + 2, '*', size - i - 2 ) );
			while( end != nullptr && ( end + 1 >= text + size || end[1] != '/' ) )
			{
				end = static_cast<const char*>( std::memchr( end + 1, '*', text + size - end - 1 ) );
			}
			i = end != nullptr ? size_t( end - text ) + 2 : size;
		}
		else if( c == '/' && i + 1 < size && text[i + 1] == '/' )
		{
			while( i < size && text[i] != '\n' )
			{
				i++;
			}
		}
		else if( c == '{' || c == '}' || c == '(' || c == ')' )
		{
			tokens.emplace_back( 1, c );
			i++;
		}
		else
		{
			const size_t start = i;
			while( i < size && !std::strchr( " \t\r\n{}()", text[i] ) )
			{
				i++;
			}
			tokens.emplace_back( text + start, i - start );
		}
	}
	return tokens;
}

struct Failures
{
	size_t					count = 0;
	std::vector<std::string>	shown;

	void Add( const std::string& what )
	{
		if( count++ < 20 )
		{
			shown.push_back( what );
		}
	}

	void Print( const char* title ) const
	{
		if( count == 0 )
		{
			return;
		}
		std::printf( "\n%s: %zu\n", title, count );
		for( const std::string& line : shown )
		{
			std::printf( "  %s\n", line.c_str() );
		}
		if( count > shown.size() )
		{
			std::printf( "  ... and %zu more\n", count - shown.size() );
		}
	}
};

} // namespace

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		std::fprintf( stderr, "usage: rage_check <RAGE folder> [--unpack]\n" );
		return 2;
	}
	const std::string root = argv[1];
	const bool unpackAll = argc > 2 && std::strcmp( argv[2], "--unpack" ) == 0;
	const auto start = std::chrono::steady_clock::now();

	// the same archives, in the same order, as the engine's fs_ragePath
	rage::Library library;
	const char* archiveNames[] = { "base/gameresources_sp_01.patch", "base/gameresources.resources" };
	for( const char* archiveName : archiveNames )
	{
		const std::string path = root + "/" + archiveName;
		FILE* probe = std::fopen( path.c_str(), "rb" );
		if( probe == nullptr )
		{
			std::printf( "%s: not found\n", path.c_str() );
			continue;
		}
		std::fclose( probe );

		std::string error;
		if( !library.Mount( path, &error ) )
		{
			std::printf( "%s: can't mount: %s\n", path.c_str(), error.c_str() );
			return 1;
		}
		std::printf( "%s: %zu entries\n", path.c_str(), library.GetArchive( library.NumArchives() - 1 ).Entries().size() );
	}
	if( library.NumArchives() == 0 )
	{
		std::printf( "No RAGE archives under %s\n", root.c_str() );
		return 1;
	}

	size_t entries = 0, shadowed = 0, unpacked = 0;
	uint64_t unpackedBytes = 0;
	size_t bmodels = 0, surfaces = 0, vertexes = 0, triangles = 0, bmodelsNoTriangles = 0;
	Failures olderBModels;	// an older .bmodel version (some occlusion meshes), not a reader failure
	std::map<uint32_t, size_t> formats;
	size_t entityFiles = 0, entityCount = 0, playerStarts = 0, entityFilesNoStart = 0;
	size_t maps = 0, mapsComplete = 0, areasTotal = 0;
	Failures unpackFailures, bmodelFailures, entityFailures, procFailures, areaFailures;

	// every entry once, from the first archive that has it, as the engine finds them
	std::unordered_set<std::string> seen;
	for( size_t a = 0; a < library.NumArchives(); a++ )
	{
		const rage::Archive& archive = library.GetArchive( a );
		for( const rage::ArchiveEntry& entry : archive.Entries() )
		{
			const std::string name = rage::NormalizeName( entry.name );
			if( !seen.insert( name ).second )
			{
				shadowed++;
				continue;
			}
			entries++;

			const bool isBModel = EndsWith( name, ".bmodel" );
			const bool isEntities = EndsWith( name, ".entities" );
			const bool isProc = name.compare( 0, 5, "maps/" ) == 0 && name.find( "/_combo/" ) != std::string::npos && EndsWith( name, ".proc" );
			if( !unpackAll && !isBModel && !isEntities && !isProc )
			{
				continue;
			}

			std::vector<uint8_t> data;
			std::string error;
			if( !archive.Read( entry, data, &error ) )
			{
				unpackFailures.Add( entry.name + ": " + error );
				continue;
			}
			if( data.size() != entry.size )
			{
				unpackFailures.Add( entry.name + ": unpacked to " + std::to_string( data.size() ) + " bytes, expected " + std::to_string( entry.size ) );
				continue;
			}
			unpacked++;
			unpackedBytes += data.size();

			if( isBModel )
			{
				bmodels++;
				rage::BModel model;
				if( !rage::ParseBModel( data.data(), data.size(), model, &error ) )
				{
					if( error.compare( 0, 27, "unsupported .bmodel version" ) == 0 )
					{
						olderBModels.Add( entry.name + ": " + error );
					}
					else
					{
						bmodelFailures.Add( entry.name + ": " + error );
					}
					continue;
				}
				size_t modelTriangles = 0;
				for( const rage::BModelSurface& surface : model.surfaces )
				{
					surfaces++;
					vertexes += surface.vertexes.size();
					modelTriangles += surface.triangles.size() / 3;
					formats[surface.format]++;
				}
				triangles += modelTriangles;
				if( modelTriangles == 0 )
				{
					bmodelsNoTriangles++;
				}
			}
			else if( isEntities )
			{
				entityFiles++;
				std::vector<rage::MapEntity> list;
				if( !rage::ParseEntities( reinterpret_cast<const char*>( data.data() ), data.size(), list, &error ) )
				{
					entityFailures.Add( entry.name + ": " + error );
					continue;
				}
				entityCount += list.size();
				size_t starts = 0;
				for( const rage::MapEntity& entity : list )
				{
					if( entity.className == "idPlayerStart" && entity.hasPosition )
					{
						starts++;
					}
				}
				playerStarts += starts;
				if( starts == 0 )
				{
					entityFilesNoStart++;
				}
			}
			else if( isProc )
			{
				maps++;
				const std::vector<std::string> tokens = ProcTokens( reinterpret_cast<const char*>( data.data() ), data.size(), 64 );
				if( tokens.empty() || tokens[0] != "mapProcFile005" )
				{
					procFailures.Add( entry.name + ": doesn't start with mapProcFile005" );
					continue;
				}
				int numAreas = -1;
				for( size_t t = 0; t + 2 < tokens.size(); t++ )
				{
					if( tokens[t] == "interAreaPortals" && tokens[t + 1] == "{" )
					{
						numAreas = std::atoi( tokens[t + 2].c_str() );
						break;
					}
				}
				if( numAreas <= 0 )
				{
					procFailures.Add( entry.name + ": no interAreaPortals area count" );
					continue;
				}
				areasTotal += size_t( numAreas );

				// the engine loads <combo folder>/_area<N>.bmodel for every area
				const std::string comboFolder = name.substr( 0, name.rfind( '/' ) + 1 );
				int missing = 0;
				for( int i = 0; i < numAreas; i++ )
				{
					if( library.Find( comboFolder + "_area" + std::to_string( i ) + ".bmodel", nullptr ) == nullptr )
					{
						missing++;
					}
				}
				if( missing > 0 )
				{
					areaFailures.Add( entry.name + ": " + std::to_string( missing ) + " of " + std::to_string( numAreas ) + " area models missing" );
				}
				else
				{
					mapsComplete++;
				}
			}
		}
	}

	const double seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
	std::printf( "\n%zu entries (%zu more replaced by the patch), %zu unpacked (%.1f MB), %.1f s\n",
				 entries, shadowed, unpacked, unpackedBytes / ( 1024.0 * 1024.0 ), seconds );
	std::printf( ".bmodel:   %zu parsed of %zu (%zu in an older version); %zu surfaces, %zu vertexes, %zu triangles; %zu with no triangles\n",
				 bmodels - bmodelFailures.count - olderBModels.count, bmodels, olderBModels.count, surfaces, vertexes, triangles, bmodelsNoTriangles );
	std::printf( "           vertex formats:" );
	for( const auto& format : formats )
	{
		std::printf( " 0x%X x%zu", format.first, format.second );
	}
	std::printf( "\n" );
	std::printf( ".entities: %zu parsed of %zu; %zu entities, %zu player starts; %zu files without a player start\n",
				 entityFiles - entityFailures.count, entityFiles, entityCount, playerStarts, entityFilesNoStart );
	std::printf( "maps:      %zu .proc files, %zu with every area model present, %zu areas in all\n", maps, mapsComplete, areasTotal );

	olderBModels.Print( ".bmodel files in an older version (not failures; the loader doesn't read them)" );
	unpackFailures.Print( "Entries that didn't unpack" );
	bmodelFailures.Print( ".bmodel files that didn't parse" );
	entityFailures.Print( ".entities files that didn't parse" );
	procFailures.Print( ".proc files that didn't check out" );
	areaFailures.Print( "Maps with missing area models" );

	const size_t failures = unpackFailures.count + bmodelFailures.count + entityFailures.count + procFailures.count + areaFailures.count;
	std::printf( "\n%s\n", failures == 0 ? "All checks passed." : "Some checks failed (see above)." );
	return failures == 0 ? 0 : 1;
}
