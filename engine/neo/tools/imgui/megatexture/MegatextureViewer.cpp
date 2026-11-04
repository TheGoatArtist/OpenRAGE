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

#include "precompiled.h"
#pragma hdrstop

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MegatextureViewer.h"
#include "../../edit_public.h"
#include "../../../imgui/BFGimgui.h"
#include "renderer/Material.h"
#include "renderer/Image.h"

// The megatexture decoder from tools/rage-jxr (the ragejxr library). Some platforms define
// PAGE_SIZE as a macro for the memory page size; the decoder uses the name for its pages.
#ifdef PAGE_SIZE
	#undef PAGE_SIZE
#endif
#include "megatexture.h"

static idCVar mtv_lastFile( "mtv_lastFile", "", CVAR_ARCHIVE | CVAR_TOOL, "the last .pages file opened in the megatexture viewer" );

namespace ImGuiTools
{

static const char* const OVERVIEW_IMAGE = "_megatextureOverview";
static const char* const DETAIL_IMAGE = "_megatextureDetail";

// the choices for the detail view's side, in pages
static const int VIEW_PAGES[] = { 8, 16, 32 };

// a decoded picture, RGBA
struct MegatexturePicture
{
	int					width = 0;
	int					height = 0;
	std::vector<byte>	rgba;
};

static MegatexturePicture ToPicture( const pages::LevelImage& image )
{
	MegatexturePicture picture;
	picture.width = image.width;
	picture.height = image.height;

	const size_t count = size_t( image.width ) * size_t( image.height );
	picture.rgba.resize( count * 4 );
	const uint8_t* src = image.rgb.data();
	byte* dst = picture.rgba.data();
	for( size_t i = 0; i < count; i++, src += 3, dst += 4 )
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = 255;
	}
	return picture;
}

