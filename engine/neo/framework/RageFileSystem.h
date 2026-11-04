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

===========================================================================
*/

#ifndef __RAGE_FILESYSTEM_H__
#define __RAGE_FILESYSTEM_H__

#include <cstdint>
#include <vector>

/*
===============================================================================

	OpenRAGE: RAGE's archives (fs_ragePath), for OpenRAGE's own RAGE loaders.

	idFileSystem::OpenFileRead only serves the RAGE file types Doom 3's loaders can't mistake
	for their own (see FileSystem.cpp). OpenRAGE's loaders read any RAGE file through this.

===============================================================================
*/

// Reads a file from RAGE's archives by its built name, unpacked. False if fs_ragePath isn't
// set or the archives don't have it.
bool Rage_ReadArchiveFile( const char* name, std::vector<uint8_t>& data );

#endif // !__RAGE_FILESYSTEM_H__
