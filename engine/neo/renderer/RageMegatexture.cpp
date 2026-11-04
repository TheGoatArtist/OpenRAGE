/*
===========================================================================

OpenRAGE GPL Source Code
Copyright (C) 2026 the OpenRAGE contributors

This file is part of OpenRAGE, which is based on RBDOOM-3-BFG and on id Software's
Doom 3 BFG Edition GPL Source Code.

OpenRAGE is free software: you can redistribute it and/or modify it under the terms of
the GNU General Public License as published by the Free Software Foundation, either
version 3 of the License, or (at your option) any later version, with id Software's
additional terms (see COPYING.txt) and the additional permission in
LICENSE-EXCEPTION.md.

OpenRAGE is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the GNU General Public License for more details.

===========================================================================
*/

/*
===============================================================================

	OpenRAGE: RAGE's megatextures on RAGE geometry, before real megatexture streaming.

	A RAGE surface's material decl says where its texture is (docs/research/formats/
	materials.md):

	- land materials (landPageFile "dusty8") use texture coordinates that are positions in
	  that map's own megatexture, virtualtextures/<name>.pages;
	- most others (pagetablemap _vmtrpagetable) sit in the shared virtualtextures/_vmtr.pages
	  at their virtualmapping rectangle { w, h, x, y }.

	Both megatextures are decoded with the ragejxr library from tools/rage-jxr, from the
	player's own install, into in-memory images: the most detailed level that fits in
	r_rageMegatextureSize, with the coarser levels as its mip levels. Nothing is written to
	disk. The materials that draw them are in base/materials/openrage.mtr, drawn unlit for now.

	This is a stopgap: one level for a whole megatexture is blurry up close, and streaming the
	pages that are in view is the real thing (docs/research/engine/megatexture-streaming.md).

===============================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include <cctype>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "RenderCommon.h"
#include "sys/DeviceManager.h"
#include "../framework/RageFileSystem.h"
#include "../rage/RageFormats.h"

// Some platforms define PAGE_SIZE as a macro for the memory page size; the decoder uses the
// name for its pages
#ifdef PAGE_SIZE
	#undef PAGE_SIZE
#endif
#include "megatexture.h"

extern DeviceManager* deviceManager;

idCVar r_rageMegatextures( "r_rageMegatextures", "1", CVAR_RENDERER | CVAR_BOOL, "draw RAGE geometry with its megatextures, decoded from the RAGE install when a map loads" );
idCVar r_rageMegatextureSize( "r_rageMegatextureSize", "8192", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "largest side of a decoded RAGE megatexture in texels: the most detailed level that fits is used", 960, 16384 );

namespace
{

const char* const LAND_IMAGE = "_rageLand";
const char* const VMTR_IMAGE = "_rageVmtr";
const char* const LAND_MATERIAL = "openrage/rageLand";
const char* const VMTR_MATERIAL = "openrage/rageVmtr";

// Rows of the top level decoded and uploaded at a time, to keep memory down (16 pages)
const int STRIP_ROWS = 16 * pages::PAGE_PAYLOAD;

struct CachedDecl
{
	bool				found = false;
	rage::MaterialDecl	decl;
};

std::unordered_map<std::string, CachedDecl>	materialDecls;

// What a megatexture image holds now
struct MegatextureImage
{
	idStr	pagesPath;		// the .pages file decoded into it, or tried
	bool	loaded = false;
};

MegatextureImage	landImage;
MegatextureImage	vmtrImage;
idStr				landThisMap;		// the land page file the current map's surfaces use
bool				warnedOtherLand = false;

// The material's RAGE decl, cached; NULL when it has none
const rage::MaterialDecl* FindMaterialDecl( const char* name )
{
	std::string key = name;
	for( char& c : key )
	{
		c = ( c == '\\' ) ? '/' : char( std::tolower( static_cast<unsigned char>( c ) ) );
	}

	auto it = materialDecls.find( key );
	if( it != materialDecls.end() )
	{
		return it->second.found ? &it->second.decl : NULL;
	}

	CachedDecl& cached = materialDecls[key];
	std::vector<uint8_t> text;
	// Most decls are generated/decls/material/<name>.decl; land materials sit next to their
	// map's geometry as <name>.decl
	if( Rage_ReadArchiveFile( va( "generated/decls/material/%s.decl", key.c_str() ), text ) || Rage_ReadArchiveFile( va( "%s.decl", key.c_str() ), text ) )
	{
		std::string error;
		cached.found = rage::ParseMaterialDecl( reinterpret_cast<const char*>( text.data() ), text.size(), cached.decl, &error );
		if( !cached.found )
		{
			common->Warning( "RAGE material %s: %s", name, error.c_str() );
		}
	}
	return cached.found ? &cached.decl : NULL;
}

/*
========================
LoadMegatexture

Decodes a .pages file from the RAGE install into the named image: the most detailed level that
fits in r_rageMegatextureSize, and every coarser level as a mip level. Does nothing if the image
already holds that file.
========================
*/
bool LoadMegatexture( const char* imageName, const char* pagesFile, MegatextureImage& state )
{
	idStr path = cvarSystem->GetCVarString( "fs_ragePath" );
	path.AppendPath( pagesFile );
	path.BackSlashesToSlashes();
	if( state.pagesPath.Icmp( path ) == 0 )
	{
		return state.loaded;	// done already, or tried and failed
	}
	state.pagesPath = path;
	state.loaded = false;

	const int startTime = Sys_Milliseconds();
	common->UpdateLevelLoadPacifier();

	std::unique_ptr<pages::PagesFile> file;
	try
	{
		file.reset( new pages::PagesFile( path.c_str() ) );
	}
	catch( const std::exception& e )
	{
		common->Warning( "RAGE megatexture %s: %s", path.c_str(), e.what() );
		return false;
	}

	// The most detailed level that fits; each coarser level is half as wide, like a mip level
	const int levels = int( file->levels );
	const int maxSide = r_rageMegatextureSize.GetInteger();
	int first = 0;
	while( first < levels - 1 && ( pages::PAGE_PAYLOAD << ( levels - 1 - first ) ) > maxSide )
	{
		first++;
	}
	const int side = pages::PAGE_PAYLOAD << ( levels - 1 - first );
	const int numMips = levels - first;

	idImageOpts opts;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.width = side;
	opts.height = side;
	opts.numLevels = numMips;

	idImage* image = globalImages->GetImage( imageName );
	if( image == NULL )
	{
		image = globalImages->ScratchImage( imageName, &opts, TF_DEFAULT, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	}
	else
	{
		image->AllocImage( opts, TF_DEFAULT, TR_CLAMP );
	}
	if( image == NULL || !image->GetTextureHandle() )
	{
		common->Warning( "RAGE megatexture %s: couldn't make a %d x %d texture", path.c_str(), side, side );
		return false;
	}

	nvrhi::CommandListHandle commandList = deviceManager->GetDevice()->createCommandList();
	commandList->open();
	commandList->beginTrackingTextureState( image->GetTextureHandle(), nvrhi::AllSubresources, nvrhi::ResourceStates::Common );

	size_t decoded = 0, filled = 0, failedStrips = 0;
	std::vector<byte> rgba;
	for( int mip = 0; mip < numMips; mip++ )
	{
		const int level = first + mip;
		const int mipSide = side >> mip;
		const int strip = Min( mipSide, STRIP_ROWS );
		for( int y = 0; y < mipSide; y += strip )
		{
			common->UpdateLevelLoadPacifier();

			const int rows = Min( strip, mipSide - y );
			pages::LevelImage part;
			try
			{
				part = pages::assemble_diffuse( *file, uint32_t( level ), 0, y, mipSide, rows );
			}
			catch( const std::exception& e )
			{
				// A damaged page stream: leave these rows out and carry on
				if( failedStrips++ == 0 )
				{
					common->Warning( "RAGE megatexture %s, level %d: %s", path.c_str(), level, e.what() );
				}
				continue;
			}
			decoded += part.decoded;
			filled += part.filled;

			const size_t count = size_t( mipSide ) * size_t( rows );
			rgba.resize( count * 4 );
			const uint8_t* src = part.rgb.data();
			byte* dst = rgba.data();
			for( size_t i = 0; i < count; i++, src += 3, dst += 4 )
			{
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 255;
			}
			image->SubImageUpload( mip, 0, y, 0, mipSide, rows, rgba.data(), commandList );
		}
	}

	commandList->setPermanentTextureState( image->GetTextureHandle(), nvrhi::ResourceStates::ShaderResource );
	commandList->commitBarriers();
	commandList->close();
	deviceManager->GetDevice()->executeCommandList( commandList );

	common->Printf( "RAGE megatexture %s: level %d of %d, %d x %d texels with %d mip levels, %zu pages decoded, %zu filled from coarser pages, %.1f s\n",
					path.c_str(), first, levels, side, side, numMips, decoded, filled, ( Sys_Milliseconds() - startTime ) / 1000.0f );
	if( failedStrips > 0 )
	{
		common->Warning( "RAGE megatexture %s: %zu strips couldn't be decoded", path.c_str(), failedStrips );
	}
	state.loaded = true;
	return true;
}

} // namespace