/*
========================
UploadPicture

Uploads a picture to the named image, creating the image the first time, and returns the
material that draws it, for ImGui::Image. Main thread only.
========================
*/
static const idMaterial* UploadPicture( const char* name, const MegatexturePicture& picture )
{
	idImageOpts opts;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.width = picture.width;
	opts.height = picture.height;
	opts.numLevels = 1;

	// returns the existing image after the first call
	idImage* image = globalImages->ScratchImage( name, &opts, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	if( image == NULL )
	{
		return NULL;
	}
	if( !image->IsLoaded() )
	{
		// purged since it was made, by a vid_restart for example
		image->AllocImage( opts, TF_LINEAR, TR_CLAMP );
	}

	// reallocates the texture when the size changes
	renderSystem->UploadImage( name, picture.rgba.data(), picture.width, picture.height );

	// the implicit material for a name draws the image of the same name
	return declManager->FindMaterial( name );
}

/*
================================================
MegatextureViewer::State

Decoding runs on a worker thread, one job at a time: opening a file (with its overview), or
decoding a region of one level. While a job runs, the main thread never replaces or frees the
file the job reads, and the controls that would start another job are disabled.
================================================
*/
struct MegatextureViewer::State
{
	// what a finished job hands back to the main thread
	struct Result
	{
		bool								opened = false;		// an open job, rather than a new detail view
		std::string							path;
		std::string							error;				// empty when it worked
		std::unique_ptr<pages::PagesFile>	file;				// open jobs only
		MegatexturePicture					overview;			// open jobs only
		MegatexturePicture					detail;
		int									level = 0;
		int									x = 0;
		int									y = 0;
		size_t								decoded = 0;
		size_t								filled = 0;
		double								seconds = 0.0;
	};

	// the open file
	std::unique_ptr<pages::PagesFile>	file;
	std::string							path;
	char								pathBuf[1024] = {};
	int									overviewLevel = 0;
	int									overviewSize = 0;
	const idMaterial*					overviewMaterial = NULL;

	// the detail view on screen
	const idMaterial*					detailMaterial = NULL;
	int									detailLevel = 0;
	int									detailX = 0;
	int									detailY = 0;
	int									detailWidth = 0;
	int									detailHeight = 0;
	idStr								detailInfo;

	// what to show next
	int									level = 0;
	float								centerX = 0.5f;		// on the whole texture, 0 to 1
	float								centerY = 0.5f;
	int									viewSize = 1;		// index into VIEW_PAGES
	bool								actualSize = false;
	bool								wantDetail = false;

	idStr								status;
	idStr								error;
	int									jobStartTime = 0;

	// shared with the worker
	std::thread							worker;
	std::atomic<bool>					busy{ false };
	std::mutex							mutex;
	std::vector<std::unique_ptr<Result>>	finished;

	~State()
	{
		if( worker.joinable() )
		{
			worker.join();
		}
	}

	int LevelTexels( int lvl ) const
	{
		return ( 1 << ( int( file->levels ) - 1 - lvl ) ) * pages::PAGE_PAYLOAD;
	}

	// worker thread: hand the result over, then let the main thread start the next job
	void Finish( std::unique_ptr<Result> result )
	{
		{
			std::lock_guard<std::mutex> lock( mutex );
			finished.push_back( std::move( result ) );
		}
		busy = false;
	}

	void StartOpen( const std::string& newPath )
	{
		if( busy || newPath.empty() )
		{
			return;
		}
		if( worker.joinable() )
		{
			worker.join();
		}
		busy = true;
		error.Clear();
		status = va( "Opening %s", newPath.c_str() );
		jobStartTime = Sys_Milliseconds();

		worker = std::thread( [this, newPath]()
		{
			std::unique_ptr<Result> result( new Result );
			result->opened = true;
			result->path = newPath;
			try
			{
				const auto start = std::chrono::steady_clock::now();
				std::unique_ptr<pages::PagesFile> newFile( new pages::PagesFile( newPath ) );

				// the overview is the level 8 pages across (960 x 960 texels), or level 0 of a smaller file
				const uint32_t ovLevel = newFile->levels > 4 ? newFile->levels - 4 : 0;
				const pages::LevelImage image = pages::assemble_diffuse( *newFile, ovLevel );
				result->overview = ToPicture( image );
				result->level = int( ovLevel );
				result->decoded = image.decoded;
				result->filled = image.filled;
				result->seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
				result->file = std::move( newFile );
			}
			catch( const std::exception& e )
			{
				result->error = e.what();
			}
			catch( ... )
			{
				result->error = "unknown error";
			}
			Finish( std::move( result ) );
		} );
	}

	void StartDetail()
	{
		if( busy || !file )
		{
			return;
		}
		if( worker.joinable() )
		{
			worker.join();
		}

		// whole pages around the chosen spot, kept inside the level
		const int side = LevelTexels( level );
		const int size = Min( side, VIEW_PAGES[viewSize] * pages::PAGE_PAYLOAD );
		const int lastPage = ( side - size ) / pages::PAGE_PAYLOAD;
		const int x = idMath::ClampInt( 0, lastPage, int( centerX * side - size / 2 ) / pages::PAGE_PAYLOAD ) * pages::PAGE_PAYLOAD;
		const int y = idMath::ClampInt( 0, lastPage, int( centerY * side - size / 2 ) / pages::PAGE_PAYLOAD ) * pages::PAGE_PAYLOAD;

		busy = true;
		error.Clear();
		status = va( "Decoding level %d: %d x %d texels at (%d, %d)", level, size, size, x, y );
		jobStartTime = Sys_Milliseconds();

		pages::PagesFile* f = file.get();
		const int lvl = level;
		worker = std::thread( [this, f, lvl, x, y, size]()
		{
			std::unique_ptr<Result> result( new Result );
			result->level = lvl;
			result->x = x;
			result->y = y;
			try
			{
				const auto start = std::chrono::steady_clock::now();
				const pages::LevelImage image = pages::assemble_diffuse( *f, uint32_t( lvl ), x, y, size, size );
				result->detail = ToPicture( image );
				result->decoded = image.decoded;
				result->filled = image.filled;
				result->seconds = std::chrono::duration<double>( std::chrono::steady_clock::now() - start ).count();
			}
			catch( const std::exception& e )
			{
				result->error = e.what();
			}
			catch( ... )
			{
				result->error = "unknown error";
			}
			Finish( std::move( result ) );
		} );
	}

	void ShowDetail( const MegatexturePicture& picture, const Result& result )
	{
		detailMaterial = UploadPicture( DETAIL_IMAGE, picture );
		detailLevel = result.level;
		detailX = result.x;
		detailY = result.y;
		detailWidth = picture.width;
		detailHeight = picture.height;
		detailInfo = va( "Level %d: %d x %d texels at (%d, %d), %zu pages decoded, %zu filled from coarser pages, %.2f s",
						 result.level, picture.width, picture.height, result.x, result.y, result.decoded, result.filled, result.seconds );
	}

	void Apply( Result& result )
	{
		status.Clear();
		if( !result.error.empty() )
		{
			error = va( "%s: %s", result.opened ? result.path.c_str() : "Decoding failed", result.error.c_str() );
			return;
		}

		if( !result.opened )
		{
			ShowDetail( result.detail, result );
			return;
		}

		file = std::move( result.file );
		path = result.path;
		overviewLevel = result.level;
		overviewSize = result.overview.width;
		overviewMaterial = UploadPicture( OVERVIEW_IMAGE, result.overview );
		level = overviewLevel;
		centerX = centerY = 0.5f;
		wantDetail = false;
		mtv_lastFile.SetString( path.c_str() );

		// the first detail view is the overview level itself
		ShowDetail( result.overview, result );
	}

	// join finished work, show its results, and start the next detail view if one is wanted
	void Poll()
	{
		std::vector<std::unique_ptr<Result>> done;
		{
			std::lock_guard<std::mutex> lock( mutex );
			done.swap( finished );
		}
		if( !busy && worker.joinable() )
		{
			worker.join();
		}
		for( auto& result : done )
		{
			Apply( *result );
		}
		if( wantDetail && !busy && file )
		{
			wantDetail = false;
			StartDetail();
		}
	}

	void DrawFileRow()
	{
		ImGui::BeginDisabled( busy );
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( "File" );
		ImGui::SameLine();
		const ImGuiStyle& style = ImGui::GetStyle();
		ImGui::SetNextItemWidth( -( ImGui::CalcTextSize( "Open" ).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x ) );
		const bool enter = ImGui::InputTextWithHint( "##mtvPath", "a .pages file in your RAGE install, such as RAGE/virtualtextures/wasteland1.pages",
							pathBuf, sizeof( pathBuf ), ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if( ImGui::Button( "Open" ) || enter )
		{
			StartOpen( pathBuf );
		}
		ImGui::EndDisabled();
	}

	void DrawOverview()
	{
		if( overviewMaterial == NULL )
		{
			return;
		}

		const float side = 320.0f;
		ImGui::Image( ( void* )overviewMaterial, ImVec2( side, side ) );
		const ImVec2 corner = ImGui::GetItemRectMin();

		// click to look at that spot
		if( !busy && ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			centerX = idMath::ClampFloat( 0.0f, 1.0f, ( mouse.x - corner.x ) / side );
			centerY = idMath::ClampFloat( 0.0f, 1.0f, ( mouse.y - corner.y ) / side );
			wantDetail = true;
		}

		// outline the detail view when it shows part of its level
		if( detailMaterial != NULL )
		{
			const float levelSide = float( LevelTexels( detailLevel ) );
			if( detailWidth < levelSide || detailHeight < levelSide )
			{
				const ImVec2 a( corner.x + side * detailX / levelSide, corner.y + side * detailY / levelSide );
				const ImVec2 b( corner.x + side * ( detailX + detailWidth ) / levelSide, corner.y + side * ( detailY + detailHeight ) / levelSide );
				ImGui::GetWindowDrawList()->AddRect( a, b, IM_COL32( 255, 220, 0, 255 ), 0.0f, 0, 2.0f );
			}
		}
		ImGui::TextDisabled( "Click to look at a spot (level %d, %d x %d texels)", overviewLevel, overviewSize, overviewSize );
	}

	void DrawControls()
	{
		const int levels = int( file->levels );
		const int across0 = 1 << ( levels - 1 );
		const size_t slash = path.find_last_of( "/\\" );
		ImGui::TextWrapped( "%s: %d levels, %d x %d pages at level 0 (%d x %d texels), %zu pages stored",
							path.c_str() + ( slash == std::string::npos ? 0 : slash + 1 ), levels, across0, across0,
							across0 * pages::PAGE_PAYLOAD, across0 * pages::PAGE_PAYLOAD, file->pages.size() );
		ImGui::Separator();

		DrawOverview();
		ImGui::Separator();

		ImGui::BeginDisabled( busy );
		ImGui::SetNextItemWidth( 200.0f );
		ImGui::SliderInt( "Level", &level, 0, levels - 1, "%d" );
		if( ImGui::IsItemDeactivatedAfterEdit() )
		{
			wantDetail = true;
		}
		const int across = 1 << ( levels - 1 - level );
		ImGui::TextDisabled( "0 is the most detailed. Level %d is %d x %d pages", level, across, across );

		ImGui::SetNextItemWidth( 200.0f );
		if( ImGui::Combo( "View", &viewSize, "960 x 960 (8 x 8 pages)\0" "1920 x 1920 (16 x 16 pages)\0" "3840 x 3840 (32 x 32 pages)\0" ) )
		{
			wantDetail = true;
		}
		ImGui::EndDisabled();
		ImGui::Checkbox( "Actual size", &actualSize );
	}

	void DrawStatus()
	{
		if( busy )
		{
			ImGui::TextWrapped( "%s ... %.1f s", status.c_str(), ( Sys_Milliseconds() - jobStartTime ) / 1000.0f );
		}
		else if( detailInfo.Length() > 0 )
		{
			ImGui::TextWrapped( "%s", detailInfo.c_str() );
		}
		if( error.Length() > 0 )
		{
			ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.45f, 0.4f, 1.0f ) );
			ImGui::TextWrapped( "%s", error.c_str() );
			ImGui::PopStyleColor();
		}
	}

	void DrawDetail()
	{
		if( detailMaterial == NULL || detailWidth <= 0 || detailHeight <= 0 )
		{
			return;
		}
		if( actualSize )
		{
			ImGui::Image( ( void* )detailMaterial, ImVec2( float( detailWidth ), float( detailHeight ) ) );
			return;
		}

		// fit the window, keeping the shape
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float scale = Min( avail.x / detailWidth, avail.y / detailHeight );
		if( scale > 0.0f )
		{
			ImGui::Image( ( void* )detailMaterial, ImVec2( detailWidth * scale, detailHeight * scale ) );
		}
	}

	void DrawContents()
	{
		DrawFileRow();
		if( !file )
		{
			DrawStatus();
			return;
		}

		ImGui::BeginChild( "##mtvControls", ImVec2( 340.0f, 0.0f ), true );
		DrawControls();
		ImGui::Separator();
		DrawStatus();
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild( "##mtvDetail", ImVec2( 0.0f, 0.0f ), true, actualSize ? ImGuiWindowFlags_HorizontalScrollbar : 0 );
		DrawDetail();
		ImGui::EndChild();
	}
};

