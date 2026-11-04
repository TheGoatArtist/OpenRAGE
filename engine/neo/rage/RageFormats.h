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

	Readers for RAGE (2011) file formats.

	Every format here is described in OpenRAGE's research notes, docs/research/formats/:
	resources-archive.md, bmodel.md and maps.md. The readers only use the C++ standard
	library and zlib, not the engine, so rage_selftest can check them on their own.

	Nothing here ships game data: the files are read from the player's own RAGE install.

===============================================================================
*/

#ifndef OPENRAGE_RAGE_FORMATS_H
#define OPENRAGE_RAGE_FORMATS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace rage
{

// Lower-case a resource name and turn backslashes into forward slashes, so names compare
// the same way however they were written.
std::string NormalizeName( const std::string& name );

/*
===============================================================================

	.resources and .patch archives (resources-archive.md)

===============================================================================
*/

struct ArchiveEntry
{
	uint32_t	id = 0;
	std::string	type;			// such as "model", "image", "material"
	std::string	source;			// the asset it was built from; may be empty
	std::string	name;			// the built file's name
	uint32_t	offset = 0;		// where the data starts in the archive
	uint32_t	size = 0;		// size after unpacking
	uint32_t	storedSize = 0;	// size in the archive; equal to size when stored as is, 0 when empty
};

class Archive
{
public:
	Archive() = default;
	~Archive();

	Archive( const Archive& ) = delete;
	Archive& operator=( const Archive& ) = delete;

	// Reads the archive's table. The file stays open for Read().
	bool							Open( const std::string& path, std::string* error );
	void							Close();

	const std::string&				Path() const
	{
		return path;
	}
	const std::vector<ArchiveEntry>&	Entries() const
	{
		return entries;
	}

	// Looks an entry up by its name, then (unless sourceNames is false) by its source name.
	// Case and slash direction don't matter. Returns nullptr if it isn't in this archive.
	const ArchiveEntry*				Find( const std::string& name, bool sourceNames = true ) const;

	// Reads and unpacks an entry's data from this archive.
	bool							Read( const ArchiveEntry& entry, std::vector<uint8_t>& out, std::string* error ) const;

private:
	std::string						path;
	FILE*							file = nullptr;
	uint64_t						fileSize = 0;
	std::vector<ArchiveEntry>		entries;
	std::unordered_map<std::string, size_t> byName;
	std::unordered_map<std::string, size_t> bySource;
};

// Several archives searched in the order they were mounted, so mount patch archives
// before the base archive they update (a patch replaces base entries by name).
class Library
{
public:
	bool							Mount( const std::string& path, std::string* error );
	void							Clear();
	size_t							NumArchives() const
	{
		return archives.size();
	}
	const Archive&					GetArchive( size_t i ) const
	{
		return *archives[i];
	}

	// Finds the first archive that has the entry; archiveOut may be nullptr. sourceNames as
	// for Archive::Find.
	const ArchiveEntry*				Find( const std::string& name, const Archive** archiveOut, bool sourceNames = true ) const;
	bool							Read( const std::string& name, std::vector<uint8_t>& out, std::string* error ) const;

private:
	std::vector<Archive*>			archives;

public:
	~Library();
};

// Unpacks raw deflate data (no zlib header). RAGE's streams are flushed, not finished, so
// they have no final block: the data counts as complete once outSize bytes have come out.
bool InflateRaw( const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize );

/*
===============================================================================

	.bmodel static models and map area geometry (bmodel.md)

===============================================================================
*/

enum
{
	BMODEL_FORMAT_STRIPS		= 0x200,	// indexes are triangle strips separated by 0xFFFF
	BMODEL_FORMAT_FLOAT			= 0x1F,		// 32 bytes: float position and texture coordinates, normal, tangent, color
	BMODEL_FORMAT_PACKED_NORMAL	= 0x77,		// 20 bytes: packed position and texture coordinates, normal, tangent
	BMODEL_FORMAT_PACKED		= 0x63		// 12 bytes: packed position and texture coordinates only
};

struct BModelVertex
{
	float		xyz[3] = { 0, 0, 0 };
	float		st[2] = { 0, 0 };
	float		normal[3] = { 0, 0, 0 };	// zero when the format has no normals
	float		tangent[3] = { 0, 0, 0 };
	float		bitangentSign = 1.0f;		// from the tangent's fourth byte (0 or 255); the meaning is a guess
	uint8_t		color[4] = { 255, 255, 255, 255 };
};

struct BModelSurface
{
	std::string					material;
	uint32_t					format = 0;
	bool						hasNormals = false;
	bool						hasColors = false;
	std::vector<BModelVertex>	vertexes;
	std::vector<uint32_t>		triangles;	// three vertex numbers per triangle; degenerate triangles are left out
	float						boundsMin[3] = { 0, 0, 0 };
	float						boundsMax[3] = { 0, 0, 0 };
};

struct BModelMaterialRange
{
	std::string	material;
	uint32_t	surface = 0;
	uint32_t	firstVertex = 0;
	uint32_t	lastVertex = 0;
};

struct BModel
{
	uint64_t							sourceTime = 0;
	std::vector<BModelSurface>			surfaces;
	std::vector<BModelMaterialRange>	ranges;
};

bool ParseBModel( const uint8_t* data, size_t size, BModel& out, std::string* error );

// One drawable piece of a surface: a material and the triangles that use it.
struct BModelSurfacePart
{
	std::string				material;
	std::vector<uint32_t>	triangles;	// vertex numbers into the surface's vertexes
};

// Splits a surface by its material ranges. A surface with no ranges, or a single one,
// gives one part. With several, each triangle goes to the range that holds its first
// vertex, and triangles outside every range keep the surface's own material.
std::vector<BModelSurfacePart> SplitSurfaceByMaterial( const BModel& model, size_t surfaceIndex );

/*
===============================================================================

	.entities map entity files (maps.md)

===============================================================================
*/

struct MapEntity
{
	std::string	name;				// the entityDef's name, such as "player_start_2"
	std::string	className;			// "class", such as "idPlayerStart"
	std::string	inherit;			// "inherit", such as "player/start"
	bool		hasPosition = false;
	float		position[3] = { 0, 0, 0 };	// edit.spawnPosition; missing components are 0
	bool		hasForward = false;
	float		forward[3] = { 1, 0, 0 };	// first row of edit.spawnOrientation.mat
};

bool ParseEntities( const char* text, size_t size, std::vector<MapEntity>& out, std::string* error );

/*
===============================================================================

	Material declarations (materials.md): only what places a material in a megatexture

===============================================================================
*/

struct MaterialDecl
{
	std::string	landPageFile;		// such as "dusty8": a land material, whose texture coordinates are positions in that page file's megatexture
	std::string	pageTableMap;		// such as "_vmtrpagetable", the shared model megatexture
	bool		hasVirtualMapping = false;
	float		virtualMapping[4] = { 1, 1, 0, 0 };	// w, h, x, y: the material's rectangle in _vmtr, as fractions
	bool		hasTransMap = false;	// a translucent material
};

// A material is one brace block of "key <tab> value" lines; keys are matched ignoring case.
bool ParseMaterialDecl( const char* text, size_t size, MaterialDecl& out, std::string* error );

} // namespace rage

#endif // !OPENRAGE_RAGE_FORMATS_H
