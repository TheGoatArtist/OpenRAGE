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

#ifndef EDITOR_TOOLS_MEGATEXTUREVIEWER_H_
#define EDITOR_TOOLS_MEGATEXTUREVIEWER_H_

namespace ImGuiTools
{

/**
* Shows RAGE megatextures (.pages files) from the player's own RAGE install, decoded with
* the ragejxr library in tools/rage-jxr. Opened with the megatextureViewer console command.
*/
class MegatextureViewer
{
public:
	static MegatextureViewer&	Instance();

	// path may be empty: the window then offers the last file opened
	void						Open( const char* path );
	bool						IsShown() const;
	void						Draw();

	MegatextureViewer( const MegatextureViewer& ) = delete;
	void operator=( const MegatextureViewer& ) = delete;

private:
	MegatextureViewer();
	~MegatextureViewer();

	void						Close();

	struct State;
	State*						state;
	bool						isShown;
};

} // namespace ImGuiTools

#endif /* !EDITOR_TOOLS_MEGATEXTUREVIEWER_H_ */