MegatextureViewer::MegatextureViewer()
	: state( new State )
	, isShown( false )
{
}

MegatextureViewer::~MegatextureViewer()
{
	delete state;
}

MegatextureViewer& MegatextureViewer::Instance()
{
	static MegatextureViewer instance;
	return instance;
}

void MegatextureViewer::Open( const char* path )
{
	if( path != NULL && path[0] != '\0' )
	{
		idStr::Copynz( state->pathBuf, path, sizeof( state->pathBuf ) );
		state->StartOpen( path );
	}
	else if( state->pathBuf[0] == '\0' )
	{
		idStr::Copynz( state->pathBuf, mtv_lastFile.GetString(), sizeof( state->pathBuf ) );
	}

	isShown = true;
	com_editors |= EDITOR_MEGATEXTURE;
	SetReleaseToolMouse( true );
}

bool MegatextureViewer::IsShown() const
{
	return isShown;
}

void MegatextureViewer::Close()
{
	isShown = false;
	com_editors &= ~EDITOR_MEGATEXTURE;
	SetReleaseToolMouse( false );
}

void MegatextureViewer::Draw()
{
	state->Poll();

	bool showTool = isShown;
	ImGui::SetNextWindowSize( ImVec2( 1280.0f, 800.0f ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( "Megatexture Viewer", &showTool ) )
	{
		SetReleaseToolMouse( true );
		state->DrawContents();
	}
	ImGui::End();

	if( isShown && !showTool )
	{
		Close();
	}
}

} // namespace ImGuiTools

CONSOLE_COMMAND_SHIP( megatextureViewer, "opens the megatexture viewer: megatextureViewer [path to a .pages file in your RAGE install]", NULL )
{
	ImGuiTools::MegatextureViewer::Instance().Open( args.Argc() > 1 ? args.Args( 1 ) : "" );
}