/*
========================
Rage_BeginMapMegatextures

A RAGE map is about to load its geometry. Images stay loaded, so reloading a map doesn't decode
its megatexture again.
========================
*/
void Rage_BeginMapMegatextures()
{
	landThisMap.Clear();
	warnedOtherLand = false;
}

/*
========================
Rage_MaterialForSurface

The material to draw a RAGE surface with, from its RAGE material decl, or NULL to fall back to
the engine's own lookup of the name. When the surface sits in _vmtr, virtualMapped is set and
virtualMapping holds its { w, h, x, y }: texture coordinates (u, v) become (x + u w, y + v h).
========================
*/
const idMaterial* Rage_MaterialForSurface( const char* rageMaterial, float virtualMapping[4], bool& virtualMapped )
{
	virtualMapped = false;
	if( !r_rageMegatextures.GetBool() )
	{
		return NULL;
	}

	const rage::MaterialDecl* decl = FindMaterialDecl( rageMaterial );
	if( decl == NULL || decl->hasTransMap )
	{
		// Translucent materials need the pages' mask layer, which isn't decoded yet
		return NULL;
	}

	if( !decl->landPageFile.empty() )
	{
		// One land megatexture per map
		if( landThisMap.IsEmpty() )
		{
			landThisMap = decl->landPageFile.c_str();
		}
		else if( landThisMap.Icmp( decl->landPageFile.c_str() ) != 0 )
		{
			if( !warnedOtherLand )
			{
				common->Warning( "RAGE map uses land page files %s and %s; only %s is drawn", landThisMap.c_str(), decl->landPageFile.c_str(), landThisMap.c_str() );
				warnedOtherLand = true;
			}
			return NULL;
		}

		if( !LoadMegatexture( LAND_IMAGE, va( "virtualtextures/%s.pages", landThisMap.c_str() ), landImage ) )
		{
			return NULL;
		}
		return declManager->FindMaterial( LAND_MATERIAL );
	}

	if( decl->hasVirtualMapping && idStr::Icmp( decl->pageTableMap.c_str(), "_vmtrpagetable" ) == 0 )
	{
		if( !LoadMegatexture( VMTR_IMAGE, "virtualtextures/_vmtr.pages", vmtrImage ) )
		{
			return NULL;
		}
		for( int i = 0; i < 4; i++ )
		{
			virtualMapping[i] = decl->virtualMapping[i];
		}
		virtualMapped = true;
		return declManager->FindMaterial( VMTR_MATERIAL );
	}

	return NULL;
}
