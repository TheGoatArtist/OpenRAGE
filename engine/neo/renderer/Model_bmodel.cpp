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

	OpenRAGE: RAGE .bmodel files as static render models

	The file is read with rage::ParseBModel (rage/RageBModel.cpp) and each surface, split
	by its material ranges, becomes a model surface. The geometry then takes the same path
	as any other static model: FinishSurfaces() bounds it and fills in tangents, and the
	vertex cache uploads it to static GPU buffers when the level loads.

	The texture coordinates are kept as RAGE stores them. For megatexture materials they are
	positions in a megatexture, so drawing them correctly waits for megatexture support.

===============================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "RenderCommon.h"
#include "Model_local.h"

#include "../rage/RageFormats.h"

idCVar r_rageMaterial( "r_rageMaterial", "", CVAR_RENDERER, "if set, RAGE geometry is drawn with this material instead of its own" );

/*
=================
idRenderModelStatic::LoadBModel
=================
*/
bool idRenderModelStatic::LoadBModel( const char* fileName, ID_TIME_T* sourceTimeStamp )
{
	// RAGE's files predate 2016, so no mikktspace (see InitFromFile)
	*sourceTimeStamp = 0;

	idFileLocal file( fileSystem->OpenFileRead( fileName ) );
	if( file == NULL )
	{
		return false;
	}

	const int length = file->Length();
	idTempArray<byte> data( Max( length, 1 ) );
	if( length > 0 && file->Read( data.Ptr(), length ) != length )
	{
		common->Warning( "LoadBModel: couldn't read %s", fileName );
		return false;
	}

	rage::BModel bmodel;
	std::string error;
	if( !rage::ParseBModel( data.Ptr(), length, bmodel, &error ) )
	{
		common->Warning( "LoadBModel: %s: %s", fileName, error.c_str() );
		return false;
	}

	const idMaterial* overrideMaterial = NULL;
	if( r_rageMaterial.GetString()[0] != '\0' )
	{
		overrideMaterial = declManager->FindMaterial( r_rageMaterial.GetString() );
	}

	idList<int> remap;
	for( size_t surfaceNum = 0; surfaceNum < bmodel.surfaces.size(); surfaceNum++ )
	{
		const rage::BModelSurface& surface = bmodel.surfaces[surfaceNum];
		const std::vector<rage::BModelSurfacePart> parts = rage::SplitSurfaceByMaterial( bmodel, surfaceNum );

		for( const rage::BModelSurfacePart& part : parts )
		{
			if( part.triangles.empty() )
			{
				continue;
			}

			// Keep only the vertexes this part uses
			remap.AssureSize( ( int )surface.vertexes.size() );
			for( int i = 0; i < ( int )surface.vertexes.size(); i++ )
			{
				remap[i] = -1;
			}
			int numVerts = 0;
			for( uint32_t index : part.triangles )
			{
				if( remap[index] < 0 )
				{
					remap[index] = numVerts++;
				}
			}

			srfTriangles_t* tri = R_AllocStaticTriSurf();
			tri->numVerts = numVerts;
			tri->numIndexes = ( int )part.triangles.size();
			R_AllocStaticTriSurfVerts( tri, tri->numVerts );
			R_AllocStaticTriSurfIndexes( tri, tri->numIndexes );
			tri->generateNormals = !surface.hasNormals;

			for( int i = 0; i < ( int )surface.vertexes.size(); i++ )
			{
				if( remap[i] < 0 )
				{
					continue;
				}
				const rage::BModelVertex& in = surface.vertexes[i];
				idDrawVert& out = tri->verts[remap[i]];
				out.Clear();
				out.xyz.Set( in.xyz[0], in.xyz[1], in.xyz[2] );
				out.SetTexCoord( in.st[0], in.st[1] );
				if( surface.hasNormals )
				{
					out.SetNormal( in.normal[0], in.normal[1], in.normal[2] );
					out.SetTangent( in.tangent[0], in.tangent[1], in.tangent[2] );
					out.SetBiTangentSign( in.bitangentSign );
				}
				out.color[0] = in.color[0];
				out.color[1] = in.color[1];
				out.color[2] = in.color[2];
				out.color[3] = in.color[3];
			}

			for( int i = 0; i < tri->numIndexes; i++ )
			{
				tri->indexes[i] = ( triIndex_t )remap[part.triangles[i]];
			}

			modelSurface_t surf;
			surf.id = NumSurfaces();
			surf.shader = ( overrideMaterial != NULL ) ? overrideMaterial : declManager->FindMaterial( part.material.c_str() );
			surf.geometry = tri;
			AddSurface( surf );
		}
	}

	return true;
}

/*
=================
idRenderModelStatic::InitFromBModel

Builds a model with its own name, such as "_area12" for a map area, from a .bmodel file.
An empty or unreadable file gives a model with no surfaces, so a map still loads.
=================
*/
bool idRenderModelStatic::InitFromBModel( const char* modelName, const char* fileName )
{
	InitEmpty( modelName );

	ID_TIME_T sourceTimeStamp;
	const bool loaded = LoadBModel( fileName, &sourceTimeStamp );

	purged = false;
	FinishSurfaces( false );
	return loaded;
}
