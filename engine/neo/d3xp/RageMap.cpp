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

	OpenRAGE: a stand-in .map for RAGE maps

	RAGE maps have no Doom-style .map file: their entities are in <map>.entities, in id Tech
	5's own syntax, and their collision is in .bcm files. Until those are loaded, the game
	gets a stand-in map file with:

	- a worldspawn holding one brush, a floor slab under the whole map, so the world has a
	  collision model and the clip sectors get sensible bounds;
	- an info_player_start at the map's first idPlayerStart from <map>.entities.

	There's no collision with RAGE's own geometry yet, so look around with noclip.

===============================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "Game_local.h"

#include "../rage/RageFormats.h"

// idMapFile keeps its name protected; this lets the stand-in set it
class idRageStandInMapFile : public idMapFile
{
public:
	void SetName( const char* mapName )
	{
		name = mapName;
		name.StripFileExtension();
		fileTime = 0;
	}
};

/*
================
Rage_AddBoxBrush

Adds an axis-aligned box brush. Brush planes face outwards (see
idCollisionModelManagerLocal::ConvertBrush), with d = -(normal . point on the face).
================
*/
static void Rage_AddBoxBrush( idMapEntity* entity, const idBounds& box, const char* material )
{
	idMapBrush* brush = new( TAG_GAME ) idMapBrush();

	idVec3 textureMatrix[2];
	textureMatrix[0].Set( 1.0f / 128.0f, 0.0f, 0.0f );
	textureMatrix[1].Set( 0.0f, 1.0f / 128.0f, 0.0f );

	for( int axis = 0; axis < 3; axis++ )
	{
		for( int side = 0; side < 2; side++ )
		{
			idVec3 normal = vec3_zero;
			normal[axis] = ( side == 0 ) ? -1.0f : 1.0f;
			const float faceCoord = box[side][axis];

			idMapBrushSide* brushSide = new( TAG_GAME ) idMapBrushSide();
			brushSide->SetPlane( idPlane( normal, -normal[axis] * faceCoord ) );
			brushSide->SetMaterial( material );
			brushSide->SetTextureMatrix( textureMatrix );
			brush->AddSide( brushSide );
		}
	}

	entity->AddPrimitive( brush );
}

/*
================
Rage_FindPlayerStart

The first idPlayerStart in <map>.entities with a position
================
*/
static bool Rage_FindPlayerStart( const idStr& mapBase, idVec3& origin, float& yaw, idStr& startName )
{
	idStr entitiesPath = mapBase;
	entitiesPath.SetFileExtension( "entities" );

	idFileLocal file( fileSystem->OpenFileRead( entitiesPath ) );
	if( file == NULL )
	{
		gameLocal.Warning( "RAGE map: %s not found", entitiesPath.c_str() );
		return false;
	}

	const int length = file->Length();
	idTempArray<char> text( length + 1 );
	if( file->Read( text.Ptr(), length ) != length )
	{
		gameLocal.Warning( "RAGE map: couldn't read %s", entitiesPath.c_str() );
		return false;
	}
	text[length] = '\0';

	std::vector<rage::MapEntity> entities;
	std::string error;
	if( !rage::ParseEntities( text.Ptr(), length, entities, &error ) )
	{
		// Keep what was read before the problem
		gameLocal.Warning( "RAGE map: %s: %s", entitiesPath.c_str(), error.c_str() );
	}

	for( const rage::MapEntity& entity : entities )
	{
		if( entity.className == "idPlayerStart" && entity.hasPosition )
		{
			origin.Set( entity.position[0], entity.position[1], entity.position[2] );
			yaw = entity.hasForward ? RAD2DEG( idMath::ATan( entity.forward[1], entity.forward[0] ) ) : 0.0f;
			startName = entity.name.c_str();
			return true;
		}
	}
	return false;
}

/*
================
Rage_MakeStandInMapFile

Returns a stand-in map file if mapName is a RAGE map, NULL otherwise. The render world
must already hold the map (idRenderWorldLocal::InitFromMap runs before the game loads it).
================
*/
idMapFile* Rage_MakeStandInMapFile( const char* mapName, const idRenderWorld* renderWorld )
{
	idStr mapBase = mapName;
	mapBase.StripFileExtension();
	idStr shortName = mapBase;
	shortName.StripPath();

	// A RAGE map has <map>/_combo/<map>.proc
	idStr procPath = mapBase;
	procPath.Append( "/_combo/" );
	procPath.Append( shortName );
	procPath.Append( ".proc" );
	{
		idFileLocal probe( fileSystem->OpenFileRead( procPath ) );
		if( probe == NULL )
		{
			return NULL;
		}
	}

	// The map's extent, from the areas the render world loaded
	idBounds mapBounds;
	mapBounds.Clear();
	if( renderWorld != NULL )
	{
		for( int i = 0; i < renderWorld->NumAreas(); i++ )
		{
			const idBounds areaBounds = renderWorld->AreaBounds( i );
			if( !areaBounds.IsCleared() )
			{
				mapBounds.AddBounds( areaBounds );
			}
		}
	}
	if( mapBounds.IsCleared() )
	{
		mapBounds = idBounds( idVec3( -4096, -4096, -4096 ), idVec3( 4096, 4096, 4096 ) );
	}

	idVec3 startOrigin = mapBounds.GetCenter();
	float startYaw = 0.0f;
	idStr startName = "(none found, using the map's centre)";
	Rage_FindPlayerStart( mapBase, startOrigin, startYaw, startName );

	idRageStandInMapFile* mapFile = new( TAG_GAME ) idRageStandInMapFile();
	mapFile->SetName( mapName );

	idMapEntity* world = new( TAG_GAME ) idMapEntity();
	world->epairs.Set( "classname", "worldspawn" );
	world->epairs.Set( "name", "world" );

	// A 64-unit-thick floor 128 units under the map, 1024 units wider on every side
	idBounds floor;
	floor[0].Set( mapBounds[0].x - 1024.0f, mapBounds[0].y - 1024.0f, mapBounds[0].z - 192.0f );
	floor[1].Set( mapBounds[1].x + 1024.0f, mapBounds[1].y + 1024.0f, mapBounds[0].z - 128.0f );
	Rage_AddBoxBrush( world, floor, "textures/common/caulk" );
	mapFile->AddEntity( world );

	idMapEntity* start = new( TAG_GAME ) idMapEntity();
	start->epairs.Set( "classname", "info_player_start" );
	start->epairs.Set( "name", "info_player_start_rage" );
	start->epairs.SetVector( "origin", startOrigin );
	start->epairs.SetFloat( "angle", startYaw );
	mapFile->AddEntity( start );

	gameLocal.Printf( "RAGE map %s: no .map file, so a stand-in is used\n", mapBase.c_str() );
	gameLocal.Printf( "  player start %s at ( %.0f %.0f %.0f ), facing %.0f degrees\n", startName.c_str(), startOrigin.x, startOrigin.y, startOrigin.z, startYaw );
	gameLocal.Printf( "  no collision with RAGE's geometry yet: use noclip to look around\n" );
	return mapFile;
}
